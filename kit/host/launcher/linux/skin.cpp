// skin.cpp - see skin.h.
#include "skin.h"

#include "../../../platform/os.h"
#include "../../../runtime/sha256.h"
#include "../../../third_party/miniz/miniz.h"
#include "detect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace launcher::native {

namespace {

const TexInfo kTextures[] = {
    {"logo", "UI/MainMenu2/Logo.(Texture).dds", 0, 0, 0, 0, false, false},
    {"sky", "Arenas/Interface/MainMenu/pPlane1-2.(Texture).dds", 0, 0, 1024, 800, true, false},
    {"scroll", "UI/MainMenu2/SelectCampaign/MenuPanel.(Texture).dds", 0, 0, 328, 506, false, false},
    {"parchment", "UI/MainMenu2/SetupPanel/SetupPanel.(Texture).dds", 0, 1, 342, 112, false, false},
    {"page", "UI/GameOptions/MainBackground.(Texture).dds", 0, 0, 711, 717, false, false},
    {"darkBox", "UI/MainMenu2/SetupPanel/DifficultyList.(Texture).dds", 0, 0, 122, 81, false,
     false},
    {"dialog", "Textures/Interface/CommonBoxes/Box0p/back_texture.dds", 0, 2, 525, 328, false,
     false},
    {"messageIcon", "UI/MessageBox/Message.dds", 4, 1, 124, 126, true, false},
    {"button", "UI/Common/Button_136x49/Active.(Texture).dds", 0, 0, 136, 48, false, false},
    {"buttonHighlight", "UI/Common/Button_136x49/Highlight.(Texture).dds", 0, 0, 136, 49, false,
     true},
    {"buttonPushed", "UI/Common/Button_136x49/Pushed.(Texture).dds", 0, 0, 136, 48, false, false},
    {"buttonDisabled", "UI/Common/Button_136x49/Disabled.(Texture).dds", 0, 0, 136, 48, false,
     false},
    {"smallButton", "UI/Common/DlgButtons/Button_122_34_Normal.(Texture).dds", 0, 0, 122, 34, false,
     false},
    {"smallButtonHighlight", "UI/Common/DlgButtons/Button_122_34_Highlight.(Texture).dds", 0, 0,
     122, 34, false, true},
    {"smallButtonPushed", "UI/Common/DlgButtons/Button_122_34_Click.(Texture).dds", 0, 0, 122, 34,
     false, false},
    {"smallButtonDisabled", "UI/Common/DlgButtons/Button_122_34_Disabled.(Texture).dds", 0, 0, 122,
     34, false, false},
    {"okButton", "Textures/Interface/CommonBoxes/button_ok/Normal.dds", 0, 0, 97, 48, false, false},
    {"okButtonHighlight", "Textures/Interface/CommonBoxes/button_ok/Highlight.dds", 0, 0, 97, 49,
     false, true},
    {"okButtonPushed", "Textures/Interface/CommonBoxes/button_ok/Pushed.dds", 0, 0, 97, 48, false,
     false},
    {"okButtonDisabled", "Textures/Interface/CommonBoxes/button_ok/Disabled.dds", 0, 0, 97, 48,
     false, false},
    {"rowSelected", "UI/MainMenu2/ChangeProfile/ProfileListSelection.(Texture).dds", 0, 0, 382, 21,
     false, true},
    {"rowHover", "UI/MainMenu2/ChangeProfile/ProfileListPreselection.(Texture).dds", 0, 0, 382, 15,
     false, true},
    {"track", "UI/GameOptions/Templates/ScrollBarPart/ScrollBarBg.(Texture).dds", 0, 0, 201, 24,
     false, false},
    {"checkOff", "UI/GameOptions/Templates/CheckBox/Button_0_Normal.(Texture).dds", 0, 0, 25, 25,
     false, false},
    {"checkOn", "UI/GameOptions/Templates/CheckBox/Button_1_Normal.(Texture).dds", 0, 0, 27, 25,
     false, false},
};
static_assert(sizeof kTextures / sizeof kTextures[0] == size_t(Tex::Count),
              "a TexInfo for every Tex");

// The bake's version: a change to decoding makes older bakes stale.
const char *const kCacheVersion = "linux-1";

uint16_t u16(const uint8_t *p) {
    return uint16_t(p[0] | p[1] << 8);
}

uint32_t u32(const uint8_t *p) {
    return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16;
}

std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}

