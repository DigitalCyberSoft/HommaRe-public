// server_prompt.cpp - see server_prompt.h.
//
// THREADS. Several parties touch the prompt, and none of them may wait on
// another: the guest thread that asked (curl_http polls it, sleeping in
// between with the baton handed on), whoever draws the page (the thread that
// seals a frame, or the presenter's worker when the asking thread is the one
// that presents), whichever thread applies input (keys and the pointer, under
// the baton but on any guest thread, or the scheduler's own drain while the
// run thread idles) and the window's thread (typed and pasted text). So
// everything is one small state under one mutex, held only for a change or a
// draw; the guest's question and the player's answer meet in it.
#include "server_prompt.h"
#include "page_overlay.h"
#include "../runtime/curl_http.h"
#include "../runtime/gdi32_canvas.h"
#include "../platform/os.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

using namespace gdi_canvas;

// The settings menu's palette (mods/settings_menu_draw.cpp), so the two pages
// read as one interface.
const Rgba kDim = {0, 0, 0, 150};
const Rgba kPanel = {18, 20, 24, 238};
const Rgba kEdge = {64, 68, 78, 255};
const Rgba kInk = {236, 238, 242, 255};
const Rgba kSoft = {168, 174, 184, 255};
const Rgba kFaint = {112, 116, 124, 255};
const Rgba kAccent = {255, 168, 24, 255};
const Rgba kSelect = {255, 255, 255, 26};
const Rgba kField = {8, 9, 11, 255};
const Rgba kError = {255, 120, 100, 255};
const Rgba kOnAccent = {18, 20, 24, 255};

// DirectInput scan codes of the keys the prompt acts on.
enum : uint8_t {
    DIK_ESCAPE = 0x01,
    DIK_BACK = 0x0e,
    DIK_RETURN = 0x1c,
    DIK_NUMPADENTER = 0x9c,
    DIK_HOME = 0xc7,
    DIK_UP = 0xc8,
    DIK_LEFT = 0xcb,
    DIK_RIGHT = 0xcd,
    DIK_END = 0xcf,
    DIK_DOWN = 0xd0,
    DIK_DELETE = 0xd3,
};

const size_t kMaxServers = 10;
const size_t kMaxField = 255;

enum HitKind { HIT_FIELD, HIT_ROW, HIT_OK, HIT_CANCEL };
struct Hit {
    float x0, y0, x1, y1;
    HitKind kind;
    int index;
};

struct State {
    std::mutex mu;
    std::string path, title, text; // the list's file, game.toml's words
    bool open = false;
    bool busy = false; // a chosen server is being tried
    int answer = 0;    // 1 chosen and not yet polled, -1 cancelled
    std::string chosen;
    std::string field;
    size_t caret = 0; // a byte offset into field, on a character boundary
    std::vector<std::string> list;
    int selected = -1;
    std::string error;
    bool consumed[256] = {};
    // Drawing: what the last page put where, and what changed since.
    std::vector<Hit> hits;
    int hover_kind = -1, hover_index = -1;
    uint64_t serial = 1;
};
State g;

void changed() {
    ++g.serial;
}

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ')
        ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ')
        --b;
    return s.substr(a, b - a);
}

// The field is UTF-8; the caret moves a character, not a byte.
size_t prev_char(const std::string &s, size_t at) {
    if (at == 0)
        return 0;
    --at;
    while (at > 0 && ((unsigned char)s[at] & 0xc0) == 0x80)
        --at;
    return at;
}
size_t next_char(const std::string &s, size_t at) {
    if (at >= s.size())
        return s.size();
    ++at;
    while (at < s.size() && ((unsigned char)s[at] & 0xc0) == 0x80)
        ++at;
    return at;
}

// The list entry the field names, or -1.
int find_in_list(const std::string &text) {
    std::string canonical;
    if (!curl_http_parse_server(text, nullptr, &canonical))
        return -1;
    for (size_t i = 0; i < g.list.size(); ++i)
        if (g.list[i] == canonical)
            return int(i);
    return -1;
}

void select_row(int i) {
    if (i < 0 || i >= int(g.list.size()))
        return;
    g.selected = i;
    g.field = g.list[size_t(i)];
    g.caret = g.field.size();
    g.error.clear();
    changed();
}

