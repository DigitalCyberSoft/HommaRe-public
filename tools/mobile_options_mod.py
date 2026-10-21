#!/usr/bin/env python3
"""Build a user mod that takes the screen resolution out of the game's options.

    tools/mobile_options_mod.py --base-game ~/Heroes\\ V/game --out build/mobile/MobileOptions.h5u
    tools/mobile_options_mod.py --game hof --base-game ~/Heroes\\ V/game \\
        --out build/mobile/MobileOptions-hof.pak
    tools/mobile_options_mod.py --game toe --base-game ~/Heroes\\ V/game-toe \\
        --out build/mobile/MobileOptions-toe.h5u

On an iPhone or iPad the game draws at the screen's own size, whatever the
screen is now (sdl/main.cpp follow_screen resizes it as a foldable folds), and
the player chooses the 3D's share of it - the render quality, upscaled by
MetalFX - in the launcher instead (host.display/render_quality). The game's
own resolution list (Options, Video: ScreenResolution, gfx_resolution) has
nothing left to choose, so this mod hides it: one change, <Hide>true</Hide>
on that option in UI/uiconsts.(UIGameConstsH5).xdb, the newest copy of the
game's own layers. Derived from the player's own game files and never
committed: --recipe writes the recipe (tools/ui_recipe.py) the app makes it
from on iOS (game.toml [ios] ui_mods, kit/host/ui_mods.cpp).

Hammers of Fate keeps its copy in dataa1, mounted above data\\*.pak and
never reading UserMODs: the result goes in the profile as
data/zz-mobile-options.pak, which native/hof_paks.c mounts above both."""

import argparse
import re
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ui_recipe  # noqa: E402
from widescreen_mod import BASE_PAKS, HOF_PAKS  # noqa: E402

TOE_PAKS = ["data/data.pak", "data/a2p1-data.pak"]
UICONSTS = "UI/uiconsts.(UIGameConstsH5).xdb"
# Later than any entry of the game's own paks: where two hold a file, the
# game takes the newer.
ENTRY_DATE = (2031, 1, 1, 0, 0, 0)
# Where the app puts the mod in the player's profile.
RECIPE_OUT = {"base": "UserMODs/MobileOptions.h5u", "hof": "data/zz-mobile-options.pak",
              "toe": "UserMODs/MobileOptions.h5u"}
RESOLUTION = re.compile(rb"(<ID>ScreenResolution</ID>.*?<Hide>)false(</Hide>)", re.S)


def newest(paks):
    """The last layer's copy of uiconsts among `paks`, or None."""
    data = None
    for pak in paks:
        if not pak.is_file():
            continue
        with zipfile.ZipFile(pak) as z:
            if UICONSTS in z.namelist():
                data = z.read(UICONSTS)
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--base-game", type=Path, required=True,
                   help="the game's install folder (the base game's for hof, Tribes of the East's for toe)")
    p.add_argument("--game", choices=("base", "hof", "toe"), default="base")
    p.add_argument("--out", type=Path, required=True, help="the .h5u (or for hof the .pak) to write")
    p.add_argument("--recipe", type=Path, help="also write the recipe (tools/ui_recipe.py) the app makes it from")
    args = p.parse_args()

    paks = {"base": BASE_PAKS, "hof": HOF_PAKS, "toe": TOE_PAKS}[args.game]
    data = newest([args.base_game / name for name in paks])
    if data is None:
        sys.exit("No %s found: check --base-game" % UICONSTS)
    out, n = RESOLUTION.subn(rb"\1true\2", data, count=1)
    if n != 1:
        sys.exit("No visible ScreenResolution option in %s" % UICONSTS)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        info = zipfile.ZipInfo(UICONSTS, date_time=ENTRY_DATE)
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, out)
    if args.recipe:
        ui_recipe.write(args.recipe, name="MobileOptions" + ("" if args.game == "base" else "-" + args.game),
                        out=RECIPE_OUT[args.game], date="%04d-%02d-%02d %02d:%02d:%02d" % ENTRY_DATE,
                        layers=paks, files={UICONSTS: out}, sources={UICONSTS: (UICONSTS, data)},
                        comment="Made by tools/mobile_options_mod.py --game %s; applied by kit/host/ui_mods.cpp."
                                % args.game)
    print("%s: the screen resolution option hidden" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
