# Changelog

What changed for players in each release of HommaRe. The builds are in
[Releases](../../releases); the recompilation kit underneath keeps its own notes in
[kit/CHANGELOG.md](kit/CHANGELOG.md).

## 0.1-playable-344 (2026-10-29)

- **Install from GOG.com.** *Install from GOG…* opens a GOG.com panel: sign in on GOG's
  own page, and the app downloads the games you own straight onto the device, in English
  or Russian, and checks every file. No installer files, no Files app, no copying
  gigabytes to the phone. A stopped download resumes where it stopped, also after the app
  was closed. Installer files still work: *Use installer files…* in the same panel.
- **Hammers of Fate with the English game on iPhone and iPad:** full screen instead of
  4:3, and *Options* works (the English release lacks the settings file the expansion
  reads).
- **Hammers of Fate, native menus:** *Options* in the main menu opens again instead of
  leaving an empty screen.
- **Online play through ubi-gs.** *Multi Player → Ubi.com* asks for a community server
  standing in for Ubisoft's closed one (it suggests `ubigs.wayofharm.com`), logs in,
  joins the lobby's channels, and creates and joins games there. On iPhone its login,
  CD key, channels and lobby are native screens, in all three games. The server's
  operator sees the CD key and password you type: any CD key works, so don't use your
  real one.
- **Linux:** the three games build, run and package on x86-64 Linux, with Vulkan and the
  native launcher. Build it yourself for now.

## 0.1-playable-286 (2026-10-28)

- **Local network games.** *Multiplayer → Local Network* works: copies of the game on
  one network find each other's games, join them and play. Checked with a Mac hosting
  and an iPad joining a match; two copies on one Mac see each other too. On iPad and
  iPhone the app asks once for access to the local network.
- **Native menus on iPhone.** The main menu, profiles, campaigns, Load and Save, Options,
  duels, custom and hot-seat games, the in-game menu and the game's questions are drawn
  as native screens, built from the game's own pictures, words and frames. On by default
  on iPhone; on iPad, choose *Menus → For the screen* in the launcher's settings.
- **Foldable iPhone:** the game no longer falls back to 1024x768 after a screen lock or
  an app switch.
- **For developers:** development now happens in this repository, through pull requests
  that CI checks. `pip install -r kit/requirements-dev.txt` works again, on Python 3.12
  to 3.14.

## 0.1-playable-247 (2026-10-26)

- Battle stack counters stay outside the interface bounds on wide screens.
- The launcher's settings fit the window.
- iPad and iPhone: an unsigned IPA for sideloading, added to the release on 2026-10-27
  (its version reads 249).

## 0.1-playable-243 (2026-10-26)

- The first ready-made Mac build: Heroes V, Hammers of Fate and Tribes of the East in one
  app, installed from your own GOG offline installers.
