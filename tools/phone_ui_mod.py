#!/usr/bin/env python3
"""Build a user mod that lays the interface out for a phone.

    tools/phone_ui_mod.py --base-game ~/Heroes\\ V/game \\
        --widescreen build/widescreen/Widescreen.h5u --out build/phone/PhoneUI.h5u
    tools/phone_ui_mod.py --game hof --base-game ~/Heroes\\ V/game \\
        --widescreen build/widescreen/Widescreen-hof.pak --out build/phone/PhoneUI-hof.pak
    tools/phone_ui_mod.py --game toe --base-game ~/Heroes\\ V/game-toe \\
        --out build/phone/PhoneUI-toe.h5u

The game's corner menus are jog dials: a disc with round buttons around it.
On a phone, where native/widescreen.c draws them larger, the disc takes a
corner of the screen for little use. This mod lays the buttons out in an L
instead: the screen's main button (end of turn, defend, leave the town) in
the corner, a row of buttons beside it and a column above it. On the map and
in a battle the buttons are windows of the screen's root and the disc is
hidden; in a town each dial owns its buttons, so the dial becomes a frame
without the disc, about the size it had, with its buttons laid in it (the
left dial mirrored). The buttons' own art is round and stays as it is.

Only the files of the windows it changes are written: each one taken from the
wide-screen mod when that has it (tools/widescreen_mod.py), otherwise from the
base game's own data, and changed in its <Placement> (and <Visible> for a
disc, <Background> for a frame) alone. Its entries carry a date later than the
wide-screen mod's, so the game takes these copies where both mods hold the
same file. It is derived from the player's own game files and is never
committed; game.toml seeds it on an iPhone only ([ios] profile_files under
phone/).

The layout itself is native now (native/phone_layout.c): --c-table writes
native/phone_layout_table.h from SCREENS below, which lays the windows out
this way only while the window is phone-sized (a foldable iPhone's closed
screen, not its opened one) and switches as it folds. The mod this writes by
default is then neutral - it keeps the profile seeding (game.toml [ios]
profile_files) and replaces an older copy that moved the windows itself;
--data-layout writes the old mod that does.

The expansions have the same windows (names, sizes, the dials' slots):
--game hof reads Hammers of Fate's paks over the base game's (its wide-screen
pak stretches the roots), and it has no wait button in a battle, so the
buttons beside defend move up one place; Hammers of Fate mounts no UserMODs,
so the result goes in the profile as data/zz-phone-ui.pak, which native/
hof_paks.c mounts above its own data. --game toe reads Tribes of the East's
own data (it anchors its screens itself; no wide-screen mod) and is a .h5u
like the base game's.

--recipe writes what it changed without the files (tools/ui_recipe.py): the
app makes the same mod from the player's own install with it, over the
wide-screen mod it made the same way."""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ui_recipe  # noqa: E402
from widescreen_mod import BASE_PAKS, HOF_PAKS, PLACEMENT, RECIPE_OUT as WIDE_OUT, read_layers  # noqa: E402

TOE_PAKS = ["data/data.pak", "data/a2p1-data.pak"]

# Later than any entry of the wide-screen mod and of the game's own paks:
# where two paks hold a file, the game takes the newer.
ENTRY_DATE = (2031, 1, 1, 0, 0, 0)
# Where the app puts the mod in the player's profile: phone/... only on an
# iPhone with the phone interface on (host/sdl/platform_ui_ios.mm).
RECIPE_OUT = {"base": "phone/UserMODs/PhoneUI.h5u", "hof": "phone/data/zz-phone-ui.pak",
              "toe": "phone/UserMODs/PhoneUI.h5u"}

# Spacing in the interface's virtual pixels at its own scale (the phone's
# panel scale multiplies it): the margin from the screen's edge and the gap
# between two buttons.
MARGIN = 6
GAP = 6
SMALL = 57  # the round buttons
BIG = 82    # a corner's main button
STEP = SMALL + GAP

