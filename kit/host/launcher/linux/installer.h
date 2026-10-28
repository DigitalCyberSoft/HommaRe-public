// installer.h - installing a game from the player's GOG offline installer
// (setup_*.exe and its *-N.bin, Inno Setup) on Linux, as the Mac does
// (apple/LauncherInstaller.swift): innoextract, the system's (or
// RECOMP_INNOEXTRACT), unpacks only the files the install manifest lists into
// the launcher's Games folder; every file is checked against the manifest's
// SHA-256 before the game counts as installed. An interrupted install keeps
// what it unpacked and resumes. A GOG update (patch_*.exe) brings the
// manifest's files it carries.
#pragma once
#include "config.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace launcher::native {

// An install or a GOG update under way, for the progress dialog.
struct Progress {
    enum Phase { Copying, Checking, Extracting, Verifying, Patching };
    std::string name;
    Phase phase = Copying;
    int files_done = 0, files_total = 0;
    int64_t bytes_done = 0, bytes_total = 0;
    std::string current;
    double fraction() const {
        return bytes_total > 0 ? std::min(1.0, double(bytes_done) / double(bytes_total)) : 0;
    }
    bool operator==(const Progress &) const = default;
};
// Called as work goes on; false cancels it.
using ProgressFn = std::function<bool(const Progress &)>;

// How a step ended: done, cancelled, or failed for the reason given (in the
// player's words).
struct Outcome {
    bool ok = true, cancelled = false;
    std::string error;
    static Outcome fail(std::string why) {
        return {false, false, std::move(why)};
    }
    static Outcome cancel() {
        return {false, true, ""};
    }
};

// Sits in an install folder until every file checked out: the launcher shows
// its games as an unfinished install and resumes from it.
struct InstallMarker {
    static constexpr const char *kName = ".recomp-install.json";
    std::string installer, setup, manifest;
    std::vector<std::string> games;
    // The innoextract that was unpacking into the folder.
    int64_t helper_pid = 0;
    static bool read(const std::string &root, InstallMarker *out);
    bool write(const std::string &root) const;
};

// Finds GOG offline installers in what the player picked or dropped: an
// installer's .exe, any of its .bin slices, or folders holding them (up to
// two levels down). Each setup_x.exe needs setup_x-1.bin beside it; a slice a
// browser renamed on download ("setup_x-1 (1).bin") is reported with a hint.
namespace finder {

struct Problem {
    // MissingSlice: `file` (an .exe) has no `expected` slice beside it.
    // MissingSetup: `file` (a .bin) has no `expected` installer beside it.
    // `renamed` is the file that looks like the missing one ("" if none).
    enum Kind { MissingSlice, MissingSetup } kind = MissingSlice;
    std::string file, expected, renamed;
    bool operator==(const Problem &) const = default;
};

struct Found {
    std::vector<std::string> setups;  // the installers' .exe files, in name order
    std::vector<std::string> patches; // GOG updates, in name order: after the installers
    std::vector<Problem> problems;
    std::vector<std::string> empty_folders; // folders with no installer, files that are none
};

// "setup_x-1.bin" -> ("setup_x", 1); false for other names.
bool slice(const std::string &name, std::string *base, int *index);
// A name with the copy counters browsers and file managers add taken out.
std::string normalized(const std::string &stem);
bool is_setup_name(const std::string &name);
bool is_patch_name(const std::string &name);
// Whether `exe` has its first slice beside it (false), or what is wrong.
bool check_slice(const std::string &exe, Problem *problem);
Found find(const std::vector<std::string> &paths);
// The player's words for `problem`.
std::string describe(const Problem &problem);

} // namespace finder

namespace gog {

struct Plan {
    InstallerSpec installer;
    std::string manifest_name;
    Manifest manifest;
    std::string setup, dest;
    // GOG updates found with the installer: an older build of it unpacks
    // files the manifest (the newest build) does not have, which they bring.
    std::vector<std::string> patches;
};

// What a GOG update did to one install.
struct PatchResult {
    std::string title;
    int replaced = 0;
};

// The innoextract to run: RECOMP_INNOEXTRACT, one beside the app, or the
// first on PATH; "" when there is none.
std::string helper();
// Stops the helper when the launcher goes away mid-install.
void stop_running();
// The paths a listing names (` - "bin/x.exe" [ru-RU]`), lower-cased.
std::set<std::string> listed_paths(const std::string &listing);
// The bytes of a zlib stream; false when `data` is none.
bool inflate(const std::string &data, std::string *out);

// Which installer `setup` is, which manifest describes it and where it goes
// (under `base`); a reason for the player when it is none the app knows.
Outcome plan(const std::string &setup, const Config &config, const std::string &manifest_dir,
             const std::string &base, Plan *out);
// Unpacks and checks the install `plan` describes. Files already there and
// intact are kept, so a cancelled or interrupted run resumes.
Outcome run(const Plan &plan, const ProgressFn &progress);
// Applies `patch` to the installs in `base` whose manifests know a file it
// carries.
Outcome apply_patch(const std::string &patch, const Config &config, const std::string &manifest_dir,
                    const std::string &base, const ProgressFn &progress,
                    std::vector<PatchResult> *results);

} // namespace gog

} // namespace launcher::native
