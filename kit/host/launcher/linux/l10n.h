// l10n.h - the native launcher's own words on Linux, in the languages the
// GOG installers come in: the same texts as the SwiftUI launcher's
// (apple/LauncherStrings.swift), read from apple/launcher-strings.json, which
// the build embeds. English stands in for a text a language lacks; a key
// with a ".linux" variant takes that one.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace launcher::native {

struct Language {
    std::string code, name;
};

// The languages the launcher speaks, in the catalog's order (English first).
const std::vector<Language> &languages();

// The launcher's language: RECOMP_LAUNCHER_LANG, then the player's choice in
// Settings, then the system's.
std::string language();
// The choice in Settings: a code of languages(), or "" to follow the system.
const std::string &chosen_language();
void choose_language(const std::string &code);

// The player's languages as (language, region) pairs, from the environment as
// gettext reads it: LANGUAGE's list first (unless the locale is C), then
// LC_ALL, LC_MESSAGES, LANG ("en_US.UTF-8" is English in the US).
std::vector<std::pair<std::string, std::string>> preferred_languages();
// The first of `preferred` (language, region) pairs the launcher speaks:
// zh in CN or SG is zh-Hans, other Chinese is passed over; English otherwise.
std::string pick_language(const std::vector<std::pair<std::string, std::string>> &preferred);

using Args = std::vector<std::pair<const char *, std::string>>;
// The text `key` names in the launcher's language, its {placeholders} filled.
std::string t(const char *key, const Args &args = {});

// A size in the launcher's language ("4.08 GB", "4,08 ГБ").
std::string bytes(int64_t count);
// Where a game lives, for people: the path with ~ for the home folder.
std::string location(const std::string &path);

// The texts whose arguments need shaping (LauncherStrings.swift).
std::string file_of(int done, int total);
std::string progress_of(int64_t done, int64_t total);
std::string interrupted_detail(const std::string &setup);
std::string wrong_version_detail(const std::string &digest);
std::string incomplete_detail(const std::vector<std::string> &missing);
std::string verify_failed(const std::vector<std::string> &paths);
std::string no_space(int64_t need, int64_t free);

} // namespace launcher::native