bool read_bytes(const std::string &path, std::vector<uint8_t> *out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    out->clear();
    uint8_t buf[1 << 16];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
        out->insert(out->end(), buf, buf + n);
    fclose(f);
    return true;
}

void mkdirs(const std::string &path) {
    for (size_t i = path.find('/', 1); i != std::string::npos; i = path.find('/', i + 1))
        os_mkdir(path.substr(0, i).c_str());
    os_mkdir(path.c_str());
}

void remove_tree(const std::string &path) {
    OsStat st{};
    if (os_lstat(path.c_str(), &st) != 0)
        return;
    if (!st.is_dir || st.is_symlink) {
        os_unlink(path.c_str());
        return;
    }
    std::vector<std::string> names;
    os_listdir(
        path.c_str(),
        [](const char *name, void *user) {
            static_cast<std::vector<std::string> *>(user)->push_back(name);
            return 0;
        },
        &names);
    for (const std::string &n : names)
        remove_tree(path + "/" + n);
    os_rmdir(path.c_str());
}

void rgb565(uint16_t c, int *r, int *g, int *b) {
    const int r5 = c >> 11 & 31, g6 = c >> 5 & 63, b5 = c & 31;
    *r = r5 << 3 | r5 >> 2;
    *g = g6 << 2 | g6 >> 4;
    *b = b5 << 3 | b5 >> 2;
}

bool decode_blocks(const uint8_t *s, size_t n, int w, int h, int kind, std::vector<uint8_t> &px) {
    const size_t block_bytes = kind == 1 ? 8 : 16;
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (n < size_t(bw) * size_t(bh) * block_bytes)
        return false;
    size_t off = 0;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx, off += block_bytes) {
            uint8_t alpha[16];
            std::fill(alpha, alpha + 16, uint8_t(255));
            size_t c = off;
            if (kind == 3) {
                for (int i = 0; i < 16; ++i) {
                    const uint8_t nib = uint8_t(s[off + size_t(i / 2)] >> ((i % 2) * 4) & 0xf);
                    alpha[i] = uint8_t(nib << 4 | nib);
                }
                c += 8;
            } else if (kind == 5) {
                const int a0 = s[off], a1 = s[off + 1];
                int table[8] = {a0, a1, 0, 0, 0, 0, 0, 0};
                if (a0 > a1) {
                    for (int i = 1; i <= 6; ++i)
                        table[i + 1] = ((7 - i) * a0 + i * a1) / 7;
                } else {
                    for (int i = 1; i <= 4; ++i)
                        table[i + 1] = ((5 - i) * a0 + i * a1) / 5;
                    table[6] = 0;
                    table[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i)
                    bits |= uint64_t(s[off + 2 + size_t(i)]) << (8 * i);
                for (int i = 0; i < 16; ++i)
                    alpha[i] = uint8_t(table[bits >> (3 * i) & 7]);
                c += 8;
            }
            const uint16_t c0 = u16(s + c), c1 = u16(s + c + 2);
            int pal[4][4];
            rgb565(c0, &pal[0][0], &pal[0][1], &pal[0][2]);
            rgb565(c1, &pal[1][0], &pal[1][1], &pal[1][2]);
            pal[0][3] = pal[1][3] = 255;
            for (int k = 0; k < 3; ++k) {
                if (c0 > c1 || kind != 1) {
                    pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
                    pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
                } else {
                    pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
                    pal[3][k] = 0;
                }
            }
            pal[2][3] = 255;
            pal[3][3] = c0 > c1 || kind != 1 ? 255 : 0;
            const uint32_t idx = u32(s + c + 4);
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x >= w || y >= h)
                    continue;
                const int *p = pal[idx >> (2 * i) & 3];
                uint8_t *o = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
                o[0] = uint8_t(p[0]);
                o[1] = uint8_t(p[1]);
                o[2] = uint8_t(p[2]);
                o[3] = kind == 1 ? uint8_t(p[3]) : alpha[i];
            }
        }
    return true;
}

uint8_t channel(uint32_t v, uint32_t mask) {
    if (mask == 0)
        return 255;
    int shift = 0;
    while (!(mask >> shift & 1))
        ++shift;
    const uint64_t max = mask >> shift;
    return uint8_t(((uint64_t((v & mask) >> shift)) * 255 + max / 2) / max);
}

