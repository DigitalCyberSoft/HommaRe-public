// native_skin_tests.cpp - the native launcher's game look on Linux
// (launcher/linux/skin.h): pak archives, DDS and BMP decoding, the texture
// table and its bake, over paks built in a scratch folder. No window, no game
// (RECOMP_TEST_GAME_ROOT=<install> also loads a real one).
#include "../launcher/linux/skin.h"

#include "../../platform/os.h"
#include "../../third_party/miniz/miniz.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0, g_checks = 0;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

using namespace launcher;
using namespace launcher::native;

namespace {

std::string g_scratch;

void write_file(const std::string &path, const std::string &bytes) {
    for (size_t i = 1; i < path.size(); ++i)
        if (path[i] == '/')
            os_mkdir(path.substr(0, i).c_str());
    FILE *f = fopen(path.c_str(), "wb");
    if (f) {
        fwrite(bytes.data(), 1, bytes.size(), f);
        fclose(f);
    }
}

bool exists(const std::string &path) {
    OsStat st{};
    return os_stat(path.c_str(), &st) == 0;
}

void remove_tree(const std::string &path) {
    OsStat st{};
    if (os_lstat(path.c_str(), &st) != 0)
        return;
    if (st.is_dir && !st.is_symlink) {
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
    } else {
        os_unlink(path.c_str());
    }
}

std::string fresh(const char *name) {
    const std::string dir = g_scratch + "/" + name;
    remove_tree(dir);
    os_mkdir(dir.c_str());
    return dir;
}

void put16(std::string &s, size_t at, uint32_t v) {
    s[at] = char(v & 0xff);
    s[at + 1] = char(v >> 8 & 0xff);
}

void put32(std::string &s, size_t at, uint32_t v) {
    put16(s, at, v & 0xffff);
    put16(s, at + 2, v >> 16);
}

// A DDS header for a w x h texture: block-compressed with `four_cc` ("DXT1"),
// or uncompressed with `bits` and the channel masks.
std::string dds(int w, int h, const char *four_cc, int bits = 0, uint32_t r = 0, uint32_t g = 0,
                uint32_t b = 0, uint32_t a = 0) {
    std::string s(128, '\0');
    memcpy(&s[0], "DDS ", 4);
    put32(s, 4, 124);
    put32(s, 12, uint32_t(h));
    put32(s, 16, uint32_t(w));
    put32(s, 76, 32);
    if (four_cc) {
        put32(s, 80, 0x4);
        memcpy(&s[84], four_cc, 4);
    } else {
        put32(s, 80, 0x40 | (a ? 0x1 : 0));
        put32(s, 88, uint32_t(bits));
        put32(s, 92, r);
        put32(s, 96, g);
        put32(s, 100, b);
        put32(s, 104, a);
    }
    return s;
}

// A 32-bit DDS of straight-alpha pixels (r, g, b, a each).
std::string dds32(int w, int h, const std::vector<uint32_t> &rgba) {
    std::string s = dds(w, h, nullptr, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    for (uint32_t p : rgba) {
        std::string px(4, '\0');
        px[0] = char(p >> 8 & 0xff);  // b
        px[1] = char(p >> 16 & 0xff); // g
        px[2] = char(p >> 24 & 0xff); // r
        px[3] = char(p & 0xff);       // a
        s += px;
    }
    return s;
}

const uint8_t *px(const Image &img, int x, int y) {
    return &img.px[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
}

bool is(const Image &img, int x, int y, int r, int g, int b, int a) {
    const uint8_t *p = px(img, x, y);
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

bool decode(const std::string &d, Image *out, const int *crop = nullptr,
            const float *light = nullptr) {
    return decode_dds(reinterpret_cast<const uint8_t *>(d.data()), d.size(), crop, light, out);
}

const int kWhole[4] = {0, 0, 4, 4};

void make_pak(const std::string &path,
              const std::vector<std::pair<std::string, std::string>> &files, mz_uint level) {
    for (size_t i = 1; i < path.size(); ++i)
        if (path[i] == '/')
            os_mkdir(path.substr(0, i).c_str());
    mz_zip_archive zip{};
    CHECK(mz_zip_writer_init_file(&zip, path.c_str(), 0));
    for (const auto &f : files)
        CHECK(
            mz_zip_writer_add_mem(&zip, f.first.c_str(), f.second.data(), f.second.size(), level));
    CHECK(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);
}

std::string bytes(const std::vector<uint8_t> &v) {
    return std::string(v.begin(), v.end());
}

void test_pak() {
    const std::string dir = fresh("pak");
    const std::string big(20000, 'x');
    make_pak(dir + "/stored.pak", {{"UI/Foo.(Texture).dds", "one"}, {"Big/Thing.TXT", big}}, 0);
    make_pak(dir + "/deflated.pak", {{"UI/Foo.(Texture).dds", "two"}, {"Big/Thing.TXT", big}},
             MZ_BEST_COMPRESSION);
    for (const char *name : {"stored.pak", "deflated.pak"}) {
        PakArchive pak;
        CHECK(pak.open(dir + "/" + name));
        CHECK(pak.size() == 2);
        std::vector<uint8_t> out;
        CHECK(pak.read("ui/foo.(texture).DDS", &out));
        CHECK(bytes(out) == (strcmp(name, "stored.pak") == 0 ? "one" : "two"));
        CHECK(pak.read("big/thing.txt", &out) && bytes(out) == big);
        CHECK(!pak.read("UI/Missing.dds", &out));
        CHECK(!pak.read("UI/Foo", &out));
    }
    write_file(dir + "/not.pak", std::string(100, 'z'));
    PakArchive pak;
    CHECK(!pak.open(dir + "/not.pak"));
    CHECK(!pak.open(dir + "/missing.pak"));
}

void test_dxt() {
    Image img;
    // DXT1, four colours (c0 > c1): red, blue and the two thirds between.
    std::string d = dds(4, 4, "DXT1");
    std::string block(8, '\0');
    put16(block, 0, 0xf800);
    put16(block, 2, 0x001f);
    put32(block, 4, 0 | 1 << 2 | 2 << 4 | 3 << 6);
    CHECK(decode(d + block, &img, kWhole));
    CHECK(img.w == 4 && img.h == 4);
    CHECK(is(img, 0, 0, 255, 0, 0, 255));
    CHECK(is(img, 1, 0, 0, 0, 255, 255));
    CHECK(is(img, 2, 0, 170, 0, 85, 255));
    CHECK(is(img, 3, 0, 85, 0, 170, 255));
    CHECK(is(img, 0, 1, 255, 0, 0, 255));
    // DXT1 with one-bit alpha (c0 <= c1): the mean, and clear.
    put16(block, 0, 0x001f);
    put16(block, 2, 0xf800);
    CHECK(decode(d + block, &img, kWhole));
    CHECK(is(img, 2, 0, 127, 0, 127, 255));
    CHECK(is(img, 3, 0, 0, 0, 0, 0));
    // Too short a body.
    CHECK(!decode(d + block.substr(0, 4), &img, kWhole));

    // DXT3: explicit 4-bit alpha, premultiplied white.
    std::string d3 = dds(4, 4, "DXT3");
    std::string b3(16, '\0');
    b3[0] = char(0xf0); // pixel 0 alpha 0, pixel 1 alpha 15
    b3[1] = char(0x08); // pixel 2 alpha 8
    put16(b3, 8, 0xffff);
    put16(b3, 10, 0x0000);
    CHECK(decode(d3 + b3, &img, kWhole));
    CHECK(is(img, 0, 0, 0, 0, 0, 0));
    CHECK(is(img, 1, 0, 255, 255, 255, 255));
    CHECK(is(img, 2, 0, 136, 136, 136, 136));

    // DXT5: interpolated alpha, eight steps.
    std::string d5 = dds(4, 4, "DXT5");
    std::string b5(16, '\0');
    b5[0] = char(255);
    b5[1] = 0;
    const uint64_t bits = 0 | 1 << 3 | 2 << 6 | uint64_t(7) << 9;
    for (int i = 0; i < 6; ++i)
        b5[2 + size_t(i)] = char(bits >> (8 * i) & 0xff);
    put16(b5, 8, 0xffff);
    CHECK(decode(d5 + b5, &img, kWhole));
    CHECK(is(img, 0, 0, 255, 255, 255, 255));
    CHECK(is(img, 1, 0, 0, 0, 0, 0));
    CHECK(is(img, 2, 0, 218, 218, 218, 218));
    CHECK(is(img, 3, 0, 36, 36, 36, 36));
    // a0 <= a1: six steps, then 0 and 255.
    b5[0] = 0;
    b5[1] = char(255);
    const uint64_t bits2 = 6 | uint64_t(7) << 3 | uint64_t(2) << 6;
    for (int i = 0; i < 6; ++i)
        b5[2 + size_t(i)] = char(bits2 >> (8 * i) & 0xff);
    CHECK(decode(d5 + b5, &img, kWhole));
    CHECK(is(img, 0, 0, 0, 0, 0, 0));
    CHECK(is(img, 1, 0, 255, 255, 255, 255));
    CHECK(is(img, 2, 0, 51, 51, 51, 51));

    // Not a DDS, an unknown four-cc.
    CHECK(!decode(std::string(200, 'x'), &img));
    CHECK(!decode(dds(4, 4, "ATI2") + b5, &img, kWhole));
}

void test_masked() {
    Image img;
    // 32 bits with alpha: premultiplied.
    CHECK(decode(dds32(2, 1, {0x11223380, 0xffffffff}), &img));
    CHECK(img.w == 2 && img.h == 1);
    CHECK(is(img, 0, 0, (0x11 * 128 + 127) / 255, (0x22 * 128 + 127) / 255,
             (0x33 * 128 + 127) / 255, 128));
    CHECK(is(img, 1, 0, 255, 255, 255, 255));
    // 16-bit 565 without alpha: opaque.
    std::string d = dds(2, 1, nullptr, 16, 0xf800, 0x07e0, 0x001f, 0);
    std::string body(4, '\0');
    put16(body, 0, 0xf800);
    put16(body, 2, 0x07e0);
    CHECK(decode(d + body, &img));
    CHECK(is(img, 0, 0, 255, 0, 0, 255));
    CHECK(is(img, 1, 0, 0, 255, 0, 255));
    // 24 bits.
    std::string d24 = dds(1, 1, nullptr, 24, 0xff0000, 0x00ff00, 0x0000ff, 0);
    CHECK(decode(d24 + std::string("\x30\x20\x10", 3), &img));
    CHECK(is(img, 0, 0, 0x10, 0x20, 0x30, 255));
    CHECK(!decode(dds(1, 1, nullptr, 8, 0xe0, 0x1c, 0x03, 0) + "x", &img));
    CHECK(!decode(dds32(2, 2, {0, 0, 0}), &img)); // a pixel short
}

void test_crop_and_light() {
    Image img;
    // Without a crop: cut to the opaque pixels (the logo).
    std::vector<uint32_t> pixels(16, 0x00000000);
    pixels[1 * 4 + 2] = 0xff0000ff;
    pixels[2 * 4 + 3] = 0x00ff00ff;
    CHECK(decode(dds32(4, 4, pixels), &img));
    CHECK(img.w == 2 && img.h == 2);
    CHECK(is(img, 0, 0, 255, 0, 0, 255));
    CHECK(is(img, 1, 1, 0, 255, 0, 255));
    CHECK(is(img, 1, 0, 0, 0, 0, 0));
    // Nothing opaque: all of it.
    CHECK(decode(dds32(4, 4, std::vector<uint32_t>(16, 0)), &img));
    CHECK(img.w == 4 && img.h == 4);
    // A crop, clamped to the texture.
    const int crop[4] = {2, 1, 100, 100};
    CHECK(decode(dds32(4, 4, pixels), &img, crop));
    CHECK(img.w == 2 && img.h == 3);
    CHECK(is(img, 0, 0, 255, 0, 0, 255));
    // Additive: the light's colour, the brightness as alpha, the whole size.
    std::vector<uint32_t> dark(16, 0x000000ff);
    dark[0] = 0x643214ff; // brightness 100
    CHECK(decode(dds32(4, 4, dark), &img, nullptr, kWarmLight));
    CHECK(img.w == 4 && img.h == 4);
    CHECK(is(img, 0, 0, 100, (209 * 100 + 127) / 255, (79 * 100 + 127) / 255, 100));
    CHECK(is(img, 1, 0, 0, 0, 0, 0));
}

// A BMP of w x h straight RGB pixels (r, g, b each byte of `rgb`).
std::string bmp24(int w, int h, const std::vector<uint32_t> &rgb, bool top_down) {
    const size_t row = (size_t(w) * 3 + 3) & ~size_t(3);
    std::string s(54, '\0');
    s[0] = 'B';
    s[1] = 'M';
    put32(s, 2, uint32_t(54 + row * size_t(h)));
    put32(s, 10, 54);
    put32(s, 14, 40);
    put32(s, 18, uint32_t(w));
    put32(s, 22, uint32_t(top_down ? -h : h));
    put16(s, 26, 1);
    put16(s, 28, 24);
    for (int r = 0; r < h; ++r) {
        const int y = top_down ? r : h - 1 - r;
        std::string line(row, '\0');
        for (int x = 0; x < w; ++x) {
            const uint32_t p = rgb[size_t(y) * size_t(w) + size_t(x)];
            line[size_t(x) * 3] = char(p & 0xff);
            line[size_t(x) * 3 + 1] = char(p >> 8 & 0xff);
            line[size_t(x) * 3 + 2] = char(p >> 16 & 0xff);
        }
        s += line;
    }
    return s;
}

bool decode_b(const std::string &d, Image *out) {
    return decode_bmp(reinterpret_cast<const uint8_t *>(d.data()), d.size(), out);
}

void test_bmp() {
    std::vector<uint32_t> rgb;
    for (int i = 0; i < 6; ++i)
        rgb.push_back(uint32_t(i * 40) << 16 | uint32_t(i) << 8 | 0x07);
    for (bool top_down : {false, true}) {
        Image img;
        CHECK(decode_b(bmp24(3, 2, rgb, top_down), &img));
        CHECK(img.w == 3 && img.h == 2);
        CHECK(is(img, 0, 0, 0, 0, 7, 255));
        CHECK(is(img, 2, 0, 80, 2, 7, 255));
        CHECK(is(img, 0, 1, 120, 3, 7, 255));
        CHECK(is(img, 2, 1, 200, 5, 7, 255));
    }
    // 8 bits with a palette, five wide (three bytes of padding a row).
    std::string s(54 + 4 * 3, '\0');
    s[0] = 'B';
    s[1] = 'M';
    put32(s, 10, uint32_t(s.size()));
    put32(s, 14, 40);
    put32(s, 18, 5);
    put32(s, 22, 2);
    put16(s, 26, 1);
    put16(s, 28, 8);
    put32(s, 46, 3);
    const uint8_t pal[3][3] = {{10, 20, 30}, {40, 50, 60}, {70, 80, 90}}; // r, g, b
    for (int i = 0; i < 3; ++i) {
        s[54 + size_t(i) * 4] = char(pal[i][2]);
        s[54 + size_t(i) * 4 + 1] = char(pal[i][1]);
        s[54 + size_t(i) * 4 + 2] = char(pal[i][0]);
    }
    s += std::string("\x02\x02\x02\x02\x02\0\0\0", 8); // the bottom row
    s += std::string("\x00\x01\x02\x01\x00\0\0\0", 8); // the top row
    Image img;
    CHECK(decode_b(s, &img));
    CHECK(img.w == 5 && img.h == 2);
    CHECK(is(img, 0, 0, 10, 20, 30, 255));
    CHECK(is(img, 1, 0, 40, 50, 60, 255));
    CHECK(is(img, 2, 0, 70, 80, 90, 255));
    CHECK(is(img, 4, 1, 70, 80, 90, 255));
    // Truncated, compressed, not a bitmap.
    CHECK(!decode_b(bmp24(3, 2, rgb, false).substr(0, 60), &img));
    std::string rle = bmp24(3, 2, rgb, false);
    put32(rle, 30, 1);
    CHECK(!decode_b(rle, &img));
    CHECK(!decode_b("not a bitmap at all, not at all......", &img));

    // load_art: the crop as CGRect.integral has it.
    std::vector<uint32_t> rows;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            rows.push_back(uint32_t(y) << 16 | uint32_t(x));
    const std::string dir = fresh("art");
    write_file(dir + "/splash.bmp", bmp24(10, 10, rows, false));
    auto whole = load_art(dir + "/splash.bmp", {});
    CHECK(whole && whole->w == 10 && whole->h == 10);
    auto art = load_art(dir + "/splash.bmp", {0, 0.19, 1, 0.63});
    CHECK(art && art->w == 10 && art->h == 8); // rows 1 (1.9 floored) to 9 (8.2 ceiled)
    CHECK(art && is(*art, 3, 0, 1, 0, 3, 255));
    CHECK(art && is(*art, 0, 7, 8, 0, 0, 255));
    CHECK(!load_art(dir + "/missing.bmp", {}));
}

void test_paks() {
    const std::string root = fresh("paks");
    for (const char *name :
         {"data.pak", "b-data.pak", "A-data.pak", "texts.pak", "p3-texts.pak", "notes.txt"})
        write_file(root + "/Data/" + name, "x");
    const std::vector<std::string> p = paks(root);
    CHECK(p.size() == 3);
    CHECK(p.size() == 3 && p[0] == root + "/Data/data.pak" && p[1] == root + "/Data/A-data.pak" &&
          p[2] == root + "/Data/b-data.pak");
    CHECK(paks(root + "/nothing").empty());
}

// A fake install with every texture as a tiny DDS: the scroll red in
// data.pak, blue in the patch's z-data.pak (which wins); the optional ones
// left out.
std::string make_skin_install(const char *name, bool with_sky, bool with_parchment = true) {
    const std::string root = fresh(name);
    std::vector<std::pair<std::string, std::string>> base, patch;
    for (int i = 0; i < int(Tex::Count); ++i) {
        const Tex t = Tex(i);
        const TexInfo &info = tex_info(t);
        if ((t == Tex::Sky && !with_sky) || t == Tex::MessageIcon ||
            (t == Tex::Parchment && !with_parchment))
            continue;
        uint32_t colour = 0x808080ff;
        if (t == Tex::Scroll)
            colour = 0xff0000ff;
        if (t == Tex::Logo)
            colour = 0x00ff00ff;
        std::vector<uint32_t> pixels(16, colour);
        if (t == Tex::Logo)
            std::fill(pixels.begin(), pixels.begin() + 4, 0); // a clear top row, cut away
        // Mixed-case names, as some paks spell them.
        std::string path = info.path;
        if (i % 2)
            for (char &c : path)
                c = char(c >= 'a' && c <= 'z' ? c - 32 : c);
        base.push_back({path, dds32(4, 4, pixels)});
    }
    patch.push_back(
        {tex_info(Tex::Scroll).path, dds32(4, 4, std::vector<uint32_t>(16, 0x0000ffff))});
    make_pak(root + "/data/data.pak", base, MZ_BEST_SPEED);
    make_pak(root + "/data/z-data.pak", patch, 0);
    return root;
}

bool same(const Skin &a, const Skin &b) {
    for (int i = 0; i < int(Tex::Count); ++i)
        if (a.images[i].w != b.images[i].w || a.images[i].h != b.images[i].h ||
            a.images[i].px != b.images[i].px)
            return false;
    return true;
}

void test_skin_load() {
    CHECK(std::string(tex_info(Tex::CheckOn).name) == "checkOn");
    CHECK(tex_info(Tex::Sky).optional && tex_info(Tex::MessageIcon).optional &&
          !tex_info(Tex::Logo).optional);
    CHECK(tex_info(Tex::RowHover).additive && !tex_info(Tex::Button).additive);

    const std::string root = make_skin_install("skin", true);
    auto skin = Skin::load(root, "");
    CHECK(skin != nullptr);
    if (!skin)
        return;
    CHECK(skin->root == root);
    CHECK(skin->has(Tex::Sky) && !skin->has(Tex::MessageIcon));
    CHECK(is((*skin)[Tex::Scroll], 0, 0, 0, 0, 255, 255));         // the patch's
    CHECK((*skin)[Tex::Logo].w == 4 && (*skin)[Tex::Logo].h == 3); // the clear corner cut away
    CHECK((*skin)[Tex::Parchment].w == 4 &&
          (*skin)[Tex::Parchment].h == 3); // rect (0, 1, 342, 112), clamped
    CHECK(is((*skin)[Tex::RowHover], 0, 0, 128, (209 * 128 + 127) / 255, (79 * 128 + 127) / 255,
             128));

    // Baked, then read back from the bake: the paks' bytes no longer matter
    // while their sizes and dates stay.
    const std::string cache = g_scratch + "/skin-cache";
    remove_tree(cache);
    auto first = Skin::load(root, cache);
    CHECK(first && same(*first, *skin));
    std::vector<std::string> bakes;
    os_listdir(
        cache.c_str(),
        [](const char *name, void *user) {
            static_cast<std::vector<std::string> *>(user)->push_back(name);
            return 0;
        },
        &bakes);
    CHECK(bakes.size() == 1);
    CHECK(bakes.size() == 1 && exists(cache + "/" + bakes[0] + "/textures.txt") &&
          exists(cache + "/" + bakes[0] + "/scroll.rgba") &&
          !exists(cache + "/" + bakes[0] + "/messageIcon.rgba"));
    OsStat st{};
    os_stat((root + "/data/data.pak").c_str(), &st);
    write_file(root + "/data/data.pak", std::string(size_t(st.size), '\0'));
    os_set_mtime((root + "/data/data.pak").c_str(), st.mtime);
    CHECK(Skin::load(root, "") == nullptr);
    auto cached = Skin::load(root, cache);
    CHECK(cached && same(*cached, *skin));
    // A changed pak makes a new bake; the old one goes.
    write_file(root + "/data/data.pak", "changed");
    CHECK(Skin::load(root, cache) == nullptr);
    const std::string other = make_skin_install("skin-other", false);
    auto toe = Skin::load(other, cache);
    CHECK(toe && !toe->has(Tex::Sky));
    bakes.clear();
    os_listdir(
        cache.c_str(),
        [](const char *name, void *user) {
            static_cast<std::vector<std::string> *>(user)->push_back(name);
            return 0;
        },
        &bakes);
    CHECK(bakes.size() == 1);
    // The bake without the optional sky reads back without it.
    auto toe_cached = Skin::load(other, cache);
    CHECK(toe_cached && same(*toe_cached, *toe) && !toe_cached->has(Tex::Sky));

    // A required texture missing: no skin. No paks at all: none either.
    CHECK(Skin::load(make_skin_install("skin-short", true, false), "") == nullptr);
    CHECK(Skin::load(fresh("skin-empty"), "") == nullptr);
}

void test_real_skin() {
    const char *root = getenv("RECOMP_TEST_GAME_ROOT");
    if (!root || !*root) {
        printf("(RECOMP_TEST_GAME_ROOT not set: the real skin is skipped)\n");
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    auto skin = Skin::load(root, "");
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CHECK(skin != nullptr);
    if (!skin)
        return;
    printf("(real skin from %s in %.0f ms)\n", root, ms);
    for (int i = 0; i < int(Tex::Count); ++i) {
        const TexInfo &t = tex_info(Tex(i));
        if (t.optional && !skin->has(Tex(i)))
            continue;
        CHECK(skin->has(Tex(i)));
        if (t.w)
            CHECK(skin->images[i].w == t.w && skin->images[i].h == t.h);
    }
}

} // namespace

int main() {
    const char *base = getenv("RECOMP_LAUNCHER_TEST_DIR");
    g_scratch = std::string(base && *base ? base : "build/recomp") + "/native-skin-test";
    for (size_t i = g_scratch.find('/'); i != std::string::npos; i = g_scratch.find('/', i + 1))
        os_mkdir(g_scratch.substr(0, i).c_str());
    remove_tree(g_scratch);
    os_mkdir(g_scratch.c_str());

    struct Test {
        const char *name;
        void (*run)();
    } tests[] = {
        {"pak archives", test_pak},
        {"dxt blocks", test_dxt},
        {"masked pixels", test_masked},
        {"crop and light", test_crop_and_light},
        {"bitmaps and art", test_bmp},
        {"pak order", test_paks},
        {"skin and its bake", test_skin_load},
        {"real skin", test_real_skin},
    };
    for (const Test &t : tests) {
        const int before = g_failures;
        t.run();
        printf("%-28s %s\n", t.name, g_failures == before ? "ok" : "FAILED");
    }
    printf("%d checks, %d failures\n", g_checks, g_failures);
    remove_tree(g_scratch);
    return g_failures ? 1 : 0;
}