// OK: the field, if it is an address, becomes the answer.
void submit() {
    std::string canonical;
    if (!curl_http_parse_server(g.field, nullptr, &canonical)) {
        g.error = trim(g.field).empty() ? "Type the server's address first."
                                        : "\"" + trim(g.field) +
                                              "\" is not a server address: an IP address or host "
                                              "name, optionally with :port.";
        changed();
        return;
    }
    g.chosen = canonical;
    g.answer = 1;
    g.busy = true;
    g.error.clear();
    fprintf(stderr, "[host] ubi-gs: server %s chosen\n", canonical.c_str());
    changed();
}

void cancel_locked() {
    if (!g.open)
        return;
    g.answer = -1;
    g.open = false;
    g.busy = false;
    fprintf(stderr, "[host] ubi-gs: cancelled\n");
    changed();
}

// ------------------------------------------------------------------ drawing --

struct Layout {
    double s;
    int pad, gap;
    Font title, body, small, field, row, button;
};
Layout layout_for(double s) {
    auto u = [s](double v) { return int(std::lround(v * s)); };
    Layout l;
    l.s = s;
    l.pad = u(52);
    l.gap = u(22);
    l.title = font(u(40), true);
    l.body = font(u(26), false);
    l.small = font(u(23), false);
    l.field = font(u(30), false);
    l.row = font(u(27), false);
    l.button = font(u(28), true);
    return l;
}

// Draws the page into *page and records the hits. Under g.mu.
void draw(int w, int h, std::vector<uint8_t> *page) {
    page->assign(size_t(w) * size_t(h) * 4, 0);
    Canvas c{w, h, page};
    c.fill(0, 0, w, h, kDim);
    std::vector<Hit> hits;
    auto hit = [&](int x0, int y0, int x1, int y1, HitKind kind, int index) {
        hits.push_back({float(x0) / w, float(y0) / h, float(x1) / w, float(y1) / h, kind, index});
    };

    // Laid out on a 1920x1080 reference, as the settings menu is, and made
    // smaller still when its content would not fit the height.
    double s = std::min(w / 1920.0, h / 1080.0);
    Layout l = layout_for(s);
    int pw = 0, ph = 0, inner = 0;
    std::vector<std::string> body, status;
    const size_t rows = std::min(g.list.size(), kMaxServers);
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto u = [&](double v) { return int(std::lround(v * l.s)); };
        pw = std::min(w - u(40), u(1240));
        inner = pw - 2 * l.pad;
        body = wrap(l.body, g.text, inner);
        const std::string line =
            g.busy ? "Asking " + g.chosen + " for the game's servers..." : g.error;
        status = line.empty() ? std::vector<std::string>{} : wrap(l.small, line, inner);
        ph = l.pad + l.title.height + l.gap + int(body.size()) * (l.body.height + u(4)) + l.gap +
             l.small.height + u(8) + u(66) + u(10) +
             std::max<int>(1, int(status.size())) * l.small.height + l.gap +
             (rows ? l.small.height + u(8) + int(rows) * u(50) + l.gap : 0) + u(66) + l.pad;
        if (ph <= h - u(24) || l.s < 0.2)
            break;
        l = layout_for(l.s * double(h - u(24)) / ph);
    }
    auto u = [&](double v) { return int(std::lround(v * l.s)); };
    const int px = (w - pw) / 2, py = std::max(0, (h - ph) / 2);
    c.fill(px - u(2), py - u(2), px + pw + u(2), py + ph + u(2), kEdge);
    c.fill(px, py, px + pw, py + ph, kPanel);
    const int left = px + l.pad, right = px + pw - l.pad;
    int y = py + l.pad;

    text(c, l.title, left, y, g.title.empty() ? "Server" : g.title, kInk, right);
    y += l.title.height + l.gap;
    for (const std::string &line : body) {
        text(c, l.body, left, y, line, kSoft, right);
        y += l.body.height + u(4);
    }
    y += l.gap;

    // The field, with its caret; a long address scrolls to keep the caret in view.
    text(c, l.small, left, y,
         "Server address: IP address or host name, optionally :port (80 if none)", kFaint, right);
    y += l.small.height + u(8);
    const int fh = u(66), fx0 = left, fx1 = right, fy = y;
    c.fill(fx0, fy, fx1, fy + fh, kField);
    outline(c, fx0, fy, fx1, fy + fh, std::max(1, u(2)), g.busy ? kEdge : kAccent);
    hit(fx0, fy, fx1, fy + fh, HIT_FIELD, 0);
    {
        const int tx = fx0 + u(18), room = fx1 - u(18) - tx;
        size_t first = 0;
        while (first < g.caret && measure(l.field, g.field.substr(first, g.caret - first)) > room)
            first = next_char(g.field, first);
        const int ty = fy + (fh - l.field.height) / 2;
        text(c, l.field, tx, ty, g.field.substr(first), g.busy ? kSoft : kInk, fx1 - u(14));
        if (!g.busy) {
            const int cx = tx + measure(l.field, g.field.substr(first, g.caret - first));
            c.fill(cx, ty + u(4), cx + std::max(2, u(3)), ty + l.field.height - u(2), kAccent);
        }
    }
    y += fh + u(10);
    if (status.empty()) {
        text(c, l.small, left, y, "Enter connects, Escape cancels.", kFaint, right);
        y += l.small.height;
    }
    for (const std::string &line : status) {
        text(c, l.small, left, y, line, g.busy ? kSoft : kError, right);
        y += l.small.height;
    }
    y += l.gap;

    // The servers that answered before.
    if (rows) {
        text(c, l.small, left, y, "Servers used before (Up and Down choose)", kFaint, right);
        y += l.small.height + u(8);
        const int rh = u(50);
        for (size_t i = 0; i < rows; ++i) {
            const bool on = int(i) == g.selected;
            const bool hover = g.hover_kind == HIT_ROW && g.hover_index == int(i);
            if (on || hover)
                c.fill(left, y, right, y + rh, kSelect);
            if (on)
                c.fill(left, y, left + u(5), y + rh, kAccent);
            text(c, l.row, left + u(22), y + (rh - l.row.height) / 2, g.list[i], on ? kInk : kSoft,
                 right - u(10));
            hit(left, y, right, y + rh, HIT_ROW, int(i));
            y += rh;
        }
        y += l.gap;
    }

    // OK and Cancel, at the bottom right.
    const int bh = u(66), by = py + ph - l.pad - bh;
    auto button = [&](int x1, const char *label, HitKind kind, bool primary) {
        const int bw = std::max(u(180), measure(l.button, label) + u(64));
        const int x0 = x1 - bw;
        const bool hover = g.hover_kind == kind;
        const bool enabled = !(primary && g.busy);
        if (primary && enabled) {
            c.fill(x0, by, x1, by + bh, kAccent);
            if (hover)
                c.fill(x0, by, x1, by + bh, kSelect);
        } else {
            c.fill(x0, by, x1, by + bh, hover ? Rgba{40, 43, 50, 255} : kPanel);
            outline(c, x0, by, x1, by + bh, std::max(1, u(2)), hover ? kSoft : kEdge);
        }
        const Rgba ink = primary && enabled ? kOnAccent : enabled ? kInk : kFaint;
        text(c, l.button, x0 + (bw - measure(l.button, label)) / 2, by + (bh - l.button.height) / 2,
             label, ink);
        hit(x0, by, x1, by + bh, kind, 0);
        return x0;
    };
    const int ok_left = button(right, "OK", HIT_OK, true);
    button(ok_left - u(20), "Cancel", HIT_CANCEL, false);
    g.hits = std::move(hits);
}

