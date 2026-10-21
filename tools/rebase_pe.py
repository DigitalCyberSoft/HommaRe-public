#!/usr/bin/env python3
"""Rebase the game's DLLs so the kit can map them inside its guest arena.

    python3 tools/rebase_pe.py [GAME_DIR] [--only NAME ...]

The kit maps every module at its preferred base and has no relocation
support. granny2.dll wants 0x50000000, fmod.dll and zlib1.dll 0x10000000
(all three cannot sit at their own bases, and the first is outside the 1 GB
arena), so for every [modules.aux.*] in game.toml whose path is under
rebased/ this writes a copy relocated to that section's `base` into rebased/,
applying the DLL's own .reloc table, and updates the section's sha256 and
size lines to match. The originals are untouched. GAME_DIR defaults to
original/ (the flattened view, where bin/'s DLLs sit at the top).
"""
import hashlib
import re
import struct
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def targets(toml_text, only):
    """name -> (game.toml key, new base) for every rebased aux module."""
    result = {}
    for key, entry in tomllib.loads(toml_text).get("modules", {}).get("aux", {}).items():
        name = entry["name"].lower()
        if not str(entry.get("path", "")).startswith("rebased/"):
            continue
        if only and name not in only:
            continue
        result[name] = (key, int(entry["base"]))
    return result


def rebase(data, new_base):
    data = bytearray(data)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x10B:
        raise ValueError("not a PE32 image")
    old_base, = struct.unpack_from("<I", data, opt + 28)
    size_image, = struct.unpack_from("<I", data, opt + 56)
    ndirs, = struct.unpack_from("<I", data, opt + 92)
    secs = []
    for i in range(nsec):
        o = opt + opt_size + i * 40
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((va, max(vsize, rsize), raw, rsize))

    def off(rva):
        for va, span, raw, rsize in secs:
            if va <= rva < va + span:
                if rva - va >= rsize:
                    raise ValueError("relocation target %#x is in uninitialised data" % rva)
                return raw + rva - va
        raise ValueError("rva %#x is outside every section" % rva)

    if ndirs <= 5:
        raise ValueError("no relocation directory")
    rel_rva, rel_size = struct.unpack_from("<II", data, opt + 96 + 5 * 8)
    if not rel_rva or not rel_size:
        raise ValueError("image has no relocations (stripped)")
    delta = (new_base - old_base) & 0xFFFFFFFF
    p, end, count = off(rel_rva), off(rel_rva) + rel_size, 0
    while p + 8 <= end:
        page, block = struct.unpack_from("<II", data, p)
        if block < 8:
            break
        for k in range((block - 8) // 2):
            entry, = struct.unpack_from("<H", data, p + 8 + 2 * k)
            kind, at = entry >> 12, entry & 0xFFF
            if kind == 0:          # IMAGE_REL_BASED_ABSOLUTE: padding
                continue
            if kind != 3:          # only HIGHLOW occurs in x86 images
                raise ValueError("unsupported relocation type %d" % kind)
            o = off(page + at)
            v, = struct.unpack_from("<I", data, o)
            struct.pack_into("<I", data, o, (v + delta) & 0xFFFFFFFF)
            count += 1
        p += block
    struct.pack_into("<I", data, opt + 28, new_base)   # ImageBase
    struct.pack_into("<I", data, opt + 64, 0)          # CheckSum (no longer valid)
    if ndirs > 11:                                     # bound imports point at the old bases
        struct.pack_into("<II", data, opt + 96 + 11 * 8, 0, 0)
    return bytes(data), old_base, size_image, count


def main():
    args = sys.argv[1:]
    only = set()
    if "--only" in args:
        i = args.index("--only")
        only = {a.lower() for a in args[i + 1:]}
        args = args[:i]
    if len(args) > 1:
        sys.exit(__doc__)
    game = Path(args[0]).expanduser() if args else ROOT / "original"
    out = ROOT / "rebased"
    out.mkdir(exist_ok=True)
    toml = ROOT / "game.toml"
    text = toml.read_text()
    todo = targets(text, only)
    if not todo:
        sys.exit("game.toml names no [modules.aux.*] with a rebased/ path%s" % (" among --only" if only else ""))
    for name, (key, base) in todo.items():
        src = next((p for p in game.iterdir() if p.name.lower() == name), None)
        if src is None:
            sys.exit("%s is not in %s" % (name, game))
        data, old, size, n = rebase(src.read_bytes(), base)
        (out / name).write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        print("%-12s %#010x -> %#010x  size %#x  %d relocations  sha256 %s" % (name, old, base, size, n, digest))
        section = re.search(r"(?ms)^\[modules\.aux\.%s\]\n.*?(?=^\[|\Z)" % re.escape(key), text)
        if not section:
            sys.exit("game.toml has no [modules.aux.%s]" % key)
        body = re.sub(r'(?m)^sha256 = "[0-9a-f]*"', 'sha256 = "%s"' % digest, section.group(0))
        body = re.sub(r"(?m)^size = 0x[0-9a-fA-F]+", "size = %#x" % size, body)
        text = text[:section.start()] + body + text[section.end():]
    toml.write_text(text)
    print("Wrote rebased/ and updated game.toml.")


if __name__ == "__main__":
    main()
