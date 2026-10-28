// server_prompt.h - the ubi-gs server question: which community server a
// Ubisoft Game Service title's online mode should use (game.toml [online]).
//
// The game asks its gsconnect host for the addresses of its other servers
// when the player opens its online mode; runtime/curl_http.cpp holds that
// request and asks this prompt. It is a page over the game in the settings
// menu's place and look (page_overlay.h): a short explanation from game.toml,
// a field for the server's IP address or host name with an optional :port,
// the servers that answered before (most recent first, the first one already
// in the field), and OK and Cancel. Enter is OK, Escape is Cancel, Up and
// Down walk the list, Ctrl/Cmd+V pastes; a click or a tap works everything.
// A server that does not answer brings the prompt back with the reason.
//
// Every server that answered is kept in the profile, in ubi-gs-servers.txt:
// one per line, most recent first, at most ten, comments after '#'. A line
// that is not a server address is skipped, so a damaged file costs at most
// the entries it damaged.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The file in `profile_dir` (ubi-gs-servers.txt).
std::string server_list_path(const std::string &profile_dir);
// The servers in that file, canonical, without duplicates, at most `cap`. A
// missing or unreadable file is an empty list.
std::vector<std::string> server_list_load(const std::string &path, size_t cap = 10);
// `server` to the front, removing it from further down, then cut to `cap`.
void server_list_remember(std::vector<std::string> *list, const std::string &server,
                          size_t cap = 10);
// Writes the list (through a temporary file, so a crash leaves the old one).
bool server_list_save(const std::string &path, const std::vector<std::string> &list);

// Makes this host the one that asks (curl_http_set_chooser) and shows the
// prompt (host_page_set_prompt). `title` and `text` are game.toml [online]'s.
// Call before the guest starts.
void server_prompt_install(const std::string &profile_dir, const char *title, const char *text);

// The prompt's model, apart from the runtime and the presenter, for tests:
// what curl_http's chooser and the page's hooks call.
namespace server_prompt {
bool begin(const char *error);
int poll(char *buf, size_t cap);
void end();
void used(const char *server);
bool is_open();
bool busy();
bool key(uint8_t dik, bool down);
void text(const char *utf8);
void pointer(float u, float v, int button, int down, int wheel);
int render(int w, int h, uint64_t *generation, const uint8_t **pixels);
void cancel();
// The field's text, the selected list entry (-1 none), the shown error.
std::string field();
int selected();
std::string error();
std::vector<std::string> servers();
} // namespace server_prompt
