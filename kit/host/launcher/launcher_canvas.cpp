// launcher_canvas.cpp - see launcher_canvas.h.
#include "launcher_canvas.h"

#include <algorithm>
#include <cmath>

extern "C" const uint8_t *mods_font6x8_glyph(char c); // mods/font6x8.cpp

namespace launcher {

namespace {

// The colour `stops` give at `at`.
Color stop_color(const std::vector<Stop> &stops, float at) {
    if (at <= stops.front().at)
        return stops.front().c;
    for (size_t k = 0; k + 1 < stops.size(); ++k) {
        const Stop &a = stops[k], &b = stops[k + 1];
        if (at < a.at || at > b.at)
            continue;
        const float f = b.at > a.at ? (at - a.at) / (b.at - a.at) : 0.0f;
        auto mix = [f](uint8_t p, uint8_t q) {
            return uint8_t(std::lround(float(p) + (float(q) - float(p)) * f));
        };
        return {mix(a.c.r, b.c.r), mix(a.c.g, b.c.g), mix(a.c.b, b.c.b), mix(a.c.a, b.c.a)};
    }
    return stops.back().c;
}

} // namespace

const uint8_t *font_glyph(char c) {
    return mods_font6x8_glyph(c >= 32 && c < 127 ? c : '?');
}

void Canvas::resize(int w, int h, bool bgra) {
    w_ = std::max(0, w);
    h_ = std::max(0, h);
    bgra_ = bgra;
    clip_ = {0, 0, w_, h_};
    px_.assign(size_t(w_) * size_t(h_) * 4, 0);
}

void Canvas::put(int x, int y, Color c) {
    if (!clip_.contains(x, y))
        return;
    uint8_t *p = &px_[(size_t(y) * size_t(w_) + size_t(x)) * 4];
    const uint8_t r = bgra_ ? c.b : c.r, b = bgra_ ? c.r : c.b;
    if (c.a == 255) {
        p[0] = r;
        p[1] = c.g;
        p[2] = b;
        p[3] = 255;
        return;
    }
    const int a = c.a, na = 255 - a;
    p[0] = uint8_t((r * a + p[0] * na) / 255);
    p[1] = uint8_t((c.g * a + p[1] * na) / 255);
    p[2] = uint8_t((b * a + p[2] * na) / 255);
    p[3] = 255;
}

void Canvas::clear(Color c) {
    fill({0, 0, w_, h_}, {c.r, c.g, c.b, 255});
}

void Canvas::fill(Rect r, Color c) {
    const int x0 = std::max(clip_.x, r.x), y0 = std::max(clip_.y, r.y);
    const int x1 = std::min(clip_.x + clip_.w, r.x + r.w),
              y1 = std::min(clip_.y + clip_.h, r.y + r.h);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            put(x, y, c);
}

void Canvas::frame(Rect r, int t, Color c) {
    fill({r.x, r.y, r.w, t}, c);
    fill({r.x, r.y + r.h - t, r.w, t}, c);
    fill({r.x, r.y + t, t, r.h - 2 * t}, c);
    fill({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

int Canvas::text(int x, int y, const std::string &s, int size, Color c) {
    size = std::max(1, size);
    for (size_t i = 0; i < s.size(); ++i) {
        const uint8_t *glyph = font_glyph(s[i]);
        const int gx = x + int(i) * 6 * size;
        if (gx >= w_)
            break;
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 6; ++col)
                if (glyph[row] & (1 << (5 - col)))
                    fill({gx + col * size, y + row * size, size, size}, c);
    }
    return text_width(s, size);
}

std::vector<std::string> Canvas::wrap(const std::string &s, int width, int size) {
    const size_t per_line = size_t(std::max(1, width / (6 * std::max(1, size))));
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t newline = s.find('\n', start);
        std::string para =
            s.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
        while (para.size() > per_line) {
            size_t cut = para.rfind(' ', per_line);
            if (cut == std::string::npos || cut == 0) {
                // A path: after a slash rather than inside a name.
                const size_t slash = para.rfind('/', per_line - 1);
                cut = slash != std::string::npos && slash > 0 ? slash + 1 : per_line;
            }
            lines.push_back(para.substr(0, cut));
            para = para.substr(cut + (para[cut] == ' ' ? 1 : 0));
        }
        lines.push_back(para);
        if (newline == std::string::npos)
            break;
        start = newline + 1;
    }
    return lines;
}

int Canvas::paragraph(int x, int y, int width, const std::string &s, int size, Color c) {
    const std::vector<std::string> lines = wrap(s, width, size);
    const int line_h = text_height(size) + size * 3;
    for (size_t i = 0; i < lines.size(); ++i)
        text(x, y + int(i) * line_h, lines[i], size, c);
    return int(lines.size()) * line_h;
}

void Canvas::set_clip(Rect r) {
    const int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
    const int x1 = std::min(w_, r.x + r.w), y1 = std::min(h_, r.y + r.h);
    clip_ = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void Canvas::reset_clip() {
    clip_ = {0, 0, w_, h_};
}

void Canvas::blend_px(uint8_t *p, int r, int g, int b, int a, Blend blend) {
    if (blend == Blend::Add) {
        p[0] = uint8_t(std::min(255, p[0] + r));
        p[1] = uint8_t(std::min(255, p[1] + g));
        p[2] = uint8_t(std::min(255, p[2] + b));
        return;
    }
    const int na = 255 - a;
    p[0] = uint8_t(std::min(255, r + (p[0] * na + 127) / 255));
    p[1] = uint8_t(std::min(255, g + (p[1] * na + 127) / 255));
    p[2] = uint8_t(std::min(255, b + (p[2] * na + 127) / 255));
}

void Canvas::image(const Image &img, Rect dst, int opacity, Blend blend, Rect src) {
    if (img.empty() || dst.w <= 0 || dst.h <= 0 || opacity <= 0)
        return;
    if (src.w <= 0 || src.h <= 0)
        src = {0, 0, img.w, img.h};
    const int x0 = std::max(clip_.x, dst.x), y0 = std::max(clip_.y, dst.y);
    const int x1 = std::min(clip_.x + clip_.w, dst.x + dst.w),
              y1 = std::min(clip_.y + clip_.h, dst.y + dst.h);
    if (x0 >= x1 || y0 >= y1)
        return;
    // Pixel centres map onto the source's, clamped to the part: a nine-slice's
    // pieces do not bleed into each other. A picture shrunk by more than half
    // averages a box of its pixels rather than skipping most of them.
    const float sx = float(src.w) / float(dst.w), sy = float(src.h) / float(dst.h);
    const int box_x = std::max(1, int(sx)), box_y = std::max(1, int(sy));
    const int ri = bgra_ ? 2 : 0, bi = bgra_ ? 0 : 2;
    const size_t n = size_t(x1 - x0);
    std::vector<int> xa(n), xb(n), xf(n);
    for (int x = x0; x < x1; ++x) {
        const float fx = std::clamp((float(x - dst.x) + 0.5f) * sx - 0.5f, 0.0f, float(src.w - 1));
        const int ix = box_x > 1 ? std::min(int(float(x - dst.x) * sx), src.w - box_x) : int(fx);
        xa[size_t(x - x0)] = src.x + ix;
        xb[size_t(x - x0)] = src.x + std::min(ix + 1, src.w - 1);
        xf[size_t(x - x0)] = int((fx - float(int(fx))) * 256.0f);
    }
    for (int y = y0; y < y1; ++y) {
        const float fy = std::clamp((float(y - dst.y) + 0.5f) * sy - 0.5f, 0.0f, float(src.h - 1));
        const int iy = box_y > 1 ? std::min(int(float(y - dst.y) * sy), src.h - box_y) : int(fy);
        const int wy = int((fy - float(int(fy))) * 256.0f);
        const uint8_t *r0 = &img.px[size_t(src.y + iy) * size_t(img.w) * 4];
        const uint8_t *r1 =
            &img.px[size_t(src.y + std::min(iy + 1, src.h - 1)) * size_t(img.w) * 4];
        uint8_t *out = &px_[(size_t(y) * size_t(w_) + size_t(x0)) * 4];
        for (int x = x0; x < x1; ++x, out += 4) {
            const size_t i = size_t(x - x0);
            int v[4];
            if (box_x > 1 || box_y > 1) {
                int sum[4] = {0, 0, 0, 0};
                for (int by = 0; by < box_y; ++by) {
                    const uint8_t *p =
                        &img.px[(size_t(src.y + iy + by) * size_t(img.w) + size_t(xa[i])) * 4];
                    for (int bx = 0; bx < box_x; ++bx, p += 4)
                        for (int k = 0; k < 4; ++k)
                            sum[k] += p[k];
                }
                for (int k = 0; k < 4; ++k)
                    v[k] = sum[k] / (box_x * box_y) * opacity / 255;
            } else {
                const uint8_t *a = r0 + xa[i] * 4, *b = r0 + xb[i] * 4, *c = r1 + xa[i] * 4,
                              *d = r1 + xb[i] * 4;
                const int wx = xf[i];
                for (int k = 0; k < 4; ++k) {
                    const int top = a[k] * (256 - wx) + b[k] * wx,
                              bottom = c[k] * (256 - wx) + d[k] * wx;
                    v[k] = ((top * (256 - wy) + bottom * wy) >> 16) * opacity / 255;
                }
            }
            if (v[3] == 0 && blend == Blend::Over)
                continue;
            blend_px(out, v[ri], v[1], v[bi], v[3], blend);
        }
    }
}

void Canvas::nine_slice(const Image &img, Rect dst, int top, int left, int bottom, int right,
                        float scale, int opacity, Blend blend, bool middle) {
    if (img.empty() || dst.w <= 0 || dst.h <= 0)
        return;
    // The corners shrink together where the box is smaller than they are.
    float s = scale;
    if (float(left + right) * s > float(dst.w))
        s = float(dst.w) / float(left + right);
    if (float(top + bottom) * s > float(dst.h))
        s = float(dst.h) / float(top + bottom);
    const int l = int(std::lround(float(left) * s)), r = int(std::lround(float(right) * s));
    const int t = int(std::lround(float(top) * s)), b = int(std::lround(float(bottom) * s));
    const int sx[4] = {0, left, img.w - right, img.w}, sy[4] = {0, top, img.h - bottom, img.h};
    const int dx[4] = {dst.x, dst.x + l, dst.x + dst.w - r, dst.x + dst.w};
    const int dy[4] = {dst.y, dst.y + t, dst.y + dst.h - b, dst.y + dst.h};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            if (!middle && i == 1 && j == 1)
                continue;
            const Rect from{sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]};
            const Rect to{dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]};
            if (from.w > 0 && from.h > 0 && to.w > 0 && to.h > 0)
                image(img, to, opacity, blend, from);
        }
}

