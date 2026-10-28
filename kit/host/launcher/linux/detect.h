// detect.h - which of the configured games a folder holds and in what state
// (Detector in apple/LauncherModel.swift): the executable found without case,
// its SHA-256 against the supported builds, the required folders, an
// unfinished install's marker. Runs off the main thread.
#pragma once
#include "../launcher_canvas.h"
#include "config.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace launcher::native {

struct GameState {
    enum Kind { NotFound, Ready, WrongVersion, Incomplete, Interrupted } kind = NotFound;
    std::string digest;               // WrongVersion: the executable's SHA-256
    std::vector<std::string> missing; // Incomplete: the required folders not there
    std::string setup;                // Interrupted: the installer it stopped installing from
    bool operator==(const GameState &o) const {
        return kind == o.kind && digest == o.digest && missing == o.missing && setup == o.setup;
    }
};

struct Found {
    GameState state;
    std::string root, exe;
    std::shared_ptr<const Image> art;
};

// The folders in `dir` (no hidden ones), by name.
std::vector<std::string> children(const std::string &dir);
bool is_dir(const std::string &path);
bool exists(const std::string &path);
// goggame-*.info: a GOG install's folder.
bool has_gog_info(const std::string &dir);
// `relative` under `dir`, each part matched without case (a copy made on
// Windows or by an installer may spell BIN or Data); "" when it is not there.
std::string entry(const std::string &dir, const std::string &relative);
// SHA-256 of a file, remembered by path, size and modification time.
std::string sha256_cached(const std::string &path);

// How well `root` holds `spec`; false when its executable is not there.
bool probe(const GameSpec &spec, const std::string &root, Found *out);
// Each game's best copy in `roots`; an unfinished install's games wait for it.
std::map<std::string, Found> scan(const std::vector<GameSpec> &specs,
                                  const std::vector<std::string> &roots);
// Install roots at `dir` or up to two levels below it that hold any game.
std::vector<std::string> roots_below(const std::string &dir, const std::vector<GameSpec> &specs);

// The path with "." and ".." and repeated slashes taken out, symlinks resolved
// where it exists (remembered folders are compared this way).
std::string canonical(const std::string &path);
std::string parent_dir(const std::string &path);
std::string file_name(const std::string &path);

} // namespace launcher::native
