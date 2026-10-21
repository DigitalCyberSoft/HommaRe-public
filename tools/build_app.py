#!/usr/bin/env python3
"""Build the app the player installs the games into: HommaRe with the three
games' modules, no game file and no mod made from one, and check it.

    tools/build_app.py --target app          # build/HommaRe.app (macOS)
    tools/build_app.py --target ios-sim      # build/ios-sim/.../HommaRe.app
    RECOMP_IOS_TEAM=<team> tools/build_app.py --target ios

Each game (the base game, games/hof, games/toe) is built with
kit/tools/build.py --game-modules --no-game-files --no-install --release
(a player's build: no performance overlay; --overlay keeps it), the
expansions' modules go into the base game's app (kit/tools/assemble_games.py)
and tools/check_bundle.py checks the result (--game compares it with your
installs too). The launcher in it installs the games from the GOG offline
installers (innoextract: a helper on the Mac, a library on iOS) and the
interface mods are made from the recipes when a game starts. Install the iOS
app with `xcrun devicectl device install app --device <id> <app>` or
`xcrun simctl install <udid> <app>`.
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAMES = [ROOT, ROOT / "games/hof", ROOT / "games/toe"]
sys.path.insert(0, str(ROOT / "kit/tools"))
from assemble_games import built_app, load_game  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--target", choices=("app", "ios-sim", "ios"), default="app")
    p.add_argument("--jobs", type=int, help="passed to kit/tools/build.py")
    p.add_argument("--game", type=Path, action="append", default=[],
                   help="an install to compare the app with (tools/check_bundle.py --game)")
    p.add_argument("--overlay", action="store_true",
                   help="keep the kit's performance overlay (F11): build without --release")
    p.add_argument("--skip-build", action="store_true", help="assemble and check what is built already")
    args = p.parse_args()
    platform = {"app": "macos"}.get(args.target, args.target)

    if not args.skip_build:
        for game in GAMES:
            cmd = [sys.executable, str(ROOT / "kit/tools/build.py"), "--game-dir", str(game), "--target", args.target,
                   "--game-modules", "--no-game-files", "--no-install"]
            if not args.overlay:
                cmd.append("--release")
            if args.jobs:
                cmd += ["--jobs", str(args.jobs)]
            print("+ " + " ".join(cmd), flush=True)
            subprocess.run(cmd, cwd=ROOT, check=True)
    _, app_name = load_game(ROOT)
    app = built_app(ROOT / "build", app_name, platform)
    if not app.is_dir():
        sys.exit("No %s after the build" % app)
    cmd = [sys.executable, str(ROOT / "kit/tools/assemble_games.py"), "--app", str(app), "--platform", platform]
    for game in GAMES[1:]:
        cmd += ["--game", str(game)]
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)
    check = [sys.executable, str(ROOT / "tools/check_bundle.py"), str(app)]
    for g in args.game:
        check += ["--game", str(g.expanduser())]
    print("+ " + " ".join(check), flush=True)
    return subprocess.run(check, cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