void Canvas::gradient(Rect r, const std::vector<Stop> &stops, bool across) {
    if (stops.empty() || r.w <= 0 || r.h <= 0)
        return;
    const int n = across ? r.w : r.h;
    for (int i = 0; i < n; ++i) {
        const float at = n > 1 ? float(i) / float(n - 1) : 0.0f;
        const Color c = stop_color(stops, at);
        if (c.a)
            fill(across ? Rect{r.x + i, r.y, 1, r.h} : Rect{r.x, r.y + i, r.w, 1}, c);
    }
}

void Canvas::coverage(int x, int y, int w, int h, const uint8_t *alpha, int stride, Color c) {
    const int x0 = std::max(clip_.x, x), y0 = std::max(clip_.y, y);
    const int x1 = std::min(clip_.x + clip_.w, x + w), y1 = std::min(clip_.y + clip_.h, y + h);
    const int r = bgra_ ? c.b : c.r, b = bgra_ ? c.r : c.b;
    for (int py = y0; py < y1; ++py) {
        const uint8_t *row = alpha + size_t(py - y) * size_t(stride);
        uint8_t *out = &px_[(size_t(py) * size_t(w_) + size_t(x0)) * 4];
        for (int px = x0; px < x1; ++px, out += 4) {
            const int a = row[px - x] * c.a / 255;
            if (a)
                blend_px(out, r * a / 255, c.g * a / 255, b * a / 255, a, Blend::Over);
        }
    }
}

