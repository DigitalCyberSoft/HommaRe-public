// gdi32_canvas.cpp - see gdi32_canvas.h. Moved out of the settings menu's
// drawing so a second host page draws with the same type and the same cache.
#include "gdi32_canvas.h"
#include "gdi32_truetype.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <tuple>

namespace gdi_canvas {

void Canvas::blend(int x, int y, Rgba c, int cov) {
    if (x < 0 || y < 0 || x >= w || y >= h || cov <= 0)
        return;
    uint8_t *p = px->data() + (size_t(y) * w + x) * 4;
    const int a = c.a * cov / 255;
    p[0] = uint8_t((c.r * a + p[0] * (255 - a)) / 255);
    p[1] = uint8_t((c.g * a + p[1] * (255 - a)) / 255);
    p[2] = uint8_t((c.b * a + p[2] * (255 - a)) / 255);
    p[3] = uint8_t(a + p[3] * (255 - a) / 255);
}

void Canvas::fill(int x0, int y0, int x1, int y1, Rgba c) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, w), y1 = std::min(y1, h);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            blend(x, y, c, 255);
}

std::vector<uint32_t> decode(const std::string &s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + n >= s.size() + (n == 0)) {
            out.push_back('?');
            ++i;
            continue;
        }
        uint32_t cp = n == 0 ? c : n == 1 ? (c & 31) : n == 2 ? (c & 15) : (c & 7);
        bool ok = true;
        for (int k = 1; k <= n; ++k) {
            const uint8_t d = uint8_t(s[i + k]);
            if ((d >> 6) != 2)
                ok = false;
            cp = (cp << 6) | (d & 63);
        }
        out.push_back(ok ? cp : '?');
        i += ok ? n + 1 : 1;
    }
    return out;
}

namespace {

// Two pages may be drawn by two threads (a seal on one, a test on another);
// the cache is the one thing they share.
std::mutex g_glyphs_mu;
std::map<std::tuple<const gdi::TrueTypeFace *, double, uint32_t>, gdi::TrueTypeGlyph> g_glyphs;

// Under g_glyphs_mu: the reference lives until the next call clears the cache.
const gdi::TrueTypeGlyph &glyph(const Font &f, uint32_t cp) {
    auto key = std::make_tuple(f.face, f.scale, cp);
    auto it = g_glyphs.find(key);
    if (it != g_glyphs.end())
        return it->second;
    if (g_glyphs.size() > 2048)
        g_glyphs.clear();
    gdi::TrueTypeGlyph g;
    gdi::truetype_render_glyph(f.face, f.scale, cp, &g);
    return g_glyphs.emplace(key, std::move(g)).first->second;
}

} // namespace

Font font(int px, bool bold) {
    Font f;
    f.face = gdi::truetype_windows_substitute("Segoe UI", bold ? 600 : 400);
    if (!f.face)
        return f;
    f.scale = gdi::truetype_scale(f.face, -std::max(px, 6));
    const gdi::TrueTypeMetrics m = gdi::truetype_metrics(f.face, f.scale);
    f.ascent = m.ascent;
    f.height = m.height;
    return f;
}

int measure(const Font &f, const std::vector<uint32_t> &cps) {
    if (!f.face)
        return 0;
    int x = 0;
    for (size_t i = 0; i < cps.size(); ++i) {
        x += gdi::truetype_advance(f.face, f.scale, cps[i]);
        if (i + 1 < cps.size())
            x += gdi::truetype_kern(f.face, f.scale, cps[i], cps[i + 1]);
    }
    return x;
}
int measure(const Font &f, const std::string &s) {
    return measure(f, decode(s));
}

int text(Canvas &c, const Font &f, int x, int y, const std::string &s, Rgba ink, int max_x) {
    if (!f.face)
        return 0;
    const std::vector<uint32_t> cps = decode(s);
    const int x0 = x, base = y + f.ascent;
    std::lock_guard lock(g_glyphs_mu);
    for (size_t i = 0; i < cps.size(); ++i) {
        const gdi::TrueTypeGlyph &g = glyph(f, cps[i]);
        for (int gy = 0; gy < g.h; ++gy)
            for (int gx = 0; gx < g.w; ++gx) {
                const int px = x + g.x + gx;
                if (px < max_x)
                    c.blend(px, base + g.y + gy, ink, g.coverage[size_t(gy) * g.w + gx]);
            }
        x += gdi::truetype_advance(f.face, f.scale, cps[i]);
        if (i + 1 < cps.size())
            x += gdi::truetype_kern(f.face, f.scale, cps[i], cps[i + 1]);
    }
    return x - x0;
}

std::vector<std::string> wrap(const Font &f, const std::string &s, int width) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush_word = [&] {
        if (word.empty())
            return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(f, trial) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ' || ch == '\n') {
            flush_word();
            if (ch == '\n') {
                lines.push_back(line);
                line.clear();
            }
        } else {
            word += ch;
        }
    }
    flush_word();
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

void outline(Canvas &c, int x0, int y0, int x1, int y1, int t, Rgba ink) {
    c.fill(x0, y0, x1, y0 + t, ink);
    c.fill(x0, y1 - t, x1, y1, ink);
    c.fill(x0, y0, x0 + t, y1, ink);
    c.fill(x1 - t, y0, x1, y1, ink);
}

} // namespace gdi_canvas
