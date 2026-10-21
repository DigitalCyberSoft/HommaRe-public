// text_overrides.cpp - a game's own texts with a few words changed (game.toml
// [texts]): "Exit to Windows" is not where a Mac or an iPad goes. The files
// may be plain text too (a sound's .xdb description, its volume).
//
// The texts come from the player's install, so each language keeps its own
// words; the replacements only touch what they name. They are written as a
// pak into the profile, the asset overlay's first tier, so the game finds it
// beside its own paks in data/ and never sees its install changed. Its
// entries carry a date later than any the game shipped with: where two paks
// hold the same file, the game takes the newer one.
#include "text_overrides.h"

#include "../third_party/miniz/miniz.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < s.size()) {
        size_t end = s.find(sep, at);
        if (end == std::string::npos)
            end = s.size();
        if (end > at)
            out.push_back(s.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

std::string lower(std::string s) {
    for (char &c : s)
        c = (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    return s;
}

// The game's texts are UTF-16 LE with a byte order mark; the config is UTF-8.
std::string utf16_to_utf8(const std::string &bytes) {
    std::string out;
    size_t i = bytes.size() >= 2 && (uint8_t)bytes[0] == 0xff && (uint8_t)bytes[1] == 0xfe ? 2 : 0;
    for (; i + 1 < bytes.size(); i += 2) {
        uint32_t c = (uint8_t)bytes[i] | (uint32_t)(uint8_t)bytes[i + 1] << 8;
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < bytes.size()) {
            const uint32_t lo = (uint8_t)bytes[i + 2] | (uint32_t)(uint8_t)bytes[i + 3] << 8;
            if (lo >= 0xdc00 && lo < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                i += 2;
            }
        }
        if (c < 0x80) {
            out += (char)c;
        } else if (c < 0x800) {
            out += (char)(0xc0 | c >> 6);
            out += (char)(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += (char)(0xe0 | c >> 12);
            out += (char)(0x80 | (c >> 6 & 0x3f));
            out += (char)(0x80 | (c & 0x3f));
        } else {
            out += (char)(0xf0 | c >> 18);
            out += (char)(0x80 | (c >> 12 & 0x3f));
            out += (char)(0x80 | (c >> 6 & 0x3f));
            out += (char)(0x80 | (c & 0x3f));
        }
    }
    return out;
}

std::string utf8_to_utf16(const std::string &s) {
    std::string out = "\xff\xfe";
    auto put = [&](uint32_t u) {
        out += (char)(u & 0xff);
        out += (char)(u >> 8);
    };
    for (size_t i = 0; i < s.size();) {
        const uint8_t b = (uint8_t)s[i];
        uint32_t c;
        int n;
        if (b < 0x80) {
            c = b;
            n = 1;
        } else if ((b & 0xe0) == 0xc0) {
            c = b & 0x1f;
            n = 2;
        } else if ((b & 0xf0) == 0xe0) {
            c = b & 0x0f;
            n = 3;
        } else {
            c = b & 0x07;
            n = 4;
        }
        for (int k = 1; k < n && i + k < s.size(); ++k)
            c = c << 6 | ((uint8_t)s[i + k] & 0x3f);
        i += n;
        if (c >= 0x10000) {
            c -= 0x10000;
            put(0xd800 + (c >> 10));
            put(0xdc00 + (c & 0x3ff));
        } else {
            put(c);
        }
    }
    return out;
}

// The game's pak reader takes an entry's sizes from its local header, so
// they go there, not in a descriptor after the data (miniz's in-memory add
// writes only that); names are plain ASCII, as the game's own paks have them.
struct MemoryFile {
    const std::string *bytes;
};
size_t read_memory(void *opaque, mz_uint64 ofs, void *buf, size_t n) {
    const std::string &b = *static_cast<MemoryFile *>(opaque)->bytes;
    if (ofs >= b.size())
        return 0;
    n = std::min(n, (size_t)(b.size() - ofs));
    memcpy(buf, b.data() + ofs, n);
    return n;
}

struct Found {
    MZ_TIME_T time = 0;
    std::string bytes;
};

// Each wanted file from the newest pak entry that holds it, as the game picks.
std::map<std::string, Found> newest(const fs::path &data, const std::vector<std::string> &files) {
    std::map<std::string, Found> found;
    std::error_code ec;
    std::vector<fs::path> paks;
    for (const auto &e : fs::directory_iterator(data, ec))
        if (lower(e.path().extension().string()) == ".pak")
            paks.push_back(e.path());
    std::sort(paks.begin(), paks.end());
    for (const fs::path &pak : paks) {
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, pak.string().c_str(), 0))
            continue;
        for (const std::string &name : files) {
            const int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
            mz_zip_archive_file_stat st;
            if (index < 0 || !mz_zip_reader_file_stat(&zip, (mz_uint)index, &st))
                continue;
            Found &f = found[name];
            if (!f.bytes.empty() && st.m_time < f.time)
                continue;
            size_t size = 0;
            void *p = mz_zip_reader_extract_to_heap(&zip, (mz_uint)index, &size, 0);
            if (!p)
                continue;
            f.time = st.m_time;
            f.bytes.assign((const char *)p, size);
            mz_free(p);
        }
        mz_zip_reader_end(&zip);
    }
    return found;
}

} // namespace