bool decode_masked(const uint8_t *s, size_t n, int w, int h, int bits, const uint32_t masks[4],
                   std::vector<uint8_t> &px) {
    if (bits != 16 && bits != 24 && bits != 32)
        return false;
    const size_t bpp = size_t(bits / 8);
    if (n < size_t(w) * size_t(h) * bpp)
        return false;
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        uint32_t v = 0;
        for (size_t b = 0; b < bpp; ++b)
            v |= uint32_t(s[i * bpp + b]) << (8 * b);
        for (int k = 0; k < 4; ++k)
            px[i * 4 + size_t(k)] = channel(v, masks[k]);
    }
    return true;
}

void opaque_bounds(const std::vector<uint8_t> &px, int w, int h, int *rect) {
    int min_x = w, min_y = h, max_x = -1, max_y = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (px[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3]) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
    if (max_x < 0) {
        rect[0] = rect[1] = 0;
        rect[2] = w;
        rect[3] = h;
    } else {
        rect[0] = min_x;
        rect[1] = min_y;
        rect[2] = max_x - min_x + 1;
        rect[3] = max_y - min_y + 1;
    }
}

// `src` (straight RGBA, w x h) cut to rect and premultiplied into `out`.
void cut(const std::vector<uint8_t> &src, int w, int h, const int *rect, Image *out) {
    int cx = std::min(rect[0], w - 1), cy = std::min(rect[1], h - 1);
    cx = std::max(cx, 0);
    cy = std::max(cy, 0);
    const int cw = std::max(1, std::min(rect[2], w - cx)),
              ch = std::max(1, std::min(rect[3], h - cy));
    out->w = cw;
    out->h = ch;
    out->px.assign(size_t(cw) * size_t(ch) * 4, 0);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            const uint8_t *s = &src[(size_t(y + cy) * size_t(w) + size_t(x + cx)) * 4];
            uint8_t *o = &out->px[(size_t(y) * size_t(cw) + size_t(x)) * 4];
            const int a = s[3];
            o[0] = uint8_t((s[0] * a + 127) / 255);
            o[1] = uint8_t((s[1] * a + 127) / 255);
            o[2] = uint8_t((s[2] * a + 127) / 255);
            o[3] = uint8_t(a);
        }
}

std::string stamp(const std::vector<std::string> &paks) {
    Sha256 h;
    h.update(reinterpret_cast<const uint8_t *>(kCacheVersion), strlen(kCacheVersion));
    for (const std::string &p : paks) {
        OsStat st{};
        os_stat(p.c_str(), &st);
        char line[64];
        snprintf(line, sizeof line, "|%llu|%lld\n", (unsigned long long)st.size,
                 (long long)st.mtime);
        const std::string text = p + line;
        h.update(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    }
    return h.hex().substr(0, 16);
}

// A baked texture: "RGBA", width and height (little-endian u32), then the
// premultiplied pixels.
bool read_baked(const std::string &path, Image *out) {
    std::vector<uint8_t> d;
    if (!read_bytes(path, &d) || d.size() < 12 || memcmp(d.data(), "RGBA", 4) != 0)
        return false;
    const uint32_t w = u32(&d[4]), h = u32(&d[8]);
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || d.size() != 12 + size_t(w) * size_t(h) * 4)
        return false;
    out->w = int(w);
    out->h = int(h);
    out->px.assign(d.begin() + 12, d.end());
    return true;
}

bool write_baked(const std::string &path, const Image &img) {
    FILE *f = fopen((path + ".part").c_str(), "wb");
    if (!f)
        return false;
    uint8_t head[12] = {'R', 'G', 'B', 'A'};
    for (int i = 0; i < 4; ++i) {
        head[4 + i] = uint8_t(uint32_t(img.w) >> (8 * i));
        head[8 + i] = uint8_t(uint32_t(img.h) >> (8 * i));
    }
    const bool ok =
        fwrite(head, 1, 12, f) == 12 && fwrite(img.px.data(), 1, img.px.size(), f) == img.px.size();
    fclose(f);
    return ok && os_rename((path + ".part").c_str(), path.c_str()) == 0;
}

