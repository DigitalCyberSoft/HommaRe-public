// gdi32_canvas.h - straight-alpha RGBA pages drawn with the kit's own Open
// Sans (gdi32_truetype.h), for what the host lays over the game in host UI
// space: the tabbed settings menu (mods/settings_menu_draw.cpp) and the
// ubi-gs server prompt (host/server_prompt.cpp). Both are drawn on the thread
// that seals a frame, never the guest's, so the glyphs come from a cache of
// their own (gdi's is the guest's), under a lock of their own.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gdi {
struct TrueTypeFace;
}

namespace gdi_canvas {

struct Rgba {
    uint8_t r, g, b, a;
};

// w x h straight-alpha RGBA pixels in *px, which the caller sized.
struct Canvas {
    int w, h;
    std::vector<uint8_t> *px;
    void blend(int x, int y, Rgba c, int cov); // cov 0-255; clipped to the canvas
    void fill(int x0, int y0, int x1, int y1, Rgba c);
};

// UTF-8 to codepoints; a malformed byte becomes '?'.
std::vector<uint32_t> decode(const std::string &s);

// "Segoe UI" as the runtime substitutes it (Open Sans), `px` pixels to the em.
// A face of null means no font: measures are 0 and text draws nothing.
struct Font {
    const gdi::TrueTypeFace *face = nullptr;
    double scale = 0;
    int ascent = 0, height = 0;
};
Font font(int px, bool bold);

int measure(const Font &f, const std::vector<uint32_t> &cps);
int measure(const Font &f, const std::string &s);
// Text with its top at y. Returns the advance. Clipped at max_x when given.
int text(Canvas &c, const Font &f, int x, int y, const std::string &s, Rgba ink,
         int max_x = 1 << 30);
// Word-wrapped lines no wider than width; '\n' starts a new line.
std::vector<std::string> wrap(const Font &f, const std::string &s, int width);
// A frame t pixels thick just inside the box.
void outline(Canvas &c, int x0, int y0, int x1, int y1, int t, Rgba ink);

} // namespace gdi_canvas
