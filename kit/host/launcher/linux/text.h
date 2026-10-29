// text.h - TrueType text for the native launcher on Linux, through the kit's
// stb_truetype: the design's faces (apple/LauncherSkinView.swift SkinFont)
// where the system has them, the nearest common ones scaled to match where it
// does not, the kit's own Open Sans last. Characters a face lacks come from
// the next face that has them, so Simplified Chinese takes a CJK face the
// system has (Droid Sans Fallback, WenQuanYi, Noto Sans CJK; not the
// variable CFF2 Noto, which stb_truetype cannot draw).
#pragma once
#include "../launcher_canvas.h"

#include <string>
#include <vector>

namespace launcher::native {

enum class Face {
    Menu,      // the menu's bold italic serif (Cormorant Garamond)
    MenuLight, // its medium italic
    Caps,      // its semibold, for small capitals
    Label,     // the buttons' bold sans (PT Sans)
    Body,      // the descriptions' italic serif (PT Serif)
    Mono,      // paths
    Sans,      // the flat look's system face
    SansBold,
    SerifBold, // the flat look's titles
    Count
};

// A face at a size in pixels, its letters `tracking` pixels further apart.
struct Font {
    Face face = Face::Sans;
    float size = 12;
    float tracking = 0;
};

// Where the fonts are looked for first (the app's resources/fonts), before
// the system's folders. Set before the first text is measured.
void set_font_dir(const std::string &dir);
// The file each face was found in ("" for the kit's own Open Sans), for the log.
std::string face_file(Face face);

float text_width(const std::string &utf8, const Font &font);
// Above and below the baseline, and the line's whole height, in pixels.
float ascent(const Font &font);
float descent(const Font &font);
float line_height(const Font &font);
// Draws `utf8` with its baseline at `y`; returns the width drawn.
float draw_text(Canvas &canvas, float x, float y, const std::string &utf8, const Font &font,
                Color color);

// `utf8` broken into lines no wider than `width`: at spaces, between CJK
// characters, inside a word only when it fits nowhere else.
std::vector<std::string> wrap_text(const std::string &utf8, float width, const Font &font);
enum class Cut { Tail, Middle };
// `utf8` as it fits `width`, an ellipsis where it was cut.
std::string fit_text(const std::string &utf8, float width, const Font &font, Cut cut = Cut::Tail);
// Upper case for the scripts the launcher speaks (Latin, Cyrillic, Greek).
std::string upper(const std::string &utf8);

// The code points of `utf8` (bad bytes as U+FFFD), and back.
std::vector<uint32_t> decode_utf8(const std::string &utf8);
std::string encode_utf8(const std::vector<uint32_t> &cps);

} // namespace launcher::native