bool read_cache(const std::string &dir, Skin *skin) {
    std::vector<uint8_t> list;
    // The bake lists what it holds: optional pictures may be absent.
    if (!read_bytes(dir + "/textures.txt", &list))
        return false;
    const std::string text(list.begin(), list.end());
    for (int i = 0; i < int(Tex::Count); ++i) {
        const TexInfo &t = kTextures[i];
        bool listed = false;
        for (size_t start = 0; start < text.size();) {
            const size_t nl = std::min(text.find('\n', start), text.size());
            if (text.compare(start, nl - start, t.name) == 0 && strlen(t.name) == nl - start)
                listed = true;
            start = nl + 1;
        }
        if (!listed && t.optional)
            continue;
        if (!read_baked(dir + "/" + t.name + ".rgba", &skin->images[i]))
            return false;
    }
    return true;
}

void write_cache(const Skin &skin, const std::string &base, const std::string &dir) {
    // One stamp at a time: older bakes go.
    remove_tree(base);
    mkdirs(dir);
    std::string list;
    for (int i = 0; i < int(Tex::Count); ++i) {
        if (skin.images[i].empty())
            continue;
        if (!write_baked(dir + "/" + kTextures[i].name + ".rgba", skin.images[i]))
            return;
        list += std::string(list.empty() ? "" : "\n") + kTextures[i].name;
    }
    // Written last: a bake without it is unfinished and is redone.
    FILE *f = fopen((dir + "/textures.txt.part").c_str(), "wb");
    if (!f)
        return;
    const bool ok = fwrite(list.data(), 1, list.size(), f) == list.size();
    fclose(f);
    if (ok)
        os_rename((dir + "/textures.txt.part").c_str(), (dir + "/textures.txt").c_str());
}

} // namespace

const float kWarmLight[3] = {1.0f, 0.82f, 0.31f};

const TexInfo &tex_info(Tex t) {
    return kTextures[int(t)];
}

bool PakArchive::open(const std::string &path) {
    path_ = path;
    entries_.clear();
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    bool ok = false;
    do {
        if (fseeko(f, 0, SEEK_END) != 0)
            break;
        const int64_t end = ftello(f);
        if (end < 22)
            break;
        // The end-of-central-directory record: within the last 64 KiB + 22.
        const int64_t tail_len = std::min<int64_t>(end, 65557);
        std::vector<uint8_t> tail(static_cast<size_t>(tail_len));
        if (fseeko(f, end - tail_len, SEEK_SET) != 0 ||
            fread(tail.data(), 1, tail.size(), f) != tail.size())
            break;
        int64_t eocd = -1;
        for (int64_t i = tail_len - 22; i >= 0; --i)
            if (u32(&tail[size_t(i)]) == 0x06054b50) {
                eocd = i;
                break;
            }
        if (eocd < 0)
            break;
        const uint32_t cd_size = u32(&tail[size_t(eocd) + 12]),
                       cd_offset = u32(&tail[size_t(eocd) + 16]);
        if (cd_offset == 0xffffffffu || int64_t(cd_offset) + int64_t(cd_size) > end)
            break;
        std::vector<uint8_t> cd(cd_size);
        if (fseeko(f, cd_offset, SEEK_SET) != 0 || fread(cd.data(), 1, cd.size(), f) != cd.size())
            break;
        for (size_t p = 0; p + 46 <= cd.size() && u32(&cd[p]) == 0x02014b50;) {
            const size_t name_len = u16(&cd[p + 28]), extra = u16(&cd[p + 30]),
                         comment = u16(&cd[p + 32]);
            if (p + 46 + name_len > cd.size())
                break;
            const std::string name =
                lower(std::string(reinterpret_cast<const char *>(&cd[p + 46]), name_len));
            entries_.push_back(
                {name, {u16(&cd[p + 10]), u32(&cd[p + 20]), u32(&cd[p + 24]), u32(&cd[p + 42])}});
            p += 46 + name_len + extra + comment;
        }
        // A later entry of the same name wins, as in a dictionary.
        std::stable_sort(entries_.begin(), entries_.end(),
                         [](const auto &a, const auto &b) { return a.first < b.first; });
        ok = !entries_.empty();
    } while (false);
    fclose(f);
    return ok;
}