# Specs:
#   "hide"                      the window hidden (a jog dial's disc)
#   ("corner", c, dx, dy)       a child of the screen's root against corner c
#                               ("br", "bl", "tr"), its outer edges dx, dy in
#   ("at", x, y)                a child of another window at x, y in it
#   ("frame", w, h)             a shared description: w x h, no background
# Two windows may share a place where the game shows one or the other (the
# grail's dig button stands in for "move", the ghost's guard for "end turn").


def row(i):
    """The i-th small button beside a corner's main button (0 is next to it),
    its outer edge's distance from the corner."""
    return MARGIN + BIG + GAP + i * STEP


def column(i):
    """The i-th small button above a corner's main button (0 is next to it)."""
    return MARGIN + BIG + GAP + i * STEP


# A corner as one window holding its buttons (the town's dials own theirs):
# the main button in the corner, ROW small ones beside it, COLUMN above it.
FRAME_ROW, FRAME_COLUMN = 2, 4
FRAME_W = BIG + FRAME_ROW * STEP
FRAME_H = BIG + FRAME_COLUMN * STEP


def in_frame(side, slot, i=0):
    """Where a button goes in a frame anchored at the bottom `side` ("r", "l"):
    slot "main", "row" (i-th beside it) or "column" (i-th above it)."""
    if slot == "main":
        x, y = FRAME_W - BIG, FRAME_H - BIG
    elif slot == "row":
        x, y = FRAME_W - BIG - (i + 1) * STEP, FRAME_H - SMALL
    else:
        x, y = FRAME_W - SMALL, FRAME_H - BIG - (i + 1) * STEP
    if side == "l":  # mirrored: the main button in the left corner
        w = BIG if slot == "main" else SMALL
        x = FRAME_W - x - w
    return ("at", x, y)


def l_frame(main, row_items, column_items):
    """An L of buttons in a frame of their own, anchored at its bottom right:
    `main` (name, w, h) in the corner, `row_items` leftwards beside it and
    `column_items` upwards above it, each aligned on the frame's bottom or
    right edge. (frame w, frame h, {name: ("at", x, y)})."""
    name, mw, mh = main
    fw = mw + sum(w + GAP for _, w, _ in row_items)
    fh = mh + sum(h + GAP for _, _, h in column_items)
    fw = max(fw, max([w for _, w, _ in column_items] + [mw]))
    fh = max(fh, max([h for _, _, h in row_items] + [mh]))
    at = {name: ("at", fw - mw, fh - mh)}
    x = fw - mw
    for n, w, h in row_items:
        x -= GAP + w
        at[n] = ("at", x, fh - h)
    y = fh - mh
    for n, w, h in column_items:
        y -= GAP + h
        at[n] = ("at", fw - w, y)
    return fw, fh, at


# The town's inner screens (build, hire, the tavern...) take one of two dials
# the game shares among its screens (UIGameRoot JOG_DIAL_5BUTTONS and
# JOG_DIAL_EDIT_5BUTTONS, the second with the count to hire); the code puts
# their buttons in the five slots: leaving in the centre one, buying (build,
# hire, the hand with coins) in the bottom left one, plus and minus in the
# top right and top left ones when hiring. Both become one L frame: leaving,
# the big button, in the corner, as in the town; a row beside it ending in
# buying, read leftwards as buy, minus, plus, leave; the last slot (hire all,
# the full list...) above the corner with the count over it.
# native/widescreen.c knows the frame by its size (DIAL_W x DIAL_H,
# 271 x 167) and moves it to the screen's corner at the town's scale,
# whatever screen it is in. One size for both: the slots' files are shared.
DIAL_W, DIAL_H, DIAL = l_frame(
    ("5Butt/Center.(WindowMSButton).xdb", BIG, BIG),
    [("5Butt/RightTop.(WindowMSButton).xdb", SMALL, SMALL),
     ("5Butt/LeftTop.(WindowMSButton).xdb", SMALL, SMALL),
     ("5Butt/LeftBottom.(WindowMSButton).xdb", SMALL, SMALL)],
    [("5Butt/RightBottom.(WindowMSButton).xdb", SMALL, SMALL),
     ("5ButtEdit/Edit.(WindowEditLine).xdb", 100, 16)])
