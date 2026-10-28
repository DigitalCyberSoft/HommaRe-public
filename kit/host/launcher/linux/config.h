// config.h - the game repository's launcher-games.json and the install
// manifests in launcher-manifests/, as the native launcher on Linux reads
// them: the same files the SwiftUI launcher decodes (LauncherConfig in
// apple/LauncherModel.swift, InstallManifest in apple/LauncherInstaller.swift).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace launcher::native {

// Text in the languages the config carries: the launcher's language wins,
// then English, then whatever is there. A plain string is English.
struct LocalizedText {
    std::map<std::string, std::string> values;
    std::string text() const;
    bool empty() const {
        return values.empty();
    }
};

struct GameSpec {
    std::string id;
    // The game build that plays it: the app's own RECOMP_GAME_ID, or another
    // module that may not be in this app yet.
    std::string module;
    LocalizedText title, subtitle, blurb;
    std::string year, version;
    // goggame-<id>.info marks a GOG install of this game.
    std::string gog_id;
    // Where the executable sits under the install root, first match wins.
    std::vector<std::string> exe, sha256, required_dirs;
    // Splash images in the player's install, relative to the root, and the
    // part worth showing (x, y, width, height in 0..1).
    std::vector<std::string> art;
    std::vector<double> art_crop;
    // Accent colour as #RRGGBB.
    std::string accent;
};

// One GOG installer the app can install from ("installers").
struct InstallerSpec {
    std::string id;
    // What innoextract --gog-game-id reports for it.
    std::string gog_id;
    // The install folder under the launcher's Games folder.
    std::string folder;
    LocalizedText title;
    // Manifests in launcher-manifests/, one per build of the installer; the
    // one whose files the installer holds wins.
    std::vector<std::string> manifests;
    // The games it installs (GameSpec::id).
    std::vector<std::string> games;
};

struct Config {
    LocalizedText title, tagline, legal;
    // Folder names worth looking for under the usual places.
    std::vector<std::string> install_names;
    std::vector<std::string> exclude;
    std::vector<GameSpec> games;
    std::vector<InstallerSpec> installers;
};

// Every file an install keeps, with its size and SHA-256 (no game data).
struct ManifestFile {
    std::string path;
    int64_t size = 0;
    std::string sha256;
};

struct Manifest {
    std::string gog_id, installer;
    std::vector<ManifestFile> files;
};

// False (with a reason) when `text` is not a config with at least one game.
bool parse_config(const std::string &text, Config *out, std::string *error);
bool parse_manifest(const std::string &text, Manifest *out);
// The whole of a file; false when it cannot be read.
bool read_text(const std::string &path, std::string *out);

} // namespace launcher::native
