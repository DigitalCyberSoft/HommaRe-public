# Changelog

What changed for players in each release of HommaRe. The builds are in
[Releases](../../releases); the recompilation kit underneath keeps its own notes in
[kit/CHANGELOG.md](kit/CHANGELOG.md).

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