// ---------------------------------------------------- the runtime's chooser --

bool chooser_begin(const char *error) {
    return server_prompt::begin(error);
}
int chooser_poll(char *buf, size_t cap) {
    return server_prompt::poll(buf, cap);
}
void chooser_end() {
    server_prompt::end();
}
void chooser_used(const char *server) {
    server_prompt::used(server);
}
const CurlServerChooser kChooser = {chooser_begin, chooser_poll, chooser_end, chooser_used};

bool wants_text() {
    std::lock_guard lock(g.mu);
    return g.open && !g.busy;
}
const HostPagePrompt kPage = {server_prompt::is_open, server_prompt::render,  server_prompt::key,
                              server_prompt::text,    server_prompt::pointer, wants_text,
                              server_prompt::cancel};

} // namespace

// ------------------------------------------------------------- the list ----

std::string server_list_path(const std::string &profile_dir) {
    return profile_dir + "/ubi-gs-servers.txt";
}

std::vector<std::string> server_list_load(const std::string &path, size_t cap) {
    std::vector<std::string> list;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return list;
    std::string all;
    char chunk[4096];
    for (size_t n; (n = fread(chunk, 1, sizeof chunk, f)) > 0 && all.size() < (1u << 20);)
        all.append(chunk, n);
    fclose(f);
    size_t at = 0;
    while (at < all.size() && list.size() < cap) {
        size_t end = all.find('\n', at);
        if (end == std::string::npos)
            end = all.size();
        std::string line = all.substr(at, end - at);
        at = end + 1;
        line = line.substr(0, line.find('#'));
        std::string canonical;
        if (!curl_http_parse_server(line, nullptr, &canonical))
            continue; // blank, or damaged
        if (std::find(list.begin(), list.end(), canonical) == list.end())
            list.push_back(canonical);
    }
    return list;
}

