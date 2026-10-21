#!/usr/bin/env python3
"""Write the launcher's install manifest for one GOG offline installer.

The manifest lists every file the launcher keeps from the installer (all of
them minus launcher-games.json "exclude") with its size and SHA-256, so an
install can be checked without any game file in the app. Hashes are computed
from an extracted copy of the same installer; each file is first checked
against the installer's own MD5/SHA-1 (innoextract --list-checksums), so a copy
that was changed after extraction (a profile written, a patch applied) fails
here instead of producing a wrong manifest.

    python3 -I tools/gen_install_manifest.py --installer setup_...exe \
        --extracted ~/Heroes\\ V/game --out launcher-manifests/gog-1207661143-ru.json
"""

import argparse
import fnmatch
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINE = re.compile(r'^ - (?P<names>".*?)(?: \((?P<size>[^)]*)\))?(?: (?P<algo>MD5|SHA-1|CRC32|Adler32) (?P<sum>[0-9a-f]+))?$')
NAME = re.compile(r'"([^"]+)"(?: \[([^\]]*)\])?')


def excluded(rel, patterns):
    first = rel.split("/", 1)[0].lower()
    name = rel.rsplit("/", 1)[-1].lower()
    return any(fnmatch.fnmatchcase(first, p.lower()) or fnmatch.fnmatchcase(name, p.lower()) for p in patterns)


def listing(innoextract, installer):
    out = subprocess.run([innoextract, "--list-sizes", "--list-checksums", "--color=0", "--progress=0",
                          str(installer)], check=True, capture_output=True, text=True).stdout
    entries = []
    for line in out.splitlines():
        m = LINE.match(line)
        if not m or m["names"].endswith('/"'):
            continue  # header, footer, directory entries
        names = [n for n, tag in NAME.findall(m["names"]) if tag != "temp"]
        entries.append((names, m["algo"], m["sum"]))
    return entries


def gog_id(innoextract, installer):
    out = subprocess.run([innoextract, "--gog-game-id", "--color=0", str(installer)],
                         check=True, capture_output=True, text=True).stdout
    m = re.search(r"GOG\.com game ID is (\d+)", out)
    return m.group(1) if m else None


def find(root, rel):
    """The file at `rel` under `root`, matched without case like the installer."""
    path = root
    for part in rel.split("/"):
        exact = path / part
        if exact.exists():
            path = exact
            continue
        matches = [c for c in path.iterdir() if c.name.lower() == part.lower()] if path.is_dir() else []
        if not matches:
            return None
        path = matches[0]
    return path


def digests(path):
    sha256, md5, sha1 = hashlib.sha256(), hashlib.md5(), hashlib.sha1()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            sha256.update(chunk)
            md5.update(chunk)
            sha1.update(chunk)
    return {"sha256": sha256.hexdigest(), "MD5": md5.hexdigest(), "SHA-1": sha1.hexdigest()}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--installer", type=Path, required=True, help="setup_*.exe (its -1.bin beside it)")
    ap.add_argument("--extracted", type=Path, required=True, help="the same installer, extracted")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--config", type=Path, default=ROOT / "launcher-games.json")
    ap.add_argument("--innoextract", default="innoextract")
    args = ap.parse_args()

    exclude = json.loads(args.config.read_text()).get("exclude", [])
    files, bad, missing, skipped = [], [], [], 0
    for names, algo, expected in listing(args.innoextract, args.installer):
        for rel in names:
            if excluded(rel, exclude):
                skipped += 1
                continue
            path = find(args.extracted, rel)
            if path is None or not path.is_file():
                missing.append(rel)
                continue
            d = digests(path)
            if algo in d and d[algo] != expected:
                bad.append(rel)
                continue
            files.append({"path": rel, "size": path.stat().st_size, "sha256": d["sha256"]})
    if missing or bad:
        for rel in missing:
            print("missing from the extracted copy:", rel, file=sys.stderr)
        for rel in bad:
            print("differs from the installer:", rel, file=sys.stderr)
        sys.exit(1)
    files.sort(key=lambda f: f["path"].lower())
    manifest = {
        "gogId": gog_id(args.innoextract, args.installer),
        "installer": args.installer.name,
        "files": files,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    # One file per line keeps the diff of a changed installer readable.
    head = json.dumps({k: v for k, v in manifest.items() if k != "files"}, ensure_ascii=False)[:-1]
    rows = ",\n".join("  " + json.dumps(f, ensure_ascii=False) for f in files)
    args.out.write_text(head + ', "files": [\n' + rows + "\n]}\n")
    total = sum(f["size"] for f in files)
    print("%s: %d files, %.2f GB, %d excluded" % (args.out.name, len(files), total / 1e9, skipped))


if __name__ == "__main__":
    main()
