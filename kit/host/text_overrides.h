// text_overrides.h - a game's own texts with a few words changed (game.toml [texts]).
#pragma once
#include <string>

// Rebuilds `pak` (a path relative to the game, e.g. "data/zz-port-texts.pak")
// in the profile from the install's own copies of the listed files
// ("file;" records) with the replacements ("old|new;" records, UTF-8) made,
// keeping only the files the replacements changed. Writes only when the
// result differs, and removes the pak when nothing changes. Call before the
// game starts.
void host_text_overrides(const std::string &game_root, const std::string &profile_dir,
                         const char *pak, const char *file_list, const char *replace_list);