assert (DIAL_W, DIAL_H) == (271, 167), "native/widescreen.c DIAL_FRAME_W/H"


# The town's left dial's buttons (in_frame("l", ...)): children of JogDialLeft.
LEFT_DIAL = {"Special.(WindowMSButton).xdb", "EnterTavern.(WindowMSButton).xdb",
             "EnterMarket.(WindowMSButton).xdb", "EnterMagicGuild.(WindowMSButton).xdb",
             "Blacksmith.(WindowMSButton).xdb", "Shipyard.(WindowMSButton).xdb",
             "BlackMarket.(WindowMSButton).xdb"}


SCREENS = {
    # The adventure map (AdventureScreen-Heavy): end of turn (with its ring
    # of the week's days) in the corner; move, spell book and next hero to
    # its left; above it, up from it, the other level, the quest log and the
    # menu.
    "UI/AdventureScreen-heavy/": {
        "JogDialBase.(WindowSimple).xdb": "hide",
        "JogDialButton.(WindowMSButton).xdb": ("corner", "br", MARGIN, MARGIN),
        "GhostGuard.(WindowMSButton).xdb": ("corner", "br", MARGIN, MARGIN),
        "OnJog-MoveHero.(WindowMSButton).xdb": ("corner", "br", row(0), MARGIN),
        "OnJog-DigGrail.(WindowMSButton).xdb": ("corner", "br", row(0), MARGIN),
        "OnJog-Spellbook.(WindowMSButton).xdb": ("corner", "br", row(1), MARGIN),
        "OnJog-NextHero.(WindowMSButton).xdb": ("corner", "br", row(2), MARGIN),
        "OnJog-Underground.(WindowMSButton).xdb": ("corner", "br", MARGIN, column(0)),
        "OnJog-Scenario.(WindowMSButton).xdb": ("corner", "br", MARGIN, column(1)),
        "OnJog-Menu.(WindowMSButton).xdb": ("corner", "br", MARGIN, column(2)),
    },
    # A battle (CombatScreen-Heavy): defend (next creature while placing) in
    # the corner; wait, spell book (or the special ability, or while placing
    # the start of the battle) and flee to its left; auto combat and the menu
    # above it.
    "UI/CombatArena-Heavy/": {
        "Jogdial.(WindowSimple).xdb": "hide",
    },
    "UI/CombatScreen-Heavy/": {
        "OnJog-Defend.(WindowMSButton).xdb": ("corner", "br", MARGIN, MARGIN),
        "OnJog-NextCreature.(WindowMSButton).xdb": ("corner", "br", MARGIN, MARGIN),
        "OnJog-Wait.(WindowMSButton).xdb": ("corner", "br", row(0), MARGIN),
        "OnJog-Spellbook.(WindowMSButton).xdb": ("corner", "br", row(1), MARGIN),
        "OnJog-Special.(WindowMSButton).xdb": ("corner", "br", row(1), MARGIN),
        "OnJog-StartCombat.(WindowMSButton).xdb": ("corner", "br", row(1), MARGIN),
        "OnJog-Flee.(WindowMSButton).xdb": ("corner", "br", row(2), MARGIN),
        "OnJog-AutoCombat.(WindowMSButton).xdb": ("corner", "br", MARGIN, column(0)),
        "OnJog-Menu.(WindowMSButton).xdb": ("corner", "br", MARGIN, column(1)),
    },
    # A town (TownScreen): its two dials own their buttons, so each becomes a
    # frame without the disc, against its bottom corner, the buttons laid in
    # it: leave the town in the right corner, the hall and the fort beside
    # it, the upgrade (or the view), town info and the previous and next town
    # above; the town's special building in the left corner, the tavern and
    # the market beside it, the mage guild, the blacksmith, the shipyard and
    # the black market above.
    "UI/TownScreen/": {
        "JogDialBase.(WindowSimple).xdb": ("corner", "br", MARGIN, MARGIN),
        "JogDialRight.(WindowMSButtonShared).xdb": ("frame", FRAME_W, FRAME_H),
        "ExitTown.(WindowMSButton).xdb": in_frame("r", "main"),
        "EnterHall.(WindowMSButton).xdb": in_frame("r", "row", 0),
        "EnterFort.(WindowMSButton).xdb": in_frame("r", "row", 1),
        "UpgradeCreature.(WindowMSButton).xdb": in_frame("r", "column", 0),
        "CityView.(WindowMSButton).xdb": in_frame("r", "column", 0),
        "PrevTown.(WindowMSButton).xdb": in_frame("r", "column", 2),
        "NextTown.(WindowMSButton).xdb": in_frame("r", "column", 3),
        "JogDialLeft.(WindowSimple).xdb": ("corner", "bl", MARGIN, MARGIN),
        "JogDialLeft.(WindowSimpleShared).xdb": ("frame", FRAME_W, FRAME_H),
        "Special.(WindowMSButton).xdb": in_frame("l", "main"),
        "EnterTavern.(WindowMSButton).xdb": in_frame("l", "row", 0),
        "EnterMarket.(WindowMSButton).xdb": in_frame("l", "row", 1),
        "EnterMagicGuild.(WindowMSButton).xdb": in_frame("l", "column", 0),
        "Blacksmith.(WindowMSButton).xdb": in_frame("l", "column", 1),
        "Shipyard.(WindowMSButton).xdb": in_frame("l", "column", 2),
        "BlackMarket.(WindowMSButton).xdb": in_frame("l", "column", 3),
    },
    # The town's name, centred over the screen (native/widescreen.c draws the
    # top's middle centred on the screen itself, outside the safe area).
    "UI/TownScreen/@name": {
        "BuildingDesc.(WindowStaticText).xdb": ("center_x", 74),
    },
    # The dials of the town's build and hire screens.
    "UI/Common/JogDial/": dict({
        "JogDial.(WindowSimpleShared).xdb": ("frame", DIAL_W, DIAL_H),
        "EditJogDial.(WindowSimpleShared).xdb": ("frame", DIAL_W, DIAL_H),
    }, **DIAL),
    # The right dial's town info button lives with the map's dial art.
    "UI/AdventureScreen-FPP-2/JogDial/": {
        "TownInfo.(WindowMSButton).xdb": in_frame("r", "column", 1),
    },
}


