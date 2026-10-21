// layout.h - where this executable's resources and the player's profile live:
// a macOS bundle's Contents/Resources, a resources/ directory beside the
// executable (the Windows and Linux archives), or a developer checkout found
// above the executable. Computed once from os_exe_path(), except that
// RECOMP_RESOURCES_DIR (like RECOMP_PROFILE_DIR for the profile) wins over
// the computed resources directory whenever it is set: the Android host
// names its app data folder there, having no executable path to go by.
#pragma once
#include <string>

struct HostLayout {
    std::string resources_dir; // "" when nothing was found
    std::string profile_dir;   // settings, saves, game-path.txt
    std::string checkout_root; // "" outside a checkout
    // A developer run's outputs: the executable's own recomp/ directory when
    // it runs from one (a second build root, such as a cross build's), else
    // <checkout>/build/recomp.
    std::string build_dir;
    bool developer = false; // checkout_root is set
};
const HostLayout &host_layout();
// resources_dir + "/" + rel, with the developer mapping for the names
// "mods/core", "texture-pack", "classic-modes.json", "general-midi.sf2",
// "symbols.json" and "controls" (the game's on-screen controls layouts: a
// game repository's layouts/); "" when unknown.
std::string host_resource(const char *rel);
// A file the host writes during a run (the emulated registry, the run
// record): <build_dir>/<name> in developer mode, else
// <profile_dir>/<name>.
std::string host_state_file(const char *name);
// Test seam: recompute from this executable path and the current environment
// (nullptr restores the real path). Not thread-safe; tests only.
void host_layout_set_exe_path_for_test(const char *exe_path);

// A renamed app moves the user-data folder (os_user_data_dir) of the first
// former name in `previous` (null-terminated) that has one to `app`'s, with
// one rename: profiles, saves, settings and whatever the launcher keeps there
// go with it. Nothing is copied or deleted, and when `app` has a folder
// already both are left alone. 1 when a folder moved, 0 when nothing was to
// be done, -1 when the move failed; *note says what happened, if anything.
int host_migrate_user_data(const char *app, const char *const *previous, std::string *note);
// The same for this app (RECOMP_APP_NAME, game.toml previous_app_names),
// before anything reads the profile, and logged. Skipped when
// RECOMP_PROFILE_DIR names the profile and in a developer run, whose profile
// is its build's; RECOMP_MIGRATE_USER_DATA=0 skips it, =1 forces it.
void host_migrate_own_user_data();
