<!-- Describes the state of branch `integrate` (all branches merged, 2026-10-19). -->

<p align="right"><b>English</b> · <a href="README.ru.md">Русский</a></p>

<h1 align="center">HommaRe</h1>

<p align="center">
An unofficial static recompilation of <b>Heroes of Might and Magic V</b> (2006), with
<b>Hammers of Fate</b> and <b>Tribes of the East</b>: a native port, not an emulator.<br>
Play your own GOG copy on Apple silicon Macs, iPad and iPhone.
</p>

<p align="center">
<a href="#download">Download</a> ·
<a href="#quick-start">Quick start</a> ·
<a href="#controls">Controls</a> ·
<a href="#faq-and-troubleshooting">FAQ</a> ·
<a href="../../issues">Report a bug</a>
</p>

<!-- Screenshot: the adventure map on a 21:9 ultrawide (Hor+ view, HUD at the edges). -->

> [!IMPORTANT]
> HommaRe is an unofficial fan project. It is not affiliated with, endorsed by or
> sponsored by Ubisoft, Nival or GOG.
>
> **No game files are included or distributed.** To play, you need your own legally
> obtained copy of the game from GOG. The launcher installs it from GOG's own offline
> installers and verifies every file. Please don't ask for, or share, game files.
> See the full [legal notice](#legal-notice).

**Contents:** [What it is](#what-is-hommare) · [Highlights](#highlights) ·
[Status](#status) · [Download](#download) · [Quick start](#quick-start) ·
[Controls](#controls) · [Settings](#settings) · [FAQ](#faq-and-troubleshooting) ·
[What's next](#whats-next) · [For developers](#for-developers) · [Credits](#credits) ·
[License](#license) · [Legal notice](#legal-notice)

## What is HommaRe?

HommaRe is a static recompilation of the Windows version of Heroes of Might and
Magic V: the game's own x86 code, translated ahead of time into C and compiled into an
arm64 app. There is no Wine, no CrossOver, no Rosetta and no emulator at run time: the
game runs as an ordinary Mac, iPad or iPhone app.

All three games are in one app: the original game (1.60), **Hammers of Fate** (2.1) and
**Tribes of the East** (3.1). You install them from your own GOG copy, pick one in the
launcher and play, with things the 2006 game couldn't do: ultrawide screens, touch and
trackpad controls, an interface made for the iPhone, and a frame rate that saves your
battery when nothing moves.

## Highlights

- **Native on Apple silicon.** The game's code, and RAD's Granny 2 animation library
  with it, are recompiled for arm64. Direct3D 9 is drawn with Metal.
- **Three games, one app.** Heroes V, Hammers of Fate and Tribes of the East, each with
  its own profiles and saves. The launcher shows what is installed and starts the one you
  pick.
- **A launcher that looks like the game.** Its frames, buttons and backdrop are read from
  your installed game's own data at run time, so the app ships no game art. In English and
  Russian, and readable on an iPhone.
- **Installs from your GOG installers.** On a Mac, give the launcher GOG's offline
  installer: the `.exe`, the `.bin`, the folder they are in, or several at once, or drop
  them onto the window. It unpacks only what the game needs, checks every file's SHA-256
  and can resume an interrupted install. A game you already installed is found too.
- **Ultrawide done right.** On 21:9 the adventure map and battles get a wider view instead
  of a stretched one (Hor+), and the interface moves to the screen edges. *Interface
  bounds* keeps the HUD in a 21:9, 16:9 or 4:3 box on very wide screens; screens,
  backgrounds and the 3D world still fill the whole display. In all three games.
- **Touch controls built for Heroes.** Heroes V explains everything on *hover*, so a
  finger at rest acts as a hovering mouse: hold to read a tooltip, tap to act. A second
  finger gives the right click; drag to pan, pinch to zoom, twist to turn the camera.
  The system keyboard comes up on a text field, and a drag scrolls a list.
- **An interface for the iPhone.** The round dial menus become rows of buttons along the
  screen edges, panels are larger, the hero list sits beside the minimap, the town's
  build and hire menus move to the corner, and everything stays clear of the Dynamic
  Island. The battle camera comes nearer, and glides back while you place your troops.
- **Trackpad on a Mac.** Two fingers over the adventure map raise and lower the camera,
  or move it like on an iPad: scroll to pan, pinch to zoom, twist to turn.
- **Smooth, and kind to the battery.** 60 fps on the adventure map on an M1 Max. With
  nothing happening, the frame rate drops to 30 until you touch the mouse, keyboard or
  screen. In Low Power Mode, when the device is hot, or on battery on iPad, an energy mode
  lowers the frame limit and the render scale. A hidden window runs at 10 fps.
- **Sharper picture.** Optional MetalFX upscaling, MSAA and anisotropic filtering, with
  shaders compiled ahead of time and kept between runs to avoid stutter.

## Status

This is an early, active project. What works today:

| | Heroes V 1.60 | Hammers of Fate 2.1 | Tribes of the East 3.1 |
|---|---|---|---|
| Mac (Apple silicon) | Playable: campaigns, scenarios, hot-seat, battles, towns, saves | Main menu and scenario maps | Main menu and scenario maps |
| iPad | Playable with touch | Main menu and maps | Main menu and maps |
| iPhone | Playable with the phone interface | Phone interface: new, not yet tried on a device | Phone interface: new, not yet tried on a device |
| Ultrawide (21:9) | Hor+ view, HUD at the edges, interface bounds | HUD at the edges, interface bounds; wide 3D view in progress | Hor+ view, HUD at the edges, interface bounds |
| Mac trackpad | Map; battle zoom | Map | Map |

Tested on a MacBook Pro (M1 Max, macOS 27) with a 3440x1440 ultrawide, an iPad Pro 13"
(M5) and an iPhone 15 Pro Max (iOS 27), with the **Russian GOG releases** of the game.
Other languages haven't been tested yet: reports welcome.

## Download

| Platform | How to get it |
|---|---|
| Mac (Apple silicon) | Coming soon. Until then, [build it yourself](#building-from-source). |
| iPad and iPhone | Coming soon (not on the App Store). Build it yourself with Xcode. |
| Windows, Linux, Android | Not planned for now: the original game already runs on Windows. |

### System requirements

- **Everyone:** your own copy of Heroes of Might and Magic V from GOG: *Heroes of Might
  and Magic V* (with Hammers of Fate) and/or *Heroes of Might and Magic V: Tribes of the
  East*. About 4 GB of free space for Heroes V with Hammers of Fate, and 2.2 GB more for
  Tribes of the East.
- **Mac:** Apple silicon. There is no Intel Mac build.
- **iPad and iPhone:** an M-series iPad or a recent iPhone (tested on iPhone 15 Pro Max).
  The app asks iOS for more memory than most apps. The game plays in landscape.

## Quick start

### Mac

1. **Download your installers from GOG.** In your GOG library, open *Heroes of Might and
   Magic V* (and *Tribes of the East*, if you own it) and download the offline backup
   installers: one `.exe` and one or more `.bin` files for each.
2. **Open HommaRe.** On first run the launcher has nothing to play and asks for an
   installer. Choose the `.exe`, the `.bin` or the folder they are in, or drop them onto
   the window. A file renamed by the browser is recognised, and the launcher says how to
   name it.
3. **Install.** The launcher unpacks the game, checks every file and marks it ready.
   One GOG installer gives you both the original game and Hammers of Fate. If the game is
   already installed somewhere, choose *Game folder…* instead.
4. **Play.** Pick a game in the launcher and press *Play*. Your profiles, saves and
   settings are kept apart from the game files, so they survive updates and reinstalls.

### iPad and iPhone

1. **Install the game on your Mac first** (above), or use an existing GOG installation.
2. **Copy the game folder to the device** with Finder or the Files app.
3. **Open HommaRe** and tap *Add game files…*, then choose that folder. The launcher
   copies it into the app and checks it. Afterwards you can delete the copied folder.

> [!TIP]
> In *Single Player*, the Campaign, Scenarios and Hall of Fame items stay grey until a
> profile is chosen: open *Change Profile*, pick yours in the list and press *Select*. This
> is the original game's behaviour, not a bug of the port.

## Controls

### Mac

Mouse and keyboard work as in the original game: hover for information, left click to
act, right click to see details, the mouse wheel and the game's own keys for the camera.
Double clicks, Shift-drop to split a stack and Alt for map labels all work.

With a trackpad or Magic Mouse, two fingers over the 3D adventure map move the camera
(*Settings → Trackpad*):

| Mode | Two fingers over the map |
|---|---|
| Height (default) | Scroll up and down raises and lowers the camera, with momentum |
| Like an iPad | Scroll pans, pinch zooms, twist turns the camera |
| Game's own | Scrolling goes to the game, as a mouse wheel |

Over the interface, in lists and dialogs, scrolling always goes to the game. In a battle
of the original game, the gesture zooms the battle camera.

### iPad and iPhone

| Gesture | What it does |
|---|---|
| Touch and hold | Hover: shows the game's tooltips and highlights, without clicking |
| Tap | Left click (the cursor jumps to your finger first) |
| Double tap | Double click |
| Second finger tap, beside the resting one | Right click on the thing under the first finger |
| Two-finger tap | Right click between the fingers |
| One-finger drag on the map | Pan the camera |
| Drag up or down over a list | Scroll the list |
| Tap a text field | Bring up the keyboard |
| Pinch | Zoom |
| Two-finger twist | Turn the camera |

On a loading screen, tap once more when loading is done to start: the game waits for a
click there. The three intro videos can each be skipped with a tap, or turned off in the
settings.

## Settings

The launcher's *Settings* apply to every game:

| Setting | Choices | What it does |
|---|---|---|
| Language | As in the system, English, Russian | The launcher's language |
| Frame limit | Game's own, or a number | The highest frame rate the game runs at |
| Energy saving | Auto, Always, Never | Auto turns it on in Low Power Mode, when the device is hot, or on battery on iPad: a lower frame limit and render scale |
| 30 fps after 10 s idle | Auto, On, Off | Drops to 30 fps after 10 s without input, back at once on any input |
| Interface bounds | Screen edges, 21:9, 16:9, 4:3 | On wide screens, keeps the HUD in a box instead of at the far edges |
| Startup videos | Show, Skip | The company logos and intro at start |
| Trackpad *(Mac)* | Height, Like an iPad, Game's own | What two fingers do over the map |
| Camera buttons *(iPad, iPhone)* | Show, Hide | The on-screen camera panel |
| Phone interface *(iPhone)* | On, Off | Larger panels and button rows instead of dial menus; safe areas and the battle camera stay |
| Silent mode *(iPad, iPhone)* | Respect, Ignore | Whether the silent switch mutes the game |
| Game folders | | Extra places where the launcher looks for installed games |

The game's own Options menu (resolution, graphics quality, sound) works as in the
original. On iPad and iPhone, a new profile starts at the device's own screen shape.

## FAQ and troubleshooting

**Does HommaRe include the game?** No. No game files are included or distributed.
You install the game from your own GOG copy, and every file is checked.

**Which version do I need?** The GOG releases: *Heroes of Might and Magic V* 2.1 (which
contains the original game 1.60 and Hammers of Fate 2.1) and *Tribes of the East* 3.1.
The launcher checks each game's executable by its SHA-256 and says if it doesn't match.
Disc versions and other stores haven't been tested.

**Is this an emulator? Does it use Wine?** No. It's a static recompilation: the game's
x86 code was translated ahead of time into C and compiled for arm64. A runtime written for
this port stands in for the parts of Windows the game uses.

**Why can't the iPad install from the GOG installer?** Unpacking GOG installers needs a
helper program that iOS doesn't allow apps to run. Install on a Mac and copy the game
folder over; installing on the device itself is planned.

**Can I bring my Windows saves and profiles?** Profiles and saves use the game's own
formats. Copy them into the profile folder (see below); this hasn't been tested much yet.

**Where are my profiles, saves and settings?**

| | Mac | iPad and iPhone |
|---|---|---|
| Profiles and saves | `~/Library/Application Support/HommaRe/Documents/My Games/Heroes of Might and Magic V/` (the launcher's *Saves* button opens it) | Inside the app |
| Installed games | Where you chose in the launcher | Inside the app (`Games` in the Files app) |

**Hot-seat saves don't show in Load Game.** Hot-seat games are loaded from *Multiplayer →
Hot Seat → Load*, not from the single-player list, as in the original.

**My new profile isn't remembered.** The game writes its profile choice when it quits
normally. Leave with *Exit → OK* once after creating a profile.

**The game is silent on my iPhone.** The silent switch is on. Turn it off, or set
*Settings → Silent mode* to *Ignore*.

**Is multiplayer supported?** Hot-seat on one device, yes. Network play isn't.

## What's next

- Signed, downloadable builds for Mac, and an IPA for iPad and iPhone.
- Installing from GOG installers on the iPad and iPhone themselves.
- The phone interface of Hammers of Fate and Tribes of the East, tried on a real iPhone.
- The wide 3D view for Hammers of Fate.
- More speed: native versions of the game's hottest particle code, and profile-guided
  optimization.

There are no dates and no promises: it's ready when it's ready.

## For developers

### How it works

HommaRe is built with **recomp-kit**, a static recompilation kit for 32-bit x86
Windows games.

1. **Analysis.** Ghidra finds the functions of `H5_Game.exe` (about 70,000) and of
   `granny2.dll`.
2. **Translation.** The kit's translator decodes each function with Capstone and writes
   C: x86 registers, flags, x87 and SSE as plain C. The eight registers live in locals
   inside each function, and flags are followed through the function's jumps and computed
   only where they are read.
3. **Compilation.** Clang compiles the C (about 100,000 functions) together with the
   runtime into one arm64 app. Each expansion becomes a module (`libgame.<id>.dylib`)
   that the app loads when you pick it in the launcher.
4. **Runtime.** In place of Windows, the runtime provides what the game calls:
   - `kernel32`, `user32` and friends: files, memory-mapped files, threads, messages,
     code pages (Windows-1251 for the Russian release);
   - the Microsoft C and C++ runtime (UCRT, VCRUNTIME140, MSVCP140);
   - **Direct3D 9 on Metal**: the fixed-function pipeline, `ps_1_x` / `vs_1_1` shaders
     translated to Metal Shading Language and kept in a Metal binary archive, DXT
     textures (decoded on the CPU where the GPU lacks BC formats), MetalFX upscaling;
   - DirectInput for mouse and keyboard, and SDL3 for windows, input and audio;
   - an **FMOD** stand-in that plays the game's sounds and music through the kit's mixer.
5. **Native code.** Hand-written C in `native/` hooks a few of the game's functions,
   with each game's addresses in `games/*/native/addresses.h`:
   - `touch.c`, `camera.c`: touch and trackpad input, the camera gestures, Hor+;
   - `widescreen.c`, `ui_bounds.c`, `hof_screen.c`: the wide virtual screen, edge
     anchors and interface bounds (Hammers of Fate has no layout code of its own for
     them);
   - `phone_hud.c`: the iPhone layer: safe areas, scaled panels, corner menus;
   - `save.cpp`: saving settings when the app goes to the background.
6. **Data mods.** Interface changes that need no code are the game's own mod packages,
   generated at build time from the player's own game data and placed in the profile:
   `Widescreen.h5u` (edge anchors from Tribes of the East) and `PhoneUI.h5u` (button rows
   for the iPhone). Hammers of Fate reads no `UserMODs`, so it gets the same as `zz-*.pak`
   files mounted above its own data. The host rebuilds `zz-port-texts.pak` at every start
   from the install's own files (game.toml `[texts]`): "Exit to Windows" reworded and the
   buttons' click at a quarter of its volume, in all three games on every platform.

`granny2.dll` is translated at its own base address `0x50000000` and never rebased: the
game's `.gr2` model files carry raw pointers into it.

The game's data (maps, models, textures, sounds, videos) comes from the player's
installation. HommaRe contains no game data or assets.

### Building from source

The repository contains no game code: the build translates **your own** executables. The
generated code, analysis and game files are ignored by git. Never commit or share them.

Supported executables:

| Game | File | SHA-256 |
|---|---|---|
| Heroes V 1.60 | `bin/H5_Game.exe` | `820786b0f85c62ca96a1a5b18d03d60185047ae8dab09ef6f0b299e3abcfba7c` |
| Hammers of Fate 2.1 | `bina1/H5_Game.exe` | `c15cc3c68a5bd1f3111d7d2fda7ef7607dbe96d1aedadaf463b50cbe41b40730` |
| Tribes of the East 3.1 | `bin/H5_Game.exe` | `3eb6fce81071f48631360202d81d41c67b09d8b1807105b1f22c402e15d16268` |

**1. Dependencies.** Xcode, CMake, Ninja, Python 3 with the kit's requirements
(`capstone`, `pefile`, `numpy`, `Pillow`), and Ghidra with a JDK for the analysis.

**2. Link your game and analyse it.**

```bash
python3 -m venv .venv && .venv/bin/pip install -r kit/requirements-dev.txt
tools/setup.py --install "/path/to/Heroes of Might and Magic V" --link-only
tools/analyze.py
```

For the expansions, do the same in `games/hof` and `games/toe` (`games/run_analysis.py`).

**3. Build.** All three games in one app, for the Mac (`app`) or for iPad and iPhone
(`ios`):

```bash
for g in games/hof games/toe .; do
  .venv/bin/python kit/tools/build.py --game-dir $g --target ios --game-modules --no-install
done
.venv/bin/python kit/tools/assemble_games.py --app build/ios/Release/HommaRe.app \
  --game games/hof --game games/toe --platform ios
```

Set `RECOMP_IOS_TEAM` to your Apple developer team for a device build. Translation takes
about 10–15 minutes per game and compilation about 25 minutes per game and platform on an
M1 Max; later builds are incremental. Use `--regenerate` after changing the translator.

### Useful switches

| Variable | What it does |
|---|---|
| `RECOMP_LAUNCHER=0` | Start the game directly, without the launcher |
| `RECOMP_ADAPTIVE_FPS=1/0` | Force the 30 fps idle mode on or off |
| `RECOMP_ENERGY=on/off` | Force the energy mode |
| `RECOMP_D3D9_MAX_SCALE` | Cap the render scale |
| `RECOMP_HOMM5_UI_BOUNDS=0..3` | Interface bounds: screen edges, 21:9, 16:9, 4:3 |
| `RECOMP_TRACKPAD_CAMERA=0..2` | Trackpad mode: height, like an iPad, game's own |
| `RECOMP_AUTO_KEYS`, `RECOMP_MAX_SECONDS` | Scripted, unattended runs |
| `RECOMP_TRACE_FUNCS` | Trace calls to given game functions |

Design notes, with addresses, are in `docs/`: `expansions.md`, `trackpad-camera.md`,
`gog-installer.md`, `launcher-skin.md`, `ui-mods.md`, `performance-energy.md`.

## Credits

HommaRe stands on a lot of open-source work. Thank you to everyone behind:

| Project | What it's used for | License |
|---|---|---|
| recomp-kit (veritr1x) | The translator, runtime and host | MIT |
| Ghidra (NSA) | Finding the game's functions | Apache-2.0 |
| Capstone | x86 decoding in the translator | BSD-3-Clause |
| SDL3 | Windows, input, audio, app lifecycle | zlib |
| innoextract (Daniel Scharrer) | Unpacking GOG installers in the launcher | zlib |
| FFmpeg | Video playback | LGPL-2.1-or-later |
| Lua 5.4 | The kit's mod foundation | MIT |
| Unicorn | Checking translated instructions in tests | GPL-2.0 (development only) |

Full license texts are in `kit/NOTICE` and `kit/third_party/`.

## License

The license of the port's own code is not chosen yet. recomp-kit is under the MIT
License (`kit/LICENSE`). Third-party components keep their own licenses, listed above.

## Legal notice

HommaRe is an unofficial, fan-made project. It is not affiliated with, endorsed by
or sponsored by Ubisoft Entertainment, Nival or GOG.

"Heroes of Might and Magic", "Hammers of Fate" and "Tribes of the East" are trademarks of
Ubisoft Entertainment. Apple, Mac, macOS, iPad, iPhone, Metal and MetalFX are trademarks
of Apple Inc. All other trademarks belong to their respective owners. They are used here
only to say what HommaRe works with. The game content visible in screenshots belongs
to its owners.

**No game files are included or distributed.** HommaRe contains no game data or
assets: no maps, models, textures, music, sounds or videos. To play, you need your own
legally obtained copy of the game. The launcher verifies it and installs it.

HommaRe is provided "as is", without warranty of any kind.