# What the game calls each window at run time (its <Name>), where the file's
# own name differs, and how its parent is known: by the parent's <Name>, or
# for a screen's root, which has none, by a child only that root has.
WINDOW_NAMES = {
    "UpgradeCreature": "UpgradeCreatures", "Special": "EnterSpecial",
    "EnterMagicGuild": "MagicGuild", "Blacksmith": "EnterBlacksmith",
    "Shipyard": "EnterShipyard", "BlackMarket": "buy_artifacts",
    "5Butt/Center": "button_0", "5Butt/LeftTop": "button_1", "5Butt/LeftBottom": "button_2",
    "5Butt/RightTop": "button_3", "5Butt/RightBottom": "button_4", "5ButtEdit/Edit": "jd_editbox",
}
# folder -> (parent's name, a child the parent has); one of the two.
PARENTS = {
    "UI/AdventureScreen-heavy/": (None, "JogDialButton"),
    "UI/CombatArena-Heavy/": (None, "OnJog-Defend"),
    "UI/CombatScreen-Heavy/": (None, "OnJog-Defend"),
    "UI/TownScreen/": (None, "BuildingDesc"),
    "UI/TownScreen/@name": (None, "BuildingDesc"),
    "UI/Common/JogDial/": (None, "button_0"),
    "UI/AdventureScreen-FPP-2/JogDial/": ("JogDialBase", None),
}
# The frames' shared descriptions, as the windows that use them: the town's
# dials by name (a child of the town's root), the inner screens' dial as the
# window that has its centre button.
FRAME_WINDOWS = {
    "UI/TownScreen/JogDialRight.(WindowMSButtonShared).xdb": ("JogDialBase", None, None, "BuildingDesc"),
    "UI/TownScreen/JogDialLeft.(WindowSimpleShared).xdb": ("JogDialLeft", None, None, "BuildingDesc"),
    "UI/Common/JogDial/JogDial.(WindowSimpleShared).xdb": (None, "button_0", None, None),
    "UI/Common/JogDial/EditJogDial.(WindowSimpleShared).xdb": (None, "button_0", None, None),
}
ANCHOR = {"EPA_LOW_END": 0, "ERA_CENTER": 1, "EPA_HIGH_END": 2}


