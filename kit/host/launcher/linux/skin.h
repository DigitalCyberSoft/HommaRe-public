// skin.h - the game's own look for the native launcher on Linux, as on the
// Mac (apple/LauncherSkin.swift): buttons, scrolls, parchment and dialog
// frames read from the player's install (data/*.pak are zip archives of DDS
// textures), never shipped with the app. The first load decodes them and
// bakes them into the user's cache; later launches read those while the paks
// stay the same. Also the splash pictures (BMP) the game rows show.
#pragma once
#include "../launcher_canvas.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace launcher::native {

// Every texture the launcher draws (SkinTexture in LauncherSkin.swift).
enum class Tex {
    Logo,
    Sky,
    Scroll,
    Parchment,
    Page,
    DarkBox,
    Dialog,
    MessageIcon,
    Button,
    ButtonHighlight,
    ButtonPushed,
    ButtonDisabled,
    SmallButton,
    SmallButtonHighlight,
    SmallButtonPushed,
    SmallButtonDisabled,
    OkButton,
    OkButtonHighlight,
    OkButtonPushed,
    OkButtonDisabled,
    RowSelected,
    RowHover,
    Track,
    CheckOff,
    CheckOn,
    Count
};

struct TexInfo {
    const char *name; // the cache's file name, as the Mac's
    const char *path; // inside the paks
    // The part that holds the picture (the game pads textures to powers of
    // two); w == 0 for the logo, which is cut to its opaque pixels.
    int x, y, w, h;
    bool optional; // the launcher can do without it (a plain backdrop, no icon)
    bool additive; // light the game adds onto what is below (its alpha is empty)
};
const TexInfo &tex_info(Tex t);

// Gold (#F6C94C) as fractions: the colour additive textures are lit with.
extern const float kWarmLight[3];

// Reads single entries of a game pak: a plain zip (stored or deflated, no
// zip64), indexed by its central directory with names compared without case.
class PakArchive {
  public:
    bool open(const std::string &path);
    bool read(const std::string &name, std::vector<uint8_t> *out) const;
    size_t size() const {
        return entries_.size();
    }

  private:
    struct Entry {
        uint16_t method;
        uint32_t compressed, size, local_header;
    };
    std::string path_;
    std::vector<std::pair<std::string, Entry>> entries_; // sorted by lower-case name
};

// A DDS texture (DXT1/3/5, or uncompressed pixels described by channel masks)
// cut to `crop` (x, y, w, h) or, with none, to its opaque pixels. With
// `light` the texture is additive: each pixel becomes `light` with the
// pixel's brightness as its alpha (and keeps its whole size). Premultiplied.
bool decode_dds(const uint8_t *data, size_t size, const int *crop, const float *light, Image *out);
// An uncompressed Windows bitmap (24 or 32 bits, or 8 with a palette), as
// the games' splash.bmp files are. Premultiplied, top row first.
bool decode_bmp(const uint8_t *data, size_t size, Image *out);
// A splash picture cut to `crop` (x, y, width, height in 0..1, or empty for
// all of it); nullptr when it cannot be read.
std::shared_ptr<const Image> load_art(const std::string &path, const std::vector<double> &crop);

// The paks of an install, in the order the game reads them (later ones
// override earlier ones: data.pak, then the patches' *-data.pak).
std::vector<std::string> paks(const std::string &root);

struct Skin {
    std::string root; // the install the textures came from
    Image images[int(Tex::Count)];
    const Image &operator[](Tex t) const {
        return images[int(t)];
    }
    bool has(Tex t) const {
        return !images[int(t)].empty();
    }
    // The textures from `root`, from the bake in `cache_base` when the paks
    // have not changed since ("" for none); nullptr when the install lacks
    // any texture that is not optional.
    static std::shared_ptr<const Skin> load(const std::string &root, const std::string &cache_base);
};

} // namespace launcher::native
