// recomp_innoextract.cpp - the C entry point of innoextract as a library
// (recomp_innoextract.h), in place of its command line (src/cli/main.cpp).
// Each call fills innoextract's extract_options the way main() does for the
// Mac helper's arguments and runs process_file(); what innoextract prints on
// stdout (file names) is read line by line, what it prints on stderr becomes
// the error text, and its progress bar's update (patched, lib-patches/) is the
// progress report and the cancel point.
#include "recomp_innoextract.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

#include "cli/extract.hpp"
#include "setup/version.hpp"
#include "util/console.hpp"
#include "util/log.hpp"

namespace {

std::mutex call_lock;

// Hands each complete line written to it to `fn`.
class line_buf : public std::streambuf {
public:
    explicit line_buf(std::function<void(const std::string &)> fn) : fn_(std::move(fn)) {}
    void finish() {
        if (!line_.empty()) {
            fn_(line_);
            line_.clear();
        }
    }

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        put(traits_type::to_char_type(c));
        return c;
    }
    std::streamsize xsputn(const char *s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; i++) put(s[i]);
        return n;
    }

private:
    void put(char c) {
        if (c == '\n') {
            fn_(line_);
            line_.clear();
        } else if (c != '\r') {
            line_.push_back(c);
        }
    }
    std::function<void(const std::string &)> fn_;
    std::string line_;
};

// Points std::cout and std::cerr at line buffers for one call; stderr's
// lines are kept for the error text.
class capture {
public:
    explicit capture(std::function<void(const std::string &)> out)
        : out_(std::move(out)), err_([this](const std::string &l) {
              if (!l.empty()) err_lines.push_back(l);
          }) {
        old_out_ = std::cout.rdbuf(&out_);
        old_err_ = std::cerr.rdbuf(&err_);
    }
    ~capture() {
        std::cout.flush();
        std::cerr.flush();
        out_.finish();
        err_.finish();
        std::cout.rdbuf(old_out_);
        std::cerr.rdbuf(old_err_);
    }
    std::vector<std::string> err_lines;

private:
    line_buf out_, err_;
    std::streambuf *old_out_ = nullptr, *old_err_ = nullptr;
};

void copy_out(const std::string &s, char *dst, size_t size) {
    if (!dst || size == 0) return;
    std::snprintf(dst, size, "%s", s.c_str());
}

// The last two lines innoextract printed on stderr, else `what`.
std::string error_text(const std::vector<std::string> &lines, const std::string &what) {
    if (!what.empty()) return what;
    std::string out;
    size_t from = lines.size() > 2 ? lines.size() - 2 : 0;
    for (size_t i = from; i < lines.size(); i++) {
        if (!out.empty()) out += ' ';
        out += lines[i];
    }
    return out.empty() ? "innoextract failed" : out;
}

// The paths a listing line names (` - "bin/x.exe" [ru-RU], "bin2/x.exe"`),
// folders left out; nothing for other lines.
std::vector<std::string> listed_paths(const std::string &line) {
    std::vector<std::string> out;
    if (line.compare(0, 4, " - \"") != 0) return out;
    size_t pos = 0;
    for (int part = 0;; part++) {
        size_t q = line.find('"', pos);
        std::string piece = line.substr(pos, q == std::string::npos ? std::string::npos : q - pos);
        if (part % 2 == 1 && !piece.empty() && piece.back() != '/') out.push_back(piece);
        if (q == std::string::npos) break;
        pos = q + 1;
    }
    return out;
}

extract_options base_options() {
    extract_options o;
    // As main() sets them for `--color=0 --progress=0` and no other display option.
    color::init(color::disable, color::disable);
    logger::quiet = false;
    o.warn_unused = true;
    o.filenames.set_expand(true);
    o.filenames.set_lowercase(false);
    o.preserve_file_times = true;
    o.local_timestamps = false;
    o.collisions = OverwriteCollisions;
    o.extract_temp = true;
    o.gog_galaxy = true;
    o.extract_unknown = true;
    return o;
}