def layout_entries(game):
    """SCREENS for `game` as native/phone_layout.c's rows: (name, with_child,
    parent, parent_child, hor, ver, x, y, set_x, set_y, w, h, flags)."""
    screens, absent = screens_for(game)
    rows = {}

    def row_for(key):
        return rows.setdefault(key, {"hor": -1, "ver": -1, "x": 0, "y": 0, "set_x": 0, "set_y": 0,
                                     "w": 0, "h": 0, "flags": []})
    for folder, windows in screens.items():
        base = folder.split("@")[0]
        for file, spec in windows.items():
            path = base + file
            if path in absent:
                continue
            if spec[0] == "frame" if isinstance(spec, tuple) else False:
                key = FRAME_WINDOWS[path]
                r = row_for(key)
                r["w"], r["h"] = spec[1], spec[2]
                r["flags"].append("PL_BARE")
                continue
            stem = file.split(".(")[0]
            name = WINDOW_NAMES.get(stem, stem)
            parent, parent_child = PARENTS[folder]
            if folder == "UI/TownScreen/" and isinstance(spec, tuple) and spec[0] == "at":
                parent, parent_child = ("JogDialLeft" if file in LEFT_DIAL else "JogDialBase"), None
            r = row_for((name, None, parent, parent_child))
            if spec == "hide":
                r["flags"].append("PL_HIDE")
            elif spec[0] == "corner":
                _, corner, dx, dy = spec
                right, bottom = corner[1] == "r", corner[0] == "b"
                r.update(hor=2 if right else 0, ver=2 if bottom else 0, x=-dx if right else dx,
                         y=-dy if bottom else dy, set_x=1, set_y=1)
            elif spec[0] == "at":
                r.update(hor=0, ver=0, x=spec[1], y=spec[2], set_x=1, set_y=1)
            elif spec[0] == "center_x":
                r.update(hor=1, x=0, y=spec[1], set_x=1, set_y=1)
            else:
                raise ValueError("no native layout for %r" % (spec,))
    return rows


def c_table(out):
    """native/phone_layout_table.h: the base game's and Tribes of the East's
    rows, and Hammers of Fate's (HOMM5_PHONE_LAYOUT_HOF)."""
    def c_str(v):
        return '"%s"' % v if v else "NULL"
    lines = ["/* Generated by tools/phone_ui_mod.py --c-table: do not edit. A phone's",
             " * corner rows of buttons instead of the jog dials, as native/phone_layout.c",
             " * applies them (see PhoneLayout there). */"]
    for macro, game in (("#ifndef HOMM5_PHONE_LAYOUT_HOF", "base"), ("#else", "hof")):
        lines.append(macro)
        lines.append("static const PhoneLayout PHONE_LAYOUT[] = {")
        for (name, with_child, parent, parent_child), r in sorted(
                layout_entries(game).items(), key=lambda kv: tuple(str(x) for x in kv[0])):
            flags = "|".join(sorted(set(r["flags"]))) or "0"
            lines.append("    {%s, %s, %s, %s, %d, %d, %d, %d, %d, %d, %d, %d, %s}," % (
                c_str(name), c_str(with_child), c_str(parent), c_str(parent_child), r["hor"], r["ver"],
                r["x"], r["y"], r["set_x"], r["set_y"], r["w"], r["h"], flags))
        lines.append("};")
    lines.append("#endif")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    print("%s: written" % out)


