// ucrt_stdio.cpp - the UCRT's stdio (api-ms-win-crt-stdio-l1-1-0.dll): FILE
// streams over host files, and the printf and scanf engines every
// __stdio_common_* entry point shares.
//
// The UCRT's FILE is opaque ({ void *_Placeholder; }), so a guest FILE * is
// only an identity here: a small guest block mapped to the host stream behind
// it. Text mode is Windows' (CR LF on disk, LF in the program; Ctrl-Z ends a
// read); ",ccs=UTF-16LE/UNICODE/UTF-8" streams carry wide characters in that
// encoding with a byte order mark, as the UCRT reads and writes them.
//
// The engines read their arguments from the guest's va_list: every argument a
// dword, doubles and 64-bit integers two. Numbers are formatted by the host's
// snprintf from a rebuilt specification, so field widths, flags and precision
// behave exactly as C specifies; strings are padded here, since the guest's
// wide strings are UTF-16 and the host's wchar_t is not.
#include "codepage.h"
#include "imports.h"
#include "memory.h"
#include "ucrt_internal.h"
#include "win32.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <xlocale.h>

namespace {

// --------------------------------------------------------------- FILE --
enum Enc { ENC_ANSI, ENC_UTF16, ENC_UTF8 };
struct GFile {
    FILE *host = nullptr;
    bool text = true;
    Enc enc = ENC_ANSI;
    bool eof = false, err = false;
    bool std = false;
    int pushed_w = -1; // ungetwc's character
};
std::map<uint32_t, GFile> g_files;
uint32_t g_iob = 0; // stdin/stdout/stderr, 4 bytes apart

uint32_t iob() {
    if (!g_iob) {
        g_iob = heap_alloc(16, true);
        GFile in, out, err;
        in.host = stdin;
        out.host = stdout;
        err.host = stderr;
        in.std = out.std = err.std = true;
        g_files[g_iob] = in;
        g_files[g_iob + 4] = out;
        g_files[g_iob + 8] = err;
    }
    return g_iob;
}
GFile *file_of(uint32_t f) {
    iob();
    auto it = g_files.find(f);
    return it == g_files.end() ? nullptr : &it->second;
}

int raw_getc(GFile &f) {
    int ch = fgetc(f.host);
    if (ch == EOF)
        f.eof = true;
    return ch;
}
// One byte in the stream's mode: text mode folds CR LF to LF and stops at ^Z.
int get_byte(GFile &f) {
    int ch = raw_getc(f);
    if (!f.text || ch == EOF)
        return ch;
    if (ch == 0x1a) {
        f.eof = true;
        return EOF;
    }
    if (ch == '\r') {
        int next = fgetc(f.host);
        if (next == '\n')
            return '\n';
        if (next != EOF)
            ungetc(next, f.host);
    }
    return ch;
}
void put_byte(GFile &f, int ch) {
    if (f.text && ch == '\n')
        fputc('\r', f.host);
    if (fputc(ch, f.host) == EOF)
        f.err = true;
}
int utf8_decode(GFile &f) {
    int b0 = raw_getc(f);
    if (b0 == EOF || b0 < 0x80)
        return b0;
    int n = b0 >= 0xf0 ? 3 : b0 >= 0xe0 ? 2 : 1;
    uint32_t cp = (uint32_t)b0 & (0x3fu >> n);
    for (int i = 0; i < n; ++i) {
        int b = raw_getc(f);
        if (b == EOF)
            return EOF;
        cp = (cp << 6) | ((uint32_t)b & 0x3f);
    }
    return cp > 0xffff ? 0xfffd : (int)cp;
}
int raw_getw(GFile &f) {
    if (f.enc == ENC_UTF16) {
        int lo = raw_getc(f), hi = lo == EOF ? EOF : raw_getc(f);
        return hi == EOF ? EOF : (lo | (hi << 8));
    }
    if (f.enc == ENC_UTF8)
        return utf8_decode(f);
    return get_byte(f); // the C locale: a byte is its code point
}
// A wide character in the stream's mode.
int get_wide(GFile &f) {
    if (f.pushed_w >= 0) {
        int w = f.pushed_w;
        f.pushed_w = -1;
        return w;
    }
    if (f.enc == ENC_ANSI)
        return get_byte(f);
    int w = raw_getw(f);
    if (f.text && w == '\r') {
        long pos = ftell(f.host);
        int next = raw_getw(f);
        if (next == '\n')
            return '\n';
        f.eof = false;
        fseek(f.host, pos, SEEK_SET);
    }
    return w;
}
void raw_putw(GFile &f, uint32_t w) {
    if (f.enc == ENC_UTF16) {
        fputc((int)(w & 0xff), f.host);
        fputc((int)(w >> 8), f.host);
    } else if (w < 0x80) {
        fputc((int)w, f.host);
    } else if (w < 0x800) {
        fputc((int)(0xc0 | (w >> 6)), f.host);
        fputc((int)(0x80 | (w & 0x3f)), f.host);
    } else {
        fputc((int)(0xe0 | (w >> 12)), f.host);
        fputc((int)(0x80 | ((w >> 6) & 0x3f)), f.host);
        fputc((int)(0x80 | (w & 0x3f)), f.host);
    }
}
bool put_wide(GFile &f, uint32_t w) {
    if (f.enc == ENC_ANSI) {
        if (w > 0xff)
            return false; // no C-locale byte for it
        put_byte(f, (int)w);
        return true;
    }
    if (f.text && w == '\n')
        raw_putw(f, '\r');
    raw_putw(f, w);
    return true;
}

// Opens `guest_path` with a Windows mode string ("rb", "w+t", "r,ccs=UNICODE").
bool open_into(GFile &f, const std::string &guest_path, const std::string &mode) {
    std::string flags = mode.substr(0, mode.find(','));
    bool read = flags.find('r') != std::string::npos;
    bool append = flags.find('a') != std::string::npos;
    bool plus = flags.find('+') != std::string::npos;
    if (flags.find('b') != std::string::npos)
        f.text = false;
    else if (flags.find('t') != std::string::npos)
        f.text = true;
    else
        f.text = true; // _fmode is text unless _set_fmode said otherwise
    f.enc = ENC_ANSI;
    std::string ccs;
    size_t at = mode.find("ccs=");
    if (at != std::string::npos) {
        ccs = mode.substr(at + 4);
        for (char &ch : ccs)
            ch = (char)toupper((unsigned char)ch);
        f.enc = ccs.rfind("UTF-8", 0) == 0 ? ENC_UTF8 : ENC_UTF16;
    }
    std::string host = win32_host_path_op(guest_path, read && !plus ? WIN32_FILE_READ : WIN32_FILE_WRITE);
    if (host.empty())
        return false;
    std::string hmode = read ? (plus ? "r+b" : "rb") : append ? (plus ? "a+b" : "ab") : (plus ? "w+b" : "wb");
    f.host = fopen(host.c_str(), hmode.c_str());
    if (!f.host)
        return false;
    f.eof = f.err = false;
    f.pushed_w = -1;
    if (f.enc != ENC_ANSI) {
        if (read || append) {
            // A byte order mark names the encoding and is not content.
            int b0 = fgetc(f.host), b1 = b0 == EOF ? EOF : fgetc(f.host);
            if (b0 == 0xff && b1 == 0xfe) {
                f.enc = ENC_UTF16;
            } else if (b0 == 0xef && b1 == 0xbb && fgetc(f.host) == 0xbf) {
                f.enc = ENC_UTF8;
            } else {
                fseek(f.host, 0, SEEK_SET);
                if (append && b0 == EOF) // a new file gets a mark
                    fputs(f.enc == ENC_UTF16 ? "\xff\xfe" : "\xef\xbb\xbf", f.host);
            }
            if (append)
                fseek(f.host, 0, SEEK_END);
        } else {
            fputs(f.enc == ENC_UTF16 ? "\xff\xfe" : "\xef\xbb\xbf", f.host);
        }
    }
    return true;
}

void s_iob_func(X86 *c) {
    uint32_t i = arg(c, 0);
    set_eax(c, iob() + 4 * (i < 3 ? i : 2));
}
void s_fopen(X86 *c) {
    // The path is ANSI in the game's code page (profile and save names).
    set_eax(c, ucrt_fopen(win32cp::ansi_to_utf8(gm_str(arg(c, 0))), gm_str(arg(c, 1))));
}
void s_freopen(X86 *c) {
    uint32_t stream = arg(c, 2);
    GFile *f = file_of(stream);
    if (!f) {
        set_eax(c, 0);
        return;
    }
    if (f->host && !f->std)
        fclose(f->host);
    f->std = false;
    set_eax(c, open_into(*f, win32cp::ansi_to_utf8(gm_str(arg(c, 0))), gm_str(arg(c, 1))) ? stream : 0);
}
void s_fclose(X86 *c) {
    uint32_t stream = arg(c, 0);
    GFile *f = file_of(stream);
    if (!f) {
        set_eax(c, (uint32_t)EOF);
        return;
    }
    int r = f->std ? 0 : fclose(f->host);
    if (!f->std) {
        g_files.erase(stream);
        heap_free(stream);
    }
    set_eax(c, r == 0 ? 0 : (uint32_t)EOF);
}
void s_fread(X86 *c) {
    uint32_t buf = arg(c, 0), size = arg(c, 1), count = arg(c, 2);
    GFile *f = file_of(arg(c, 3));
    uint64_t want = (uint64_t)size * count;
    if (!f || !size || !count || !gm_valid(buf, (uint32_t)std::min<uint64_t>(want, 0xffffffffu))) {
        set_eax(c, 0);
        return;
    }
    uint64_t got = 0;
    if (!f->text) {
        got = fread(gm_ptr(buf), 1, (size_t)want, f->host);
        if (got < want)
            f->eof = true;
    } else {
        for (; got < want; ++got) {
            int ch = get_byte(*f);
            if (ch == EOF)
                break;
            g_mem[buf + got] = (uint8_t)ch;
        }
    }
    set_eax(c, (uint32_t)(got / size));
}
void s_fwrite(X86 *c) {
    uint32_t buf = arg(c, 0), size = arg(c, 1), count = arg(c, 2);
    GFile *f = file_of(arg(c, 3));
    uint64_t want = (uint64_t)size * count;
    if (!f || !size || !count || !gm_valid(buf, (uint32_t)std::min<uint64_t>(want, 0xffffffffu))) {
        set_eax(c, 0);
        return;
    }
    if (!f->text) {
        set_eax(c, (uint32_t)(fwrite(gm_ptr(buf), 1, (size_t)want, f->host) / size));
        return;
    }
    for (uint64_t i = 0; i < want; ++i)
        put_byte(*f, g_mem[buf + i]);
    set_eax(c, f->err ? 0 : count);
}
void s_fgetc(X86 *c) {
    GFile *f = file_of(arg(c, 0));
    set_eax(c, f ? (uint32_t)get_byte(*f) : (uint32_t)EOF);
}
void s_fputc(X86 *c) {
    GFile *f = file_of(arg(c, 1));
    if (!f) {
        set_eax(c, (uint32_t)EOF);
        return;
    }
    put_byte(*f, (int)(arg(c, 0) & 0xff));
    set_eax(c, f->err ? (uint32_t)EOF : (arg(c, 0) & 0xff));
}
void s_ungetc(X86 *c) {
    GFile *f = file_of(arg(c, 1));
    int ch = (int)arg(c, 0);
    if (!f || ch == EOF) {
        set_eax(c, (uint32_t)EOF);
        return;
    }
    f->eof = false;
    set_eax(c, (uint32_t)ungetc(ch & 0xff, f->host));
}
void s_fgetwc(X86 *c) {
    GFile *f = file_of(arg(c, 0));
    int w = f ? get_wide(*f) : EOF;
    set_eax(c, w == EOF ? 0xffffu : (uint32_t)w); // WEOF
}
void s_fputwc(X86 *c) {
    GFile *f = file_of(arg(c, 1));
    uint32_t w = arg(c, 0) & 0xffff;
    set_eax(c, f && put_wide(*f, w) ? w : 0xffffu);
}
void s_ungetwc(X86 *c) {
    GFile *f = file_of(arg(c, 1));
    uint32_t w = arg(c, 0) & 0xffff;
    if (!f || w == 0xffff) {
        set_eax(c, 0xffff);
        return;
    }
    f->pushed_w = (int)w;
    f->eof = false;
    set_eax(c, w);
}
void s_feof(X86 *c) {
    GFile *f = file_of(arg(c, 0));
    set_eax(c, f && f->eof && f->pushed_w < 0 ? 0x10 : 0);
}
void s_fflush(X86 *c) {
    uint32_t stream = arg(c, 0);
    if (!stream) {
        for (auto &kv : g_files)
            fflush(kv.second.host);
        set_eax(c, 0);
        return;
    }
    GFile *f = file_of(stream);
    set_eax(c, f && fflush(f->host) == 0 ? 0 : (uint32_t)EOF);
}
void s_setvbuf(X86 *c) {
    set_eax(c, file_of(arg(c, 0)) ? 0 : (uint32_t)-1);
}
// _fseeki64(stream, __int64 offset, origin)
void s_fseeki64(X86 *c) {
    GFile *f = file_of(arg(c, 0));
    int64_t off = (int64_t)((uint64_t)arg(c, 1) | ((uint64_t)arg(c, 2) << 32));
    if (!f || fseeko(f->host, off, (int)arg(c, 3)) != 0) {
        set_eax(c, (uint32_t)-1);
        return;
    }
    f->eof = false;
    f->pushed_w = -1;
    set_eax(c, 0);
}
void s_fgetpos(X86 *c) {
    GFile *f = file_of(arg(c, 0));
    off_t pos = f ? ftello(f->host) : -1;
    if (pos < 0 || !arg(c, 1)) {
        set_eax(c, (uint32_t)-1);
        return;
    }
    wr64(arg(c, 1), (uint64_t)pos);
    set_eax(c, 0);
}
void s_fsetpos(X86 *c) {
    GFile *f = file_of(arg(c, 0));
    if (!f || !arg(c, 1) || fseeko(f->host, (off_t)rd64(arg(c, 1)), SEEK_SET) != 0) {
        set_eax(c, (uint32_t)-1);
        return;
    }
    f->eof = false;
    f->pushed_w = -1;
    set_eax(c, 0);
}
void s_puts(X86 *c) {
    std::string s = gm_str(arg(c, 0));
    fputs(s.c_str(), stdout);
    fputc('\n', stdout);
    set_eax(c, 0);
}

// ------------------------------------------------------------- printf --
// The guest's va_list, read forward.
struct VaReader {
    uint32_t p;
    uint32_t u32() {
        uint32_t v = rd32(p);
        p += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t v = rd64(p);
        p += 8;
        return v;
    }
    double f64() {
        double v = rdf64(p);
        p += 8;
        return v;
    }
};

// A format string, narrow or UTF-16, read one unit at a time.
struct FmtReader {
    uint32_t p;
    bool wide;
    uint32_t peek() const { return wide ? rd16(p) : rd8(p); }
    uint32_t next() {
        uint32_t ch = peek();
        p += wide ? 2 : 1;
        return ch;
    }
};

const uint64_t PRINTF_LEGACY_WIDE = 0x4;

// Formats into UTF-16 units (narrow output is the low byte of each, by the C
// locale's identity mapping; a wide character above 0xff in narrow output
// becomes '?').
std::u16string format(uint32_t fmt, bool wide, uint32_t va, uint64_t options) {
    std::u16string out;
    FmtReader f{fmt, wide};
    VaReader a{va};
    auto put_ascii = [&](const char *s, size_t n) {
        for (size_t i = 0; i < n; ++i)
            out.push_back((unsigned char)s[i]);
    };
    for (uint32_t guard = 0; guard < 0x100000; ++guard) {
        uint32_t ch = f.next();
        if (!ch)
            break;
        if (ch != '%') {
            out.push_back((char16_t)ch);
            continue;
        }
        std::string flags;
        for (;;) {
            uint32_t fl = f.peek();
            if (fl == '-' || fl == '+' || fl == ' ' || fl == '#' || fl == '0') {
                flags.push_back((char)fl);
                f.next();
            } else {
                break;
            }
        }
        int width = -1, prec = -1;
        if (f.peek() == '*') {
            f.next();
            width = (int32_t)a.u32();
            if (width < 0) {
                flags.push_back('-');
                width = -width;
            }
        } else {
            while (f.peek() >= '0' && f.peek() <= '9')
                width = (width < 0 ? 0 : width * 10) + (int)(f.next() - '0');
        }
        if (f.peek() == '.') {
            f.next();
            prec = 0;
            if (f.peek() == '*') {
                f.next();
                prec = (int32_t)a.u32();
                if (prec < 0)
                    prec = -1;
            } else {
                while (f.peek() >= '0' && f.peek() <= '9')
                    prec = prec * 10 + (int)(f.next() - '0');
            }
        }
        // Length: 0 default, 'h' short, 'H' char, 'l' long/wide, 'q' 64-bit.
        char len = 0;
        bool wide_arg = false, narrow_arg = false;
        for (;;) {
            uint32_t l = f.peek();
            if (l == 'h') {
                f.next();
                if (f.peek() == 'h') {
                    f.next();
                    len = 'H';
                } else {
                    len = 'h';
                    narrow_arg = true;
                }
            } else if (l == 'l') {
                f.next();
                if (f.peek() == 'l') {
                    f.next();
                    len = 'q';
                } else {
                    len = 'l';
                    wide_arg = true;
                }
            } else if (l == 'w') {
                f.next();
                wide_arg = true;
            } else if (l == 'L' || l == 'j' || l == 'z' || l == 't') {
                f.next();
                if (l == 'j')
                    len = 'q';
            } else if (l == 'I') {
                f.next();
                if (f.peek() == '6') {
                    f.next();
                    f.next();
                    len = 'q';
                } else if (f.peek() == '3') {
                    f.next();
                    f.next();
                }
            } else {
                break;
            }
        }
        uint32_t conv = f.next();
        if (!conv)
            break;
        std::string spec = "%" + flags;
        if (width >= 0)
            spec += std::to_string(width);
        if (prec >= 0)
            spec += "." + std::to_string(prec);
        char buf[512];
        switch (conv) {
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            bool is_signed = conv == 'd' || conv == 'i';
            uint64_t raw = len == 'q' ? a.u64() : a.u32();
            long long sv;
            unsigned long long uv;
            if (len == 'q') {
                sv = (long long)raw;
                uv = raw;
            } else if (len == 'h') {
                sv = (int16_t)raw;
                uv = (uint16_t)raw;
            } else if (len == 'H') {
                sv = (int8_t)raw;
                uv = (uint8_t)raw;
            } else {
                sv = (int32_t)raw;
                uv = (uint32_t)raw;
            }
            spec += "ll";
            spec.push_back(conv == 'i' ? 'd' : (char)conv);
            int n = is_signed ? snprintf(buf, sizeof buf, spec.c_str(), sv)
                              : snprintf(buf, sizeof buf, spec.c_str(), uv);
            put_ascii(buf, (size_t)std::max(0, std::min(n, (int)sizeof buf - 1)));
            break;
        }
        case 'p': {
            int n = snprintf(buf, sizeof buf, "%08X", a.u32());
            put_ascii(buf, (size_t)n);
            break;
        }
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            spec.push_back((char)conv);
            int n = snprintf(buf, sizeof buf, spec.c_str(), a.f64());
            put_ascii(buf, (size_t)std::max(0, std::min(n, (int)sizeof buf - 1)));
            break;
        }
        case 'c':
        case 'C':
        case 's':
        case 'S': {
            // Which width the argument has: in a narrow format %s is char *;
            // in a wide one it is wchar_t * under the legacy specifiers.
            bool default_wide = wide && (options & PRINTF_LEGACY_WIDE);
            bool upper = conv == 'C' || conv == 'S';
            bool arg_wide = wide_arg ? true : narrow_arg ? false : (default_wide != upper);
            std::u16string text;
            if (conv == 'c' || conv == 'C') {
                uint32_t v = a.u32();
                text.push_back((char16_t)(arg_wide ? (v & 0xffff) : (v & 0xff)));
            } else {
                uint32_t s = a.u32();
                if (!s) {
                    text = arg_wide ? u"(null)" : u"(null)";
                } else {
                    size_t limit = prec >= 0 ? (size_t)prec : 0x100000;
                    for (size_t i = 0; i < limit; ++i) {
                        uint32_t u = arg_wide ? rd16(s + 2 * (uint32_t)i) : rd8(s + (uint32_t)i);
                        if (!u)
                            break;
                        text.push_back((char16_t)u);
                    }
                }
            }
            bool left = flags.find('-') != std::string::npos;
            size_t pad = width > 0 && (size_t)width > text.size() ? (size_t)width - text.size() : 0;
            if (!left)
                out.append(pad, u' ');
            out += text;
            if (left)
                out.append(pad, u' ');
            break;
        }
        case 'n':
            a.u32(); // the UCRT disables %n; nothing is stored
            break;
        case '%':
            out.push_back(u'%');
            break;
        default:
            out.push_back(u'%');
            out.push_back((char16_t)conv);
            break;
        }
    }
    if (!wide)
        for (char16_t &u : out)
            if (u > 0xff)
                u = u'?';
    return out;
}