// Runs process_file, turning what it throws (and errors it only logged) into
// a status and error text.
int run(const char *setup, const extract_options &o, capture &cap, char *error, size_t error_size) {
    size_t errors = logger::total_errors;
    std::string what;
    int status = RECOMP_INNO_OK;
    try {
        process_file(boost::filesystem::path(setup), o);
    } catch (const progress::cancelled &) {
        status = RECOMP_INNO_CANCELLED;
    } catch (const setup::version_error &) {
        what = "Not a supported Inno Setup installer.";
        status = RECOMP_INNO_FAILED;
    } catch (const std::exception &e) {
        what = e.what();
        status = RECOMP_INNO_FAILED;
    } catch (...) {
        what = "innoextract failed";
        status = RECOMP_INNO_FAILED;
    }
    std::cout.flush();
    std::cerr.flush();
    if (status == RECOMP_INNO_OK && logger::total_errors != errors) status = RECOMP_INNO_FAILED;
    if (status == RECOMP_INNO_FAILED) copy_out(error_text(cap.err_lines, what), error, error_size);
    return status;
}

struct extract_state {
    recomp_inno_progress_fn fn;
    void *ctx;
    uint64_t bytes = 0;
    std::string current;
    bool changed = false;
};
extract_state *active = nullptr;

bool progress_hook(boost::uint64_t delta) {
    extract_state *s = active;
    if (!s || !s->fn) return true;
    s->bytes += delta;
    const char *current = s->changed ? s->current.c_str() : nullptr;
    s->changed = false;
    return s->fn(s->ctx, current, s->bytes) != 0;
}

} // namespace

extern "C" int recomp_inno_gog_id(const char *setup, char *id, size_t id_size, char *error, size_t error_size) {
    std::lock_guard<std::mutex> guard(call_lock);
    copy_out("", id, id_size);
    std::string found;
    capture cap([&](const std::string &line) {
        if (found.empty() && !line.empty()) found = line;
    });
    extract_options o = base_options();
    // --gog-game-id --silent: the id alone on a line, nothing when there is none.
    o.gog_game_id = true;
    o.silent = o.quiet = true;
    logger::quiet = true;
    int status = run(setup, o, cap, error, error_size);
    if (status == RECOMP_INNO_OK) copy_out(found, id, id_size);
    return status;
}

extern "C" int recomp_inno_list(const char *setup, recomp_inno_file_fn file, void *ctx, char *error,
                                size_t error_size) {
    std::lock_guard<std::mutex> guard(call_lock);
    capture cap([&](const std::string &line) {
        for (const std::string &p : listed_paths(line)) file(ctx, p.c_str());
    });
    extract_options o = base_options();
    o.list = true;
    return run(setup, o, cap, error, error_size);
}

extern "C" int recomp_inno_extract(const char *setup, const char *output_dir, const char *const *paths, size_t count,
                                   recomp_inno_progress_fn progress, void *ctx, char *error, size_t error_size) {
    std::lock_guard<std::mutex> guard(call_lock);
    extract_state state{progress, ctx};
    // innoextract names each file as it starts on it.
    capture cap([&](const std::string &line) {
        std::vector<std::string> names = listed_paths(line);
        if (!names.empty()) {
            state.current = names.front();
            state.changed = true;
        }
    });
    extract_options o = base_options();
    // --extract (which lists what it unpacks), --output-dir, --include /<path>.
    o.extract = true;
    o.list = true;
    o.output_dir = output_dir;
    for (size_t i = 0; i < count; i++) o.include.push_back(std::string("/") + paths[i]);
    active = &state;
    progress::hook = progress_hook;
    int status = run(setup, o, cap, error, error_size);
    progress::hook = nullptr;
    active = nullptr;
    return status;
}