bool PakArchive::read(const std::string &name, std::vector<uint8_t> *out) const {
    const std::string key = lower(name);
    auto it = std::upper_bound(entries_.begin(), entries_.end(), key,
                               [](const std::string &k, const auto &e) { return k < e.first; });
    if (it == entries_.begin() || (it - 1)->first != key)
        return false;
    const Entry &e = (it - 1)->second;
    FILE *f = fopen(path_.c_str(), "rb");
    if (!f)
        return false;
    uint8_t local[30];
    std::vector<uint8_t> raw(e.compressed);
    const bool read =
        fseeko(f, e.local_header, SEEK_SET) == 0 && fread(local, 1, 30, f) == 30 &&
        u32(local) == 0x04034b50 &&
        fseeko(f, off_t(e.local_header) + 30 + u16(local + 26) + u16(local + 28), SEEK_SET) == 0 &&
        fread(raw.data(), 1, raw.size(), f) == raw.size();
    fclose(f);
    if (!read)
        return false;
    if (e.method == 0) {
        if (raw.size() != e.size)
            return false;
        *out = std::move(raw);
        return true;
    }
    if (e.method != 8)
        return false;
    out->assign(e.size, 0);
    const size_t n =
        tinfl_decompress_mem_to_mem(out->data(), out->size(), raw.data(), raw.size(), 0);
    return n == e.size;
}

bool decode_dds(const uint8_t *d, size_t size, const int *crop, const float *light, Image *out) {
    if (size < 128 || u32(d) != 0x20534444 /* "DDS " */)
        return false;
    const int height = int(u32(d + 12)), width = int(u32(d + 16));
    const uint32_t pf_flags = u32(d + 80), four_cc = u32(d + 84);
    const int bits = int(u32(d + 88));
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return false;
    std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4, 0);
    const uint8_t *body = d + 128;
    const size_t body_size = size - 128;
    bool ok = false;
    if (pf_flags & 0x4) {
        switch (four_cc) {
        case 0x31545844:
            ok = decode_blocks(body, body_size, width, height, 1, rgba);
            break;
        case 0x33545844:
            ok = decode_blocks(body, body_size, width, height, 3, rgba);
            break;
        case 0x35545844:
            ok = decode_blocks(body, body_size, width, height, 5, rgba);
            break;
        default:
            break;
        }
    } else {
        const uint32_t masks[4] = {u32(d + 92), u32(d + 96), u32(d + 100),
                                   (pf_flags & 0x1) ? u32(d + 104) : 0};
        ok = decode_masked(body, body_size, width, height, bits, masks, rgba);
    }
    if (!ok)
        return false;
    if (light) {
        for (size_t i = 0; i < rgba.size(); i += 4) {
            const uint8_t v = std::max({rgba[i], rgba[i + 1], rgba[i + 2]});
            rgba[i] = uint8_t(255 * light[0]);
            rgba[i + 1] = uint8_t(255 * light[1]);
            rgba[i + 2] = uint8_t(255 * light[2]);
            rgba[i + 3] = v;
        }
    }
    // An additive texture keeps its whole size (its dark parts are clear now).
    int rect[4];
    if (crop)
        std::copy(crop, crop + 4, rect);
    else if (light) {
        rect[0] = rect[1] = 0;
        rect[2] = width;
        rect[3] = height;
    } else
        opaque_bounds(rgba, width, height, rect);
    cut(rgba, width, height, rect, out);
    return true;
}

bool decode_bmp(const uint8_t *d, size_t size, Image *out) {
    if (size < 26 || d[0] != 'B' || d[1] != 'M')
        return false;
    const uint32_t data_at = u32(d + 10), header = u32(d + 14);
    if (header < 40 || 14 + size_t(header) > size)
        return false;
    const int width = int(int32_t(u32(d + 18)));
    const int raw_height = int(int32_t(u32(d + 22)));
    const int bits = u16(d + 28);
    const uint32_t compression = u32(d + 30);
    uint32_t colors = u32(d + 46);
    const bool top_down = raw_height < 0;
    const int height = top_down ? -raw_height : raw_height;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || compression != 0)
        return false;
    if (bits != 24 && bits != 32 && bits != 8)
        return false;
    const size_t row = (size_t(width) * size_t(bits) / 8 + 3) & ~size_t(3);
    if (size_t(data_at) + row * size_t(height) > size)
        return false;
    const uint8_t *palette = d + 14 + header;
    if (bits == 8) {
        if (colors == 0 || colors > 256)
            colors = 256;
        if (size_t(palette - d) + size_t(colors) * 4 > size)
            return false;
    }
    out->w = width;
    out->h = height;
    out->px.assign(size_t(width) * size_t(height) * 4, 255);
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = d + data_at + row * size_t(top_down ? y : height - 1 - y);
        uint8_t *o = &out->px[size_t(y) * size_t(width) * 4];
        for (int x = 0; x < width; ++x, o += 4) {
            const uint8_t *p;
            if (bits == 8) {
                const uint32_t i = src[x];
                p = i < colors ? palette + i * 4 : palette;
            } else
                p = src + size_t(x) * size_t(bits / 8);
            o[0] = p[2];
            o[1] = p[1];
            o[2] = p[0];
        }
    }
    return true;
}