// What __stdio_common_vs(w)printf returns and stores for a buffer of `count`.
const uint64_t PRINTF_LEGACY_NULL = 0x1, PRINTF_STANDARD_SNPRINTF = 0x2;
int32_t store_formatted(const std::u16string &text, uint32_t buffer, uint32_t count, bool wide,
                        uint64_t options) {
    uint32_t unit = wide ? 2 : 1, n = (uint32_t)text.size();
    auto put = [&](uint32_t i, uint32_t v) {
        if (wide)
            wr16(buffer + 2 * i, (uint16_t)v);
        else
            wr8(buffer + i, (uint8_t)v);
    };
    if (!buffer)
        return (int32_t)n;
    (void)unit;
    if (n < count) {
        for (uint32_t i = 0; i < n; ++i)
            put(i, text[i]);
        put(n, 0);
        return (int32_t)n;
    }
    if (!count)
        return (options & PRINTF_STANDARD_SNPRINTF) ? (int32_t)n : -1;
    uint32_t keep = (options & PRINTF_STANDARD_SNPRINTF) ? count - 1 : count;
    for (uint32_t i = 0; i < keep; ++i)
        put(i, text[i]);
    if (options & PRINTF_STANDARD_SNPRINTF) {
        put(count - 1, 0);
        return (int32_t)n;
    }
    if (n == count && !(options & PRINTF_LEGACY_NULL))
        return (int32_t)n; // fits exactly without its terminator: _vsnprintf's case
    return -1;
}
uint64_t options_arg(X86 *c) {
    return (uint64_t)arg(c, 0) | ((uint64_t)arg(c, 1) << 32);
}
// __stdio_common_vsprintf(options, buffer, count, format, locale, va_list)
void s_vsprintf(X86 *c) {
    uint64_t o = options_arg(c);
    std::u16string t = format(arg(c, 4), false, arg(c, 6), o);
    set_eax(c, (uint32_t)store_formatted(t, arg(c, 2), arg(c, 3), false, o));
}
void s_vswprintf(X86 *c) {
    uint64_t o = options_arg(c);
    std::u16string t = format(arg(c, 4), true, arg(c, 6), o);
    set_eax(c, (uint32_t)store_formatted(t, arg(c, 2), arg(c, 3), true, o));
}
// __stdio_common_vfprintf(options, stream, format, locale, va_list)
void s_vfprintf(X86 *c) {
    GFile *f = file_of(arg(c, 2));
    if (!f) {
        set_eax(c, (uint32_t)-1);
        return;
    }
    std::u16string t = format(arg(c, 3), false, arg(c, 5), options_arg(c));
    for (char16_t u : t)
        put_byte(*f, (int)u);
    set_eax(c, (uint32_t)t.size());
}
void s_vfwprintf(X86 *c) {
    GFile *f = file_of(arg(c, 2));
    if (!f) {
        set_eax(c, (uint32_t)-1);
        return;
    }
    std::u16string t = format(arg(c, 3), true, arg(c, 5), options_arg(c));
    for (char16_t u : t)
        if (!put_wide(*f, u)) {
            set_eax(c, (uint32_t)-1);
            return;
        }
    set_eax(c, (uint32_t)t.size());
}