def set_pair(e, name, x, y):
    f = e.find(name)
    f.find("First/x").text, f.find("First/y").text = str(x), str(y)
    f.find("Second").text = "true"


def set_align(e, name, anchor):
    f = e.find(name)
    f.find("First").text = anchor
    f.find("Second").text = "true"


def put_placement(data, edit):
    m = PLACEMENT.search(data)
    if not m:
        raise ValueError("no <Placement>")
    e = ET.fromstring(m.group(0))
    edit(e)
    block = ET.tostring(e, encoding="utf-8")
    if block.startswith(b"<?xml"):
        block = block.split(b"?>", 1)[1].lstrip(b"\r\n")
    nl = b"\r\n" if b"\r\n" in data else b"\n"
    block = block.replace(b"\r\n", b"\n").replace(b"\n", nl)
    return data[:m.start()] + block + data[m.end():]


def place(data, spec):
    """The file changed as `spec` says."""
    if spec == "hide":
        if b"<Visible>true</Visible>" not in data:
            raise ValueError("no <Visible>true</Visible> to turn off")
        return data.replace(b"<Visible>true</Visible>", b"<Visible>false</Visible>", 1)
    kind = spec[0]
    if kind == "corner":
        # EPA_HIGH_END: the window's far edge `position` from the parent's far
        # edge; EPA_LOW_END: its near edge `position` from the near one.
        _, corner, dx, dy = spec
        right, bottom = corner[1] == "r", corner[0] == "b"

        def edit(e):
            set_align(e, "HorAllign", "EPA_HIGH_END" if right else "EPA_LOW_END")
            set_align(e, "VerAllign", "EPA_HIGH_END" if bottom else "EPA_LOW_END")
            set_pair(e, "Position", -dx if right else dx, -dy if bottom else dy)
        return put_placement(data, edit)
    if kind == "at":
        _, x, y = spec

        def edit(e):
            set_align(e, "HorAllign", "EPA_LOW_END")
            set_align(e, "VerAllign", "EPA_LOW_END")
            set_pair(e, "Position", x, y)
        return put_placement(data, edit)
    if kind == "center_x":
        # Centred across, at y below the resource bar (and a town's own bar
        # under it: the necropolis's dark energy).
        y = spec[1]

        def edit(e):
            set_align(e, "HorAllign", "ERA_CENTER")
            f = e.find("Position")
            f.find("First/x").text = "0"
            f.find("First/y").text = str(y)
            f.find("Second").text = "true"
        return put_placement(data, edit)
    if kind == "frame":
        _, w, h = spec
        out = re.sub(rb"<Background href=\"[^\"]*\"\s*/>", b"<Background/>", data, count=1)
        if out == data:
            raise ValueError("no <Background href=...> to take off")

        def edit(e):
            set_pair(e, "Size", w, h)
        return put_placement(out, edit)
    raise ValueError("unknown spec %r" % (spec,))


# What an expansion lays out otherwise: Hammers of Fate's battle has no wait
# button (the base game's comes from DataLS/p6, which it does not mount), so
# the spell book (or the special ability, or the start of the battle) takes
# its place beside defend, and flee the spell book's.
HOF_ABSENT = {"UI/CombatScreen-Heavy/OnJog-Wait.(WindowMSButton).xdb"}
HOF_COMBAT = {
    "OnJog-Spellbook.(WindowMSButton).xdb": ("corner", "br", row(0), MARGIN),
    "OnJog-Special.(WindowMSButton).xdb": ("corner", "br", row(0), MARGIN),
    "OnJog-StartCombat.(WindowMSButton).xdb": ("corner", "br", row(0), MARGIN),
    "OnJog-Flee.(WindowMSButton).xdb": ("corner", "br", row(1), MARGIN),
}


