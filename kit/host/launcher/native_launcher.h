// native_launcher.h - the SwiftUI launcher Apple builds draw over the SDL
// window (host/launcher/apple/*.swift) when the game repository ships a
// launcher-games.json. The C++ host keeps pumping SDL events while it is up;
// other targets, and games without the config, keep the canvas launcher.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Presents the launcher in `native_window` (NSWindow * or UIWindow *).
// `config_path` names launcher-games.json, `modules` the comma-separated game
// modules this app can run (RECOMP_GAME_ID first),
// `known_exe` the executable the host already resolved ("" when none) and
// `profile_dir` the folder saves live in. False when the config is unreadable.
int recomp_native_launcher_begin(void *native_window, const char *config_path, const char *modules,
                                 const char *known_exe, const char *profile_dir);
// 0 while the launcher is up, 1 once the player pressed Play, -1 when they quit.
int recomp_native_launcher_poll(void);
// The chosen executable and its install root (absolute paths) and game id;
// valid until recomp_native_launcher_end.
const char *recomp_native_launcher_exe(void);
const char *recomp_native_launcher_root(void);
const char *recomp_native_launcher_game(void);
// The module (RECOMP_GAME_ID of the build) that plays the chosen game.
const char *recomp_native_launcher_module(void);
// Takes the launcher down.
void recomp_native_launcher_end(void);

#ifdef __cplusplus
}

#include <string>
struct SDL_Window;
namespace launcher {
struct NativeChoice {
    std::string exe, root, game, module;
};
// True when this build has the native launcher and `config_path` exists.
bool native_available(const std::string &config_path);
// Runs the native launcher: 1 once the player picked a game (`out` filled),
// 0 when they closed it, -1 when it could not be shown (use the canvas one).
int native_run(SDL_Window *window, const std::string &config_path, const std::string &modules,
               const std::string &known_exe, const std::string &profile_dir, NativeChoice *out);
} // namespace launcher
#endif