// -------------------------------------------------------------- scanf --
// The input: a narrow or wide guest string with an optional bound, or a FILE.
struct Input {
    uint32_t p = 0, end = 0xffffffffu;
    bool wide = false;
    GFile *file = nullptr;
    uint32_t consumed = 0;
    int pending = -2; // one character pushed back
    int get() {
        int ch;
        if (pending != -2) {
            ch = pending;
            pending = -2;
        } else if (file) {
            ch = wide ? get_wide(*file) : get_byte(*file);
        } else if (p >= end) {
            ch = EOF;
        } else {
            uint32_t u = wide ? rd16(p) : rd8(p);
            if (!u) {
                ch = EOF;
            } else {
                p += wide ? 2 : 1;
                ch = (int)u;
            }
        }
        if (ch != EOF)
            ++consumed;
        return ch;
    }
    void unget(int ch) {
        if (ch == EOF)
            return;
        pending = ch;
        --consumed;
    }
    void finish() {
        // A FILE gets its look-ahead back.
        if (file && pending >= 0) {
            if (wide)
                file->pushed_w = pending;
            else
                ungetc(pending, file->host);
            pending = -2;
        }
    }
};
bool is_space(int ch) {
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

const uint64_t SCANF_SECURE = 0x1, SCANF_LEGACY_WIDE = 0x2;

int scan(Input &in, uint32_t fmt, bool wide_fmt, uint32_t va, uint64_t options) {
    FmtReader f{fmt, wide_fmt};
    VaReader a{va};
    int assigned = 0;
    bool any_input = false;
    for (uint32_t guard = 0; guard < 0x10000; ++guard) {
        uint32_t ch = f.next();
        if (!ch)
            break;
        if (is_space((int)ch)) {
            int c2;
            while ((c2 = in.get()) != EOF && is_space(c2))
                ;
            in.unget(c2);
            continue;
        }
        if (ch != '%' || f.peek() == '%') {
            if (ch == '%')
                f.next();
            int c2 = in.get();
            if (c2 == EOF)
                return any_input || assigned ? assigned : -1;
            if ((uint32_t)c2 != ch) {
                in.unget(c2);
                return assigned;
            }
            any_input = true;
            continue;
        }
        bool suppress = false;
        if (f.peek() == '*') {
            f.next();
            suppress = true;
        }
        int width = 0;
        while (f.peek() >= '0' && f.peek() <= '9')
            width = width * 10 + (int)(f.next() - '0');
        char len = 0;
        bool wide_arg = false, narrow_arg = false;
        for (;;) {
            uint32_t l = f.peek();
            if (l == 'h') {
                f.next();
                if (f.peek() == 'h') {
                    f.next();
                    len = 'H';
                } else {
                    len = 'h';
                    narrow_arg = true;
                }
            } else if (l == 'l') {
                f.next();
                if (f.peek() == 'l') {
                    f.next();
                    len = 'q';
                } else {
                    len = 'l';
                    wide_arg = true;
                }
            } else if (l == 'L') {
                f.next();
                len = 'l';
            } else if (l == 'w') {
                f.next();
                wide_arg = true;
            } else if (l == 'I') {
                f.next();
                if (f.peek() == '6') {
                    f.next();
                    f.next();
                    len = 'q';
                } else if (f.peek() == '3') {
                    f.next();
                    f.next();
                }
            } else {
                break;
            }
        }
        uint32_t conv = f.next();
        if (!conv)
            break;
        if (conv == 'n') {
            if (!suppress)
                wr32(a.u32(), in.consumed);
            continue;
        }
        // Every conversion but %c and %[ skips leading white space.
        if (conv != 'c' && conv != 'C' && conv != '[') {
            int c2;
            while ((c2 = in.get()) != EOF && is_space(c2))
                ;
            in.unget(c2);
        }
        int limit = width > 0 ? width : (conv == 'c' || conv == 'C') ? 1 : 0x7fffffff;
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            std::string text;
            int c2 = in.get();
            if ((c2 == '+' || c2 == '-') && (int)text.size() < limit) {
                text.push_back((char)c2);
                c2 = in.get();
            }
            int base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X') ? 16 : conv == 'i' ? 0 : 10;
            if ((base == 16 || base == 0) && c2 == '0' && (int)text.size() < limit) {
                text.push_back('0');
                c2 = in.get();
                if ((c2 == 'x' || c2 == 'X') && (int)text.size() < limit) {
                    text.push_back((char)c2);
                    c2 = in.get();
                    base = 16;
                } else if (base == 0) {
                    base = 8;
                }
            }
            if (base == 0)
                base = 10;
            while (c2 != EOF && (int)text.size() < limit) {
                int d = c2 >= '0' && c2 <= '9' ? c2 - '0'
                        : c2 >= 'a' && c2 <= 'z' ? c2 - 'a' + 10
                        : c2 >= 'A' && c2 <= 'Z' ? c2 - 'A' + 10 : 99;
                if (d >= base)
                    break;
                text.push_back((char)c2);
                c2 = in.get();
            }
            in.unget(c2);
            bool digits = text.find_first_of("0123456789abcdefABCDEF") != std::string::npos;
            if (!digits)
                return any_input || assigned ? assigned : (c2 == EOF ? -1 : assigned);
            any_input = true;
            unsigned long long v = (text[0] == '-') ? (unsigned long long)strtoll(text.c_str(), nullptr, base)
                                                    : strtoull(text.c_str(), nullptr, base);
            if (!suppress) {
                uint32_t dst = a.u32();
                if (len == 'q')
                    wr64(dst, v);
                else if (len == 'h')
                    wr16(dst, (uint16_t)v);
                else if (len == 'H')
                    wr8(dst, (uint8_t)v);
                else
                    wr32(dst, (uint32_t)v);
                ++assigned;
            }
            continue;
        }
        if (conv == 'e' || conv == 'E' || conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G' ||
            conv == 'a' || conv == 'A') {
            std::string text;
            int c2 = in.get();
            auto take = [&](bool ok) {
                if (ok && (int)text.size() < limit) {
                    text.push_back((char)c2);
                    c2 = in.get();
                    return true;
                }
                return false;
            };
            take(c2 == '+' || c2 == '-');
            while (take(c2 >= '0' && c2 <= '9'))
                ;
            if (take(c2 == '.'))
                while (take(c2 >= '0' && c2 <= '9'))
                    ;
            if (text.find_first_of("0123456789") != std::string::npos && take(c2 == 'e' || c2 == 'E')) {
                take(c2 == '+' || c2 == '-');
                while (take(c2 >= '0' && c2 <= '9'))
                    ;
            }
            in.unget(c2);
            if (text.find_first_of("0123456789") == std::string::npos)
                return any_input || assigned ? assigned : (c2 == EOF ? -1 : assigned);
            any_input = true;
            double v = strtod_l(text.c_str(), nullptr, (locale_t)0);
            if (!suppress) {
                uint32_t dst = a.u32();
                if (len == 'l' || len == 'q')
                    wrf64(dst, v);
                else
                    wrf32(dst, (float)v);
                ++assigned;
            }
            continue;
        }
        if (conv == 's' || conv == 'S' || conv == 'c' || conv == 'C' || conv == '[') {
            // Which width the destination has, as printf decides it.
            bool default_wide = wide_fmt && (options & SCANF_LEGACY_WIDE);
            bool upper = conv == 'S' || conv == 'C';
            bool dst_wide = wide_arg ? true : narrow_arg ? false : (default_wide != upper);
            bool set[0x10000] = {};
            bool negate = false;
            if (conv == '[') {
                if (f.peek() == '^') {
                    f.next();
                    negate = true;
                }
                bool first = true;
                uint32_t prev = 0;
                for (;;) {
                    uint32_t s = f.next();
                    if (!s)
                        break;
                    if (s == ']' && !first)
                        break;
                    if (s == '-' && !first && f.peek() != ']' && prev) {
                        uint32_t hi = f.next();
                        for (uint32_t k = prev; k <= hi && k < 0x10000; ++k)
                            set[k] = true;
                        prev = 0;
                    } else {
                        set[s & 0xffff] = true;
                        prev = s;
                    }
                    first = false;
                }
            }
            uint32_t dst = suppress ? 0 : a.u32();
            uint32_t cap = suppress || !(options & SCANF_SECURE) ? 0xffffffffu : a.u32();
            uint32_t n = 0;
            int c2;
            while (n < (uint32_t)limit && (c2 = in.get()) != EOF) {
                bool ok = conv == '[' ? (set[c2 & 0xffff] != negate)
                          : (conv == 's' || conv == 'S') ? !is_space(c2) : true;
                if (!ok) {
                    in.unget(c2);
                    break;
                }
                if (dst && n < cap) {
                    if (dst_wide)
                        wr16(dst + 2 * n, (uint16_t)c2);
                    else
                        wr8(dst + n, (uint8_t)(c2 > 0xff ? '?' : c2));
                }
                ++n;
            }
            if (!n)
                return any_input || assigned ? assigned : -1;
            any_input = true;
            if (dst && conv != 'c' && conv != 'C') {
                uint32_t at = n < cap ? n : cap - 1;
                if (dst_wide)
                    wr16(dst + 2 * at, 0);
                else
                    wr8(dst + at, 0);
            }
            if (!suppress)
                ++assigned;
            continue;
        }
        break; // an unknown conversion ends the scan
    }
    return assigned;
}
// __stdio_common_vsscanf(options, buffer, count, format, locale, va_list)
void s_vsscanf(X86 *c) {
    Input in;
    in.p = arg(c, 2);
    uint32_t count = arg(c, 3);
    if (count != 0xffffffffu)
        in.end = in.p + count;
    int r = scan(in, arg(c, 4), false, arg(c, 6), options_arg(c));
    if (log_level() >= 2) {
        // Once per format: what was parsed, from what, and how many fields.
        std::string fmt = gm_str(arg(c, 4), 120);
        log_once(("sscanf " + fmt).c_str(), "ucrt: sscanf(\"%s\", \"%s\") from %08x -> %d",
                 gm_str(arg(c, 2), 120).c_str(), fmt.c_str(), rd32(c->r[R_ESP]), r);
    }
    set_eax(c, (uint32_t)r);
}
void s_vswscanf(X86 *c) {
    Input in;
    in.p = arg(c, 2);
    in.wide = true;
    uint32_t count = arg(c, 3);
    if (count != 0xffffffffu)
        in.end = in.p + 2 * count;
    int r = scan(in, arg(c, 4), true, arg(c, 6), options_arg(c));
    if (log_level() >= 2) {
        std::string fmt = gm_wstr(arg(c, 4), 120);
        log_once(("swscanf " + fmt).c_str(), "ucrt: swscanf(L\"%s\", L\"%s\") from %08x -> %d",
                 gm_wstr(arg(c, 2), 120).c_str(), fmt.c_str(), rd32(c->r[R_ESP]), r);
    }
    set_eax(c, (uint32_t)r);
}
// __stdio_common_vfscanf(options, stream, format, locale, va_list)
void s_vfscanf(X86 *c) {
    GFile *f = file_of(arg(c, 2));
    if (!f) {
        set_eax(c, (uint32_t)-1);
        return;
    }
    Input in;
    in.file = f;
    int r = scan(in, arg(c, 3), false, arg(c, 5), options_arg(c));
    in.finish();
    set_eax(c, (uint32_t)r);
}

