// launcher_canvas.h - a CPU canvas the launcher draws on: filled rectangles
// and the kit's 6x8 font at integer sizes; for the native launcher on Linux
// (linux/) also pictures, nine-slice frames, gradients and glyph coverage.
// Pixels are BGRA or RGBA to match the drawable the canvas is copied to.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace launcher {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

struct Color {
    uint8_t r, g, b, a;
};

// A picture the canvas draws: premultiplied RGBA, row after row.
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool empty() const {
        return w <= 0 || h <= 0;
    }
};

// How a picture meets the canvas: laid over it, or added onto it (the light
// the game's additive textures carry).
enum class Blend { Over, Add };

// A gradient's colour at `at` (0..1 along it).
struct Stop {
    float at;
    Color c;
};

class Canvas {
  public:
    void resize(int w, int h, bool bgra);
    int width() const {
        return w_;
    }
    int height() const {
        return h_;
    }
    const uint8_t *pixels() const {
        return px_.data();
    }
    void clear(Color c);
    void fill(Rect r, Color c);
    void frame(Rect r, int thickness, Color c);
    // Text at `size` pixels per font pixel; returns the width drawn. Characters
    // outside ASCII 32-126 draw as '?'.
    int text(int x, int y, const std::string &s, int size, Color c);
    static int text_width(const std::string &s, int size) {
        return int(s.size()) * 6 * size;
    }
    static int text_height(int size) {
        return 8 * size;
    }
    // Word-wrapped to `width`; returns the height used.
    int paragraph(int x, int y, int width, const std::string &s, int size, Color c);
    static std::vector<std::string> wrap(const std::string &s, int width, int size);

    // Everything below draws inside `r` only, until reset_clip.
    void set_clip(Rect r);
    void reset_clip();
    Rect clip() const {
        return clip_;
    }
    // `src` of `img` (all of it when empty) scaled into `dst`, filtered, at
    // `opacity` (0-255).
    void image(const Image &img, Rect dst, int opacity = 255, Blend blend = Blend::Over,
               Rect src = {});
    // A nine-slice: the corners (insets in the picture's pixels) kept at
    // `scale` times their size, the edges and the middle stretched; without
    // `middle`, the frame's ring only.
    void nine_slice(const Image &img, Rect dst, int top, int left, int bottom, int right,
                    float scale, int opacity = 255, Blend blend = Blend::Over, bool middle = true);
    // A gradient through `stops` (in order of `at`) down `r`, or across it.
    void gradient(Rect r, const std::vector<Stop> &stops, bool across = false);
    // Coverage (0-255 a pixel, rows `stride` apart) in colour `c`: a glyph.
    void coverage(int x, int y, int w, int h, const uint8_t *alpha, int stride, Color c);
    // A soft shadow under `r`: `c` fading out over `radius` pixels.
    void shadow(Rect r, int radius, Color c);
    // A rectangle with round corners, smoothed at the edge, filled or as a
    // `thickness` ring; in one colour or a gradient down it.
    void rounded(Rect r, int radius, Color c, int thickness = 0);
    void rounded(Rect r, int radius, const std::vector<Stop> &stops, int thickness = 0);
    // `area` from `inner` at (cx, cy) to `outer` `radius` pixels away and beyond.
    void radial(Rect area, float cx, float cy, float radius, Color inner, Color outer);
    // A box blur of what `r` holds, three passes (near a Gaussian).
    void blur(Rect r, int radius);

  private:
    void put(int x, int y, Color c);
    // Premultiplied (r, g, b, a) in canvas order over the pixel, or added.
    void blend_px(uint8_t *p, int r, int g, int b, int a, Blend blend);
    int w_ = 0, h_ = 0;
    bool bgra_ = true;
    Rect clip_;
    std::vector<uint8_t> px_;
};

// The glyph table the settings page uses (mods/font6x8.cpp).
const uint8_t *font_glyph(char c);

} // namespace launcher