void Canvas::shadow(Rect r, int radius, Color c) {
    if (radius <= 0) {
        fill(r, c);
        return;
    }
    const int x0 = std::max(clip_.x, r.x - radius), y0 = std::max(clip_.y, r.y - radius);
    const int x1 = std::min(clip_.x + clip_.w, r.x + r.w + radius);
    const int y1 = std::min(clip_.y + clip_.h, r.y + r.h + radius);
    const int cr = bgra_ ? c.b : c.r, cb = bgra_ ? c.r : c.b;
    for (int y = y0; y < y1; ++y) {
        const float dy = float(std::max({r.y - y, y - (r.y + r.h - 1), 0}));
        uint8_t *out = &px_[(size_t(y) * size_t(w_) + size_t(x0)) * 4];
        for (int x = x0; x < x1; ++x, out += 4) {
            const float dx = float(std::max({r.x - x, x - (r.x + r.w - 1), 0}));
            // Falls off with the distance from the box, about as a blur would.
            const float t = std::min(1.0f, std::sqrt(dx * dx + dy * dy) / float(radius));
            const int a = int(float(c.a) * (1 - t) * (1 - t));
            if (a)
                blend_px(out, cr * a / 255, c.g * a / 255, cb * a / 255, a, Blend::Over);
        }
    }
}

void Canvas::rounded(Rect r, int radius, Color c, int thickness) {
    rounded(r, radius, std::vector<Stop>{{0, c}}, thickness);
}