#define STDIO "api-ms-win-crt-stdio-l1-1-0.dll"
const ImportShim shims[] = {
    {STDIO, "__acrt_iob_func", ARGC_CDECL, s_iob_func},
    {STDIO, "fopen", ARGC_CDECL, s_fopen},
    {STDIO, "freopen", ARGC_CDECL, s_freopen},
    {STDIO, "fclose", ARGC_CDECL, s_fclose},
    {STDIO, "fread", ARGC_CDECL, s_fread},
    {STDIO, "fwrite", ARGC_CDECL, s_fwrite},
    {STDIO, "fgetc", ARGC_CDECL, s_fgetc},
    {STDIO, "fputc", ARGC_CDECL, s_fputc},
    {STDIO, "ungetc", ARGC_CDECL, s_ungetc},
    {STDIO, "fgetwc", ARGC_CDECL, s_fgetwc},
    {STDIO, "fputwc", ARGC_CDECL, s_fputwc},
    {STDIO, "ungetwc", ARGC_CDECL, s_ungetwc},
    {STDIO, "feof", ARGC_CDECL, s_feof},
    {STDIO, "fflush", ARGC_CDECL, s_fflush},
    {STDIO, "setvbuf", ARGC_CDECL, s_setvbuf},
    {STDIO, "_fseeki64", ARGC_CDECL, s_fseeki64},
    {STDIO, "fgetpos", ARGC_CDECL, s_fgetpos},
    {STDIO, "fsetpos", ARGC_CDECL, s_fsetpos},
    {STDIO, "puts", ARGC_CDECL, s_puts},
    {STDIO, "__stdio_common_vsprintf", ARGC_CDECL, s_vsprintf},
    {STDIO, "__stdio_common_vswprintf", ARGC_CDECL, s_vswprintf},
    {STDIO, "__stdio_common_vfprintf", ARGC_CDECL, s_vfprintf},
    {STDIO, "__stdio_common_vfwprintf", ARGC_CDECL, s_vfwprintf},
    {STDIO, "__stdio_common_vsscanf", ARGC_CDECL, s_vsscanf},
    {STDIO, "__stdio_common_vswscanf", ARGC_CDECL, s_vswscanf},
    {STDIO, "__stdio_common_vfscanf", ARGC_CDECL, s_vfscanf},
};
} // namespace

uint32_t ucrt_fopen(const std::string &guest_path, const std::string &mode) {
    GFile f;
    if (!open_into(f, guest_path, mode))
        return 0;
    uint32_t handle = heap_alloc(16, true);
    if (!handle) {
        fclose(f.host);
        return 0;
    }
    g_files[handle] = f;
    return handle;
}

void ucrt_stdio_register() {
    imports_register(shims, sizeof shims / sizeof shims[0]);
}