void server_list_remember(std::vector<std::string> *list, const std::string &server, size_t cap) {
    if (!list || server.empty())
        return;
    list->erase(std::remove(list->begin(), list->end(), server), list->end());
    list->insert(list->begin(), server);
    if (list->size() > cap)
        list->resize(cap);
}

bool server_list_save(const std::string &path, const std::vector<std::string> &list) {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0)
        os_mkdir(path.substr(0, slash).c_str());
    const std::string temporary = path + ".new";
    FILE *f = fopen(temporary.c_str(), "wb");
    if (!f)
        return false;
    fprintf(f, "# The ubi-gs servers that answered, most recent first (host or host:port).\n");
    for (const std::string &s : list)
        fprintf(f, "%s\n", s.c_str());
    const bool ok = fflush(f) == 0;
    fclose(f);
    if (!ok || os_rename(temporary.c_str(), path.c_str()) != 0) {
        os_unlink(temporary.c_str());
        return false;
    }
    return true;
}

void server_prompt_install(const std::string &profile_dir, const char *title, const char *text) {
    {
        std::lock_guard lock(g.mu);
        g.path = server_list_path(profile_dir);
        g.title = title ? title : "";
        g.text = text ? text : "";
    }
    curl_http_set_chooser(&kChooser);
    host_page_set_prompt(&kPage);
}

// ------------------------------------------------------------ the model ----

