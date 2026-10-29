#!/usr/bin/env python3
"""Run a cross build's CTest suites on the system it was built for: the
Windows test binaries a Linux machine built (the windows-cross-stub preset),
on Windows, from a checkout of the same commit.

    # where it was built
    ctest --test-dir build/cmake/windows-cross-stub --show-only=json-v1 > tests.json
    # on Windows, with the build's binaries in the same place under this checkout
    tools/run_cross_tests.py tests.json --root /home/runner/work/x/x --labels "nogame|game"

A path under --root (the build machine's checkout) in a command, an argument,
the working directory or the environment is the same path under this one. A
test whose program is not one of the build's .exe files (a CMake script or a
Python check, which ran where it was built) is reported as skipped, as is one
whose labels do not match --labels (a regular expression, as ctest -L).
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def properties(test):
    return {p["name"]: p["value"] for p in test.get("properties", [])}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("tests", type=Path, help="ctest --show-only=json-v1 output")
    p.add_argument("--root", required=True, help="the checkout's path on the machine that built the tests")
    p.add_argument("--labels", default=".", help="run the tests with a label matching this (default all)")
    args = p.parse_args()
    root = args.root.rstrip("/") + "/"

    def here(value):
        return str(value).replace(root, ROOT.as_posix() + "/").replace(args.root, ROOT.as_posix())

    results = {"passed": [], "failed": [], "skipped": []}
    for test in json.loads(args.tests.read_text())["tests"]:
        name, props = test["name"], properties(test)
        command = [here(part) for part in test.get("command") or []]
        if not command or not command[0].lower().endswith(".exe") or not Path(command[0]).is_file():
            results["skipped"].append((name, "not a test binary of the build"))
            continue
        if props.get("DISABLED") or not any(re.search(args.labels, label) for label in props.get("LABELS", [])):
            results["skipped"].append((name, "labels %s" % ",".join(props.get("LABELS", []))))
            continue
        env = dict(os.environ)
        for entry in props.get("ENVIRONMENT", []):
            key, _, value = entry.partition("=")
            env[key] = here(value)
        cwd = here(props.get("WORKING_DIRECTORY", ROOT / "kit"))
        timeout = float(props.get("TIMEOUT", 1500))
        print("::group::%s" % name, flush=True)
        start = time.monotonic()
        try:
            code = subprocess.run(command, cwd=cwd, env=env, timeout=timeout).returncode
            outcome = "exit %d" % code
        except subprocess.TimeoutExpired:
            code, outcome = None, "timed out after %ds" % timeout
        if code is not None and props.get("WILL_FAIL"):
            code = 0 if code else 1
        skip = props.get("SKIP_RETURN_CODE")
        print("::endgroup::", flush=True)
        elapsed = "%.1fs" % (time.monotonic() - start)
        if skip is not None and code == int(skip):
            results["skipped"].append((name, "skipped itself"))
        elif code == 0:
            results["passed"].append((name, elapsed))
        else:
            results["failed"].append((name, outcome))
            print("::error::%s: %s" % (name, outcome), flush=True)
    for kind in ("passed", "skipped", "failed"):
        print("%d %s" % (len(results[kind]), kind))
        for name, why in results[kind]:
            print("  %-36s %s" % (name, why))
    if not results["passed"] and not results["failed"]:
        sys.exit("No test ran: are the binaries where the build left them?")
    return 1 if results["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