void Canvas::radial(Rect area, float cx, float cy, float radius, Color inner, Color outer) {
    const int x0 = std::max(clip_.x, area.x), y0 = std::max(clip_.y, area.y);
    const int x1 = std::min(clip_.x + clip_.w, area.x + area.w),
              y1 = std::min(clip_.y + clip_.h, area.y + area.h);
    const std::vector<Stop> stops{{0, inner}, {1, outer}};
    for (int y = y0; y < y1; ++y) {
        uint8_t *out = &px_[(size_t(y) * size_t(w_) + size_t(x0)) * 4];
        for (int x = x0; x < x1; ++x, out += 4) {
            const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
            const Color c = stop_color(
                stops, std::min(1.0f, std::sqrt(dx * dx + dy * dy) / std::max(1.0f, radius)));
            const int a = c.a;
            if (a)
                blend_px(out, (bgra_ ? c.b : c.r) * a / 255, c.g * a / 255,
                         (bgra_ ? c.r : c.b) * a / 255, a, Blend::Over);
        }
    }
}

void Canvas::rounded(Rect r, int radius, const std::vector<Stop> &stops, int thickness) {
    if (stops.empty() || r.w <= 0 || r.h <= 0)
        return;
    radius = std::max(0, std::min({radius, r.w / 2, r.h / 2}));
    const int x0 = std::max(clip_.x, r.x), y0 = std::max(clip_.y, r.y);
    const int x1 = std::min(clip_.x + clip_.w, r.x + r.w),
              y1 = std::min(clip_.y + clip_.h, r.y + r.h);
    const float hw = float(r.w) / 2, hh = float(r.h) / 2, rad = float(radius);
    for (int y = y0; y < y1; ++y) {
        const Color c = stop_color(stops, r.h > 1 ? float(y - r.y) / float(r.h - 1) : 0.0f);
        const int cr = bgra_ ? c.b : c.r, cb = bgra_ ? c.r : c.b;
        uint8_t *out = &px_[(size_t(y) * size_t(w_) + size_t(x0)) * 4];
        for (int x = x0; x < x1; ++x, out += 4) {
            // The signed distance to the edge, negative inside.
            const float qx = std::fabs(float(x) + 0.5f - (float(r.x) + hw)) - (hw - rad);
            const float qy = std::fabs(float(y) + 0.5f - (float(r.y) + hh)) - (hh - rad);
            const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
            const float d = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - rad;
            float cov = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (thickness > 0)
                cov = std::min(cov, std::clamp(d + float(thickness) + 0.5f, 0.0f, 1.0f));
            const int a = int(float(c.a) * cov);
            if (a)
                blend_px(out, cr * a / 255, c.g * a / 255, cb * a / 255, a, Blend::Over);
        }
    }
}

void Canvas::blur(Rect r, int radius) {
    const int x0 = std::max(clip_.x, r.x), y0 = std::max(clip_.y, r.y);
    const int x1 = std::min(clip_.x + clip_.w, r.x + r.w),
              y1 = std::min(clip_.y + clip_.h, r.y + r.h);
    if (radius <= 0 || x0 >= x1 || y0 >= y1)
        return;
    const int w = x1 - x0, h = y1 - y0;
    std::vector<int> line(size_t(std::max(w, h)) * 3);
    // A running box sum along a row or a column, the ends held.
    auto pass = [&](uint8_t *first, int n, size_t step) {
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k)
                line[size_t(i) * 3 + size_t(k)] = first[size_t(i) * step + size_t(k)];
        const int span = 2 * radius + 1;
        for (int k = 0; k < 3; ++k) {
            int sum = 0;
            for (int i = -radius; i <= radius; ++i)
                sum += line[size_t(std::clamp(i, 0, n - 1)) * 3 + size_t(k)];
            for (int i = 0; i < n; ++i) {
                first[size_t(i) * step + size_t(k)] = uint8_t(sum / span);
                sum += line[size_t(std::min(i + radius + 1, n - 1)) * 3 + size_t(k)] -
                       line[size_t(std::max(i - radius, 0)) * 3 + size_t(k)];
            }
        }
    };
    for (int round = 0; round < 3; ++round) {
        for (int y = y0; y < y1; ++y)
            pass(&px_[(size_t(y) * size_t(w_) + size_t(x0)) * 4], w, 4);
        for (int x = x0; x < x1; ++x)
            pass(&px_[(size_t(y0) * size_t(w_) + size_t(x)) * 4], h, size_t(w_) * 4);
    }
}

} // namespace launcher