namespace server_prompt {

bool begin(const char *error) {
    std::lock_guard lock(g.mu);
    if (!g.open) {
        g.list = server_list_load(g.path, kMaxServers);
        g.selected = g.list.empty() ? -1 : 0;
        g.field = g.list.empty() ? "" : g.list[0];
        g.caret = g.field.size();
        g.hover_kind = g.hover_index = -1;
        fprintf(stderr, "[host] ubi-gs: asking for a server (%zu used before%s%s)\n", g.list.size(),
                g.list.empty() ? "" : ", preselected ", g.list.empty() ? "" : g.list[0].c_str());
    }
    g.open = true;
    g.busy = false;
    g.answer = 0;
    g.error = error ? error : "";
    changed();
    return true;
}

int poll(char *buf, size_t cap) {
    std::lock_guard lock(g.mu);
    if (g.answer == 1) {
        g.answer = 0;
        if (buf && cap) {
            strncpy(buf, g.chosen.c_str(), cap - 1);
            buf[cap - 1] = 0;
        }
        return 1;
    }
    return g.answer < 0 ? -1 : 0;
}

void end() {
    std::lock_guard lock(g.mu);
    g.open = false;
    g.busy = false;
    g.answer = 0;
    g.error.clear();
    changed();
}

void used(const char *server) {
    if (!server || !*server)
        return;
    std::lock_guard lock(g.mu);
    std::vector<std::string> list = server_list_load(g.path, kMaxServers);
    server_list_remember(&list, server, kMaxServers);
    if (server_list_save(g.path, list))
        fprintf(stderr, "[host] ubi-gs: %s remembered in %s\n", server, g.path.c_str());
    else
        fprintf(stderr, "[host] ubi-gs: could not write %s\n", g.path.c_str());
    g.list = list;
}

bool is_open() {
    std::lock_guard lock(g.mu);
    return g.open;
}
bool busy() {
    std::lock_guard lock(g.mu);
    return g.busy;
}

bool key(uint8_t dik, bool down) {
    std::lock_guard lock(g.mu);
    if (!down) {
        // A release is the prompt's when its press was.
        const bool mine = g.consumed[dik];
        g.consumed[dik] = false;
        return mine;
    }
    if (!g.open)
        return false;
    g.consumed[dik] = true;
    if (dik == DIK_ESCAPE) {
        cancel_locked();
        return true;
    }
    if (g.busy)
        return true; // only Escape while a server is being tried
    switch (dik) {
    case DIK_RETURN:
    case DIK_NUMPADENTER:
        submit();
        break;
    case DIK_UP:
        if (!g.list.empty())
            select_row(g.selected <= 0 ? 0 : g.selected - 1);
        break;
    case DIK_DOWN:
        if (!g.list.empty())
            select_row(std::min(int(g.list.size()) - 1, g.selected + 1));
        break;
    case DIK_LEFT:
        g.caret = prev_char(g.field, g.caret);
        changed();
        break;
    case DIK_RIGHT:
        g.caret = next_char(g.field, g.caret);
        changed();
        break;
    case DIK_HOME:
        g.caret = 0;
        changed();
        break;
    case DIK_END:
        g.caret = g.field.size();
        changed();
        break;
    case DIK_BACK:
        if (g.caret > 0) {
            const size_t from = prev_char(g.field, g.caret);
            g.field.erase(from, g.caret - from);
            g.caret = from;
            g.selected = find_in_list(g.field);
            changed();
        }
        break;
    case DIK_DELETE:
        if (g.caret < g.field.size()) {
            g.field.erase(g.caret, next_char(g.field, g.caret) - g.caret);
            g.selected = find_in_list(g.field);
            changed();
        }
        break;
    default:
        break; // the characters themselves come as text
    }
    return true;
}

void text(const char *utf8) {
    if (!utf8)
        return;
    std::lock_guard lock(g.mu);
    if (!g.open || g.busy)
        return;
    std::string add;
    for (const char *p = utf8; *p; ++p)
        if ((unsigned char)*p >= 0x20 && *p != 0x7f)
            add += *p;
    if (add.empty())
        return;
    if (g.field.size() + add.size() > kMaxField) {
        // Cut on a character boundary, never through one.
        size_t cut = kMaxField > g.field.size() ? kMaxField - g.field.size() : 0;
        while (cut > 0 && ((unsigned char)add[cut] & 0xc0) == 0x80)
            --cut;
        add.resize(cut);
    }
    g.field.insert(g.caret, add);
    g.caret += add.size();
    g.selected = find_in_list(g.field);
    g.error.clear();
    changed();
}

void pointer(float u, float v, int button, int down, int wheel) {
    std::lock_guard lock(g.mu);
    if (!g.open)
        return;
    const Hit *found = nullptr;
    for (const Hit &h : g.hits)
        if (u >= h.x0 && u < h.x1 && v >= h.y0 && v < h.y1) {
            found = &h;
            break;
        }
    const int kind = found ? int(found->kind) : -1, index = found ? found->index : -1;
    if (kind != g.hover_kind || index != g.hover_index) {
        g.hover_kind = kind;
        g.hover_index = index;
        changed();
    }
    if (wheel && !g.busy && !g.list.empty()) {
        const int step = wheel > 0 ? -1 : 1;
        select_row(std::clamp(g.selected + step, 0, int(g.list.size()) - 1));
        return;
    }
    if (button != 0 || !down || !found)
        return; // the press acts; its release and the other buttons do nothing
    switch (found->kind) {
    case HIT_OK:
        if (!g.busy)
            submit();
        break;
    case HIT_CANCEL:
        cancel_locked();
        break;
    case HIT_ROW:
        if (!g.busy)
            select_row(found->index);
        break;
    case HIT_FIELD:
        if (!g.busy) {
            g.caret = g.field.size();
            changed();
        }
        break;
    }
}

int render(int w, int h, uint64_t *generation, const uint8_t **pixels) {
    if (!generation || !pixels || w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return 0;
    std::lock_guard lock(g.mu);
    if (!g.open)
        return 0;
    const uint64_t key = (g.serial * 0x9e3779b97f4a7c15ull) ^ (uint64_t(uint32_t(w)) << 40) ^
                         (uint64_t(uint32_t(h)) << 20) ^ 0x5e5e;
    if (*generation == key)
        return 1;
    // The thread that seals a frame and the presenter's (prompt over a guest
    // that presents nothing) may both draw; each keeps its own pixels until
    // its next call.
    static thread_local std::vector<uint8_t> page;
    draw(w, h, &page);
    *generation = key;
    *pixels = page.data();
    return 2;
}

void cancel() {
    std::lock_guard lock(g.mu);
    cancel_locked();
}

std::string field() {
    std::lock_guard lock(g.mu);
    return g.field;
}
int selected() {
    std::lock_guard lock(g.mu);
    return g.selected;
}
std::string error() {
    std::lock_guard lock(g.mu);
    return g.error;
}
std::vector<std::string> servers() {
    std::lock_guard lock(g.mu);
    return g.list;
}

} // namespace server_prompt