void host_text_overrides(const std::string &game_root, const std::string &profile_dir,
                         const char *pak, const char *file_list, const char *replace_list) {
    if (!pak || !*pak || game_root.empty() || profile_dir.empty())
        return;
    const std::vector<std::string> files = split(file_list ? file_list : "", ';');
    std::vector<std::pair<std::string, std::string>> words;
    for (const std::string &rec : split(replace_list ? replace_list : "", ';')) {
        const size_t bar = rec.find('|');
        if (bar != std::string::npos && bar > 0)
            words.emplace_back(rec.substr(0, bar), rec.substr(bar + 1));
    }
    const fs::path out = fs::path(profile_dir) / pak;
    // Only the data/ beside the game's own paks; the pak itself never counts.
    const fs::path data = fs::path(game_root) / fs::path(pak).parent_path();
    std::map<std::string, Found> found = newest(data, files);

    // A fixed date, later than any the game shipped with, keeps the pak's bytes
    // the same from one start to the next.
    std::tm when{};
    when.tm_year = 2030 - 1900;
    when.tm_mon = 0;
    when.tm_mday = 1;
    when.tm_hour = 12;
    MZ_TIME_T stamp = (MZ_TIME_T)std::mktime(&when);

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0))
        return;
    int changed = 0;
    for (const std::string &name : files) {
        auto it = found.find(name);
        if (it == found.end() || it->second.bytes.empty())
            continue;
        // A text with a byte order mark is UTF-16; the rest (an .xdb's XML)
        // is ASCII or UTF-8 and changes as it is.
        const std::string &raw = it->second.bytes;
        const bool wide = raw.size() >= 2 && (uint8_t)raw[0] == 0xff && (uint8_t)raw[1] == 0xfe;
        std::string text = wide ? utf16_to_utf8(raw) : raw;
        bool any = false;
        for (const auto &w : words)
            for (size_t at = text.find(w.first); at != std::string::npos;
                 at = text.find(w.first, at + w.second.size())) {
                text.replace(at, w.first.size(), w.second);
                any = true;
            }
        if (!any)
            continue; // a text this install words differently stays the game's
        const std::string bytes = wide ? utf8_to_utf16(text) : text;
        MemoryFile file{&bytes};
        if (mz_zip_writer_add_read_buf_callback(
                &zip, name.c_str(), read_memory, &file, bytes.size(), &stamp, nullptr, 0,
                (mz_uint)MZ_DEFAULT_LEVEL | MZ_ZIP_FLAG_WRITE_HEADER_SET_SIZE | MZ_ZIP_FLAG_ASCII_FILENAME,
                nullptr, 0, nullptr, 0))
            ++changed;
    }
    void *buf = nullptr;
    size_t size = 0;
    const bool built = mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    std::string next = built && changed ? std::string((const char *)buf, size) : std::string();
    if (buf)
        mz_free(buf); // the finished archive is the caller's
    mz_zip_writer_end(&zip);

    std::error_code ec;
    if (next.empty()) {
        fs::remove(out, ec); // nothing to change in this install
        return;
    }
    std::string old;
    if (FILE *f = fopen(out.string().c_str(), "rb")) {
        char chunk[4096];
        for (size_t n; (n = fread(chunk, 1, sizeof chunk, f)) > 0;)
            old.append(chunk, n);
        fclose(f);
    }
    if (old == next)
        return;
    fs::create_directories(out.parent_path(), ec);
    if (FILE *f = fopen(out.string().c_str(), "wb")) {
        fwrite(next.data(), 1, next.size(), f);
        fclose(f);
        fprintf(stderr, "[host] texts: %d reworded into %s\n", changed, out.string().c_str());
    }
}