def screens_for(game):
    """SCREENS for `game` ("base", "hof", "toe") and the files it lacks."""
    if game != "hof":
        return SCREENS, set()
    screens = {folder: dict(windows) for folder, windows in SCREENS.items()}
    screens["UI/CombatScreen-Heavy/"].update(HOF_COMBAT)
    return screens, HOF_ABSENT


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--base-game", type=Path,
                   help="the game's install folder (the base game's for hof, Tribes of the East's for toe)")
    p.add_argument("--game", choices=("base", "hof", "toe"), default="base")
    p.add_argument("--widescreen", type=Path, help="the wide-screen mod (tools/widescreen_mod.py)")
    p.add_argument("--out", type=Path, help="the .h5u (or for hof the .pak) to write")
    p.add_argument("--data-layout", action="store_true",
                   help="move the windows in the mod itself (the layout before native/phone_layout.c)")
    p.add_argument("--c-table", type=Path, help="write native/phone_layout_table.h and stop")
    p.add_argument("--recipe", type=Path, help="also write the recipe (tools/ui_recipe.py) the app makes it from")
    args = p.parse_args()
    if args.c_table:
        c_table(args.c_table)
        return 0
    if not args.out:
        p.error("--out is required")
    if not args.data_layout:
        # Neutral: no window of the game's own changed, only a note.
        note = (b"The phone layout is native (native/phone_layout.c); "
                b"tools/phone_ui_mod.py --data-layout writes the mod that moved the windows.\n")
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
            info = zipfile.ZipInfo("PhoneUI.txt", date_time=ENTRY_DATE)
            z.writestr(info, note)
        if args.recipe:
            paks = {"base": BASE_PAKS, "hof": HOF_PAKS, "toe": TOE_PAKS}[args.game]
            ui_recipe.write(args.recipe, name="PhoneUI" + ("" if args.game == "base" else "-" + args.game),
                            out=RECIPE_OUT[args.game], date="%04d-%02d-%02d %02d:%02d:%02d" % ENTRY_DATE,
                            layers=paks, files={"PhoneUI.txt": note}, sources={}, authored={"PhoneUI.txt"},
                            comment="Made by tools/phone_ui_mod.py --game %s; applied by kit/host/ui_mods.cpp.\n"
                                    "Neutral: it replaces an older PhoneUI that moved the windows."
                                    % args.game)
        print("%s: neutral (the layout is native)" % args.out)
        return 0
    if not args.base_game:
        p.error("--base-game is required with --data-layout")

    paks = {"base": BASE_PAKS, "hof": HOF_PAKS, "toe": TOE_PAKS}[args.game]
    base = read_layers([args.base_game / name for name in paks])
    if not base:
        sys.exit("No UI files found: check --base-game")
    screens, absent = screens_for(args.game)
    wide = {}
    if args.widescreen and args.widescreen.is_file():
        with zipfile.ZipFile(args.widescreen) as z:
            wide = {n: z.read(n) for n in z.namelist()}

    written, problems, sources = {}, [], {}
    for folder, windows in screens.items():
        for file, spec in windows.items():
            name = folder.split("@")[0] + file  # "@..." only keeps two entries of one folder apart
            if name in absent:
                continue
            source = wide.get(name, base.get(name))
            if source is None:
                problems.append("%s: not in the game's data" % name)
                continue
            sources[name] = (name, source)
            try:
                written[name] = place(source, spec)
            except (ValueError, ET.ParseError, AttributeError) as err:
                problems.append("%s: %s" % (name, err))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(written.items()):
            info = zipfile.ZipInfo(name, date_time=ENTRY_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    if args.recipe:
        ui_recipe.write(args.recipe, name="PhoneUI" + ("" if args.game == "base" else "-" + args.game),
                        out=RECIPE_OUT[args.game], date="%04d-%02d-%02d %02d:%02d:%02d" % ENTRY_DATE,
                        layers=paks, files=written, sources=sources,
                        over=WIDE_OUT[args.game] if wide else None,
                        comment="Made by tools/phone_ui_mod.py --game %s; applied by kit/host/ui_mods.cpp."
                                % args.game)
    for line in problems:
        print("left out: " + line, file=sys.stderr)
    print("%s: %d files%s" % (args.out, len(written), ", %d left out" % len(problems) if problems else ""))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