std::shared_ptr<const Image> load_art(const std::string &path, const std::vector<double> &crop) {
    std::vector<uint8_t> data;
    auto img = std::make_shared<Image>();
    if (!read_bytes(path, &data) || !decode_bmp(data.data(), data.size(), img.get()))
        return nullptr;
    if (crop.size() != 4)
        return img;
    // CGRect.integral: the origin floored, the far edge ceiled.
    const double w = img->w, h = img->h;
    int x0 = int(std::floor(crop[0] * w)), y0 = int(std::floor(crop[1] * h));
    int x1 = int(std::ceil((crop[0] + crop[2]) * w)), y1 = int(std::ceil((crop[1] + crop[3]) * h));
    x0 = std::clamp(x0, 0, img->w);
    y0 = std::clamp(y0, 0, img->h);
    x1 = std::clamp(x1, x0, img->w);
    y1 = std::clamp(y1, y0, img->h);
    if (x1 - x0 <= 0 || y1 - y0 <= 0)
        return img;
    auto cropped = std::make_shared<Image>();
    cropped->w = x1 - x0;
    cropped->h = y1 - y0;
    cropped->px.resize(size_t(cropped->w) * size_t(cropped->h) * 4);
    for (int y = 0; y < cropped->h; ++y)
        memcpy(&cropped->px[size_t(y) * size_t(cropped->w) * 4],
               &img->px[(size_t(y0 + y) * size_t(img->w) + size_t(x0)) * 4],
               size_t(cropped->w) * 4);
    return cropped;
}

std::vector<std::string> paks(const std::string &root) {
    const std::string data = entry(root, "data");
    std::vector<std::string> names;
    if (data.empty())
        return {};
    os_listdir(
        data.c_str(),
        [](const char *name, void *user) {
            const std::string l = lower(name);
            if (l.size() > 4 && l.compare(l.size() - 4, 4, ".pak") == 0 &&
                (l == "data.pak" || (l.size() > 9 && l.compare(l.size() - 9, 9, "-data.pak") == 0)))
                static_cast<std::vector<std::string> *>(user)->push_back(name);
            return 0;
        },
        &names);
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        const std::string la = lower(a), lb = lower(b);
        if (la == "data.pak" || lb == "data.pak")
            return la == "data.pak" && lb != "data.pak";
        return la < lb;
    });
    std::vector<std::string> out;
    for (const std::string &n : names)
        out.push_back(data + "/" + n);
    return out;
}

std::shared_ptr<const Skin> Skin::load(const std::string &root, const std::string &cache_base) {
    const std::vector<std::string> list = paks(root);
    if (list.empty())
        return nullptr;
    auto skin = std::make_shared<Skin>();
    skin->root = root;
    const std::string cache_dir = cache_base.empty() ? "" : cache_base + "/" + stamp(list);
    if (!cache_dir.empty()) {
        if (read_cache(cache_dir, skin.get()))
            return skin;
        skin = std::make_shared<Skin>();
        skin->root = root;
    }
    std::vector<PakArchive> archives;
    for (const std::string &p : list) {
        PakArchive a;
        if (a.open(p))
            archives.push_back(std::move(a));
    }
    std::vector<uint8_t> data;
    for (int i = 0; i < int(Tex::Count); ++i) {
        const TexInfo &t = kTextures[i];
        // The last pak that has the texture wins.
        bool found = false;
        for (auto a = archives.rbegin(); a != archives.rend() && !found; ++a)
            found = a->read(t.path, &data);
        const int rect[4] = {t.x, t.y, t.w, t.h};
        if (!found || !decode_dds(data.data(), data.size(), t.w ? rect : nullptr,
                                  t.additive ? kWarmLight : nullptr, &skin->images[i])) {
            skin->images[i] = {};
            if (t.optional)
                continue;
            fprintf(stderr, "[launcher] skin: no %s in %s\n", t.path, root.c_str());
            return nullptr;
        }
    }
    if (!cache_dir.empty())
        write_cache(*skin, cache_base, cache_dir);
    return skin;
}

} // namespace launcher::native
