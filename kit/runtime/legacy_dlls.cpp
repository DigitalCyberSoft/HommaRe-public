// legacy_dlls.cpp - zlib1.dll on the host's zlib, and fail-cleanly stand-ins for
// the online, telemetry and crash-report DLLs a game links but needs no part of
// offline: libcurl.dll, UbiStats.dll, dbghelp.dll, RPCRT4 UuidCreate,
// COMCTL32 ordinal 17 (InitCommonControls) and the rest of WSOCK32's socket
// calls by name and ordinal.
//
// zlib: the guest's z_stream is the 32-bit zlib 1.2.x layout (56 bytes). Each
// guest stream owns one host z_stream, found by the guest struct's address; a
// call copies the guest's in/out cursors in, runs the host routine directly on
// guest memory (guest address a is g_mem + a, one contiguous mapping) and
// copies the advanced cursors, totals, adler and data_type back. The guest's
// zalloc/zfree are never called: the host stream allocates its own state.
// zlib1.dll is the cdecl build (zlibwapi.dll is the WINAPI one).
#include "curl_http.h"
#include "guest.h"
#include "imports.h"
#include "memory.h"
#include "win32.h"
#include "winsock.h"
#include <memory>
#include <random>
#include <string.h>
#include <unordered_map>
#include <zlib.h>

namespace {

// ---------------------------------------------------------------- zlib --

// Offsets in the guest's z_stream (zlib 1.2.x, 32-bit).
enum : uint32_t {
    ZS_NEXT_IN = 0,
    ZS_AVAIL_IN = 4,
    ZS_TOTAL_IN = 8,
    ZS_NEXT_OUT = 12,
    ZS_AVAIL_OUT = 16,
    ZS_TOTAL_OUT = 20,
    ZS_MSG = 24,
    ZS_STATE = 28,
    ZS_DATA_TYPE = 44,
    ZS_ADLER = 48,
    ZS_SIZE = 56,
};

struct HostStream {
    z_stream z{};
    bool deflate = false, back = false;
};

std::unordered_map<uint32_t, std::unique_ptr<HostStream>> &streams() {
    static std::unordered_map<uint32_t, std::unique_ptr<HostStream>> m;
    return m;
}

// zlib's messages are static strings; each gets one guest copy, kept for good.
uint32_t guest_message(const char *msg) {
    static std::unordered_map<const char *, uint32_t> copies;
    if (!msg)
        return 0;
    auto it = copies.find(msg);
    if (it != copies.end())
        return it->second;
    uint32_t n = (uint32_t)strlen(msg) + 1, a = heap_alloc(n);
    if (a)
        memcpy(g_mem + a, msg, n);
    copies[msg] = a;
    return a;
}

// The guest's cursors into the host stream. False when they leave the arena.
bool load_cursors(uint32_t strm, z_stream &z) {
    uint32_t in = rd32(strm + ZS_NEXT_IN), avail_in = rd32(strm + ZS_AVAIL_IN);
    uint32_t out = rd32(strm + ZS_NEXT_OUT), avail_out = rd32(strm + ZS_AVAIL_OUT);
    if ((in && !gm_valid(in, avail_in)) || (out && !gm_valid(out, avail_out)))
        return false;
    z.next_in = in ? g_mem + in : nullptr;
    z.avail_in = avail_in;
    z.next_out = out ? g_mem + out : nullptr;
    z.avail_out = avail_out;
    return true;
}
void store_cursors(uint32_t strm, const z_stream &z) {
    wr32(strm + ZS_NEXT_IN, z.next_in ? (uint32_t)(z.next_in - g_mem) : 0);
    wr32(strm + ZS_AVAIL_IN, z.avail_in);
    wr32(strm + ZS_TOTAL_IN, (uint32_t)z.total_in);
    wr32(strm + ZS_NEXT_OUT, z.next_out ? (uint32_t)(z.next_out - g_mem) : 0);
    wr32(strm + ZS_AVAIL_OUT, z.avail_out);
    wr32(strm + ZS_TOTAL_OUT, (uint32_t)z.total_out);
    wr32(strm + ZS_MSG, guest_message(z.msg));
    wr32(strm + ZS_DATA_TYPE, (uint32_t)z.data_type);
    wr32(strm + ZS_ADLER, (uint32_t)z.adler);
}

// A new host stream for a guest struct, replacing one a guest forgot to end.
HostStream *open_stream(uint32_t strm) {
    auto &m = streams();
    auto it = m.find(strm);
    if (it != m.end()) {
        if (it->second->deflate)
            deflateEnd(&it->second->z);
        else if (it->second->back)
            inflateBackEnd(&it->second->z);
        else
            inflateEnd(&it->second->z);
        m.erase(it);
    }
    auto s = std::make_unique<HostStream>();
    HostStream *p = s.get();
    m[strm] = std::move(s);
    return p;
}
HostStream *find_stream(uint32_t strm) {
    auto it = streams().find(strm);
    return it == streams().end() ? nullptr : it->second.get();
}
void close_stream(uint32_t strm) {
    streams().erase(strm);
    wr32(strm + ZS_STATE, 0);
}

// The guest's version string and struct size, checked as zlib's *Init_ does.
bool init_args_ok(uint32_t version, uint32_t size) {
    return version && rd8(version) == '1' && size == ZS_SIZE;
}

void z_inflateInit_(X86 *c) {
    uint32_t strm = arg(c, 0);
    if (!strm || !gm_valid(strm, ZS_SIZE) || !init_args_ok(arg(c, 1), arg(c, 2))) {
        set_eax(c, (uint32_t)Z_VERSION_ERROR);
        return;
    }
    HostStream *s = open_stream(strm);
    load_cursors(strm, s->z);
    int r = inflateInit(&s->z);
    if (r != Z_OK) {
        close_stream(strm);
        set_eax(c, (uint32_t)r);
        return;
    }
    wr32(strm + ZS_STATE, strm); // non-zero, as a live stream's state is
    store_cursors(strm, s->z);
    set_eax(c, Z_OK);
}

// deflateInit2_(strm, level, method, windowBits, memLevel, strategy, version,
// stream_size): the Heroes V expansions' saves.
void z_deflateInit2_(X86 *c) {
    uint32_t strm = arg(c, 0);
    if (!strm || !gm_valid(strm, ZS_SIZE) || !init_args_ok(arg(c, 6), arg(c, 7))) {
        set_eax(c, (uint32_t)Z_VERSION_ERROR);
        return;
    }
    HostStream *s = open_stream(strm);
    s->deflate = true;
    load_cursors(strm, s->z);
    int r = deflateInit2(&s->z, (int)arg(c, 1), (int)arg(c, 2), (int)arg(c, 3), (int)arg(c, 4), (int)arg(c, 5));
    if (r != Z_OK) {
        close_stream(strm);
        set_eax(c, (uint32_t)r);
        return;
    }
    wr32(strm + ZS_STATE, strm);
    store_cursors(strm, s->z);
    set_eax(c, Z_OK);
}

void z_deflateInit_(X86 *c) {
    uint32_t strm = arg(c, 0);
    int level = (int)arg(c, 1);
    if (!strm || !gm_valid(strm, ZS_SIZE) || !init_args_ok(arg(c, 2), arg(c, 3))) {
        set_eax(c, (uint32_t)Z_VERSION_ERROR);
        return;
    }
    HostStream *s = open_stream(strm);
    s->deflate = true;
    load_cursors(strm, s->z);
    int r = deflateInit(&s->z, level);
    if (r != Z_OK) {
        close_stream(strm);
        set_eax(c, (uint32_t)r);
        return;
    }
    wr32(strm + ZS_STATE, strm);
    store_cursors(strm, s->z);
    set_eax(c, Z_OK);
}

// inflate(strm, flush) and deflate(strm, flush).
void run(X86 *c, bool deflating) {
    uint32_t strm = arg(c, 0);
    HostStream *s = strm ? find_stream(strm) : nullptr;
    if (!s || s->deflate != deflating || s->back || !load_cursors(strm, s->z)) {
        set_eax(c, (uint32_t)Z_STREAM_ERROR);
        return;
    }
    int r = deflating ? deflate(&s->z, (int)arg(c, 1)) : inflate(&s->z, (int)arg(c, 1));
    store_cursors(strm, s->z);
    set_eax(c, (uint32_t)r);
}
void z_inflate(X86 *c) {
    run(c, false);
}
void z_deflate(X86 *c) {
    run(c, true);
}

void end(X86 *c, bool deflating) {
    uint32_t strm = arg(c, 0);
    HostStream *s = strm ? find_stream(strm) : nullptr;
    if (!s || s->deflate != deflating || s->back) {
        set_eax(c, (uint32_t)Z_STREAM_ERROR);
        return;
    }
    int r = deflating ? deflateEnd(&s->z) : inflateEnd(&s->z);
    store_cursors(strm, s->z);
    close_stream(strm);
    set_eax(c, (uint32_t)r);
}
void z_inflateEnd(X86 *c) {
    end(c, false);
}
void z_deflateEnd(X86 *c) {
    end(c, true);
}

// inflateBackInit_(strm, windowBits, window, version, stream_size): the window
// is the guest's own buffer, so out() is handed pointers into guest memory.
void z_inflateBackInit_(X86 *c) {
    uint32_t strm = arg(c, 0), window = arg(c, 2);
    int bits = (int)arg(c, 1);
    if (!strm || !gm_valid(strm, ZS_SIZE) || !init_args_ok(arg(c, 3), arg(c, 4)) || bits < 8 ||
        bits > 15 || !window || !gm_valid(window, 1u << bits)) {
        set_eax(c, (uint32_t)Z_STREAM_ERROR);
        return;
    }
    HostStream *s = open_stream(strm);
    s->back = true;
    load_cursors(strm, s->z);
    int r = inflateBackInit(&s->z, bits, g_mem + window);
    if (r != Z_OK) {
        close_stream(strm);
        set_eax(c, (uint32_t)r);
        return;
    }
    wr32(strm + ZS_STATE, strm);
    store_cursors(strm, s->z);
    set_eax(c, Z_OK);
}

// The guest's in()/out() callbacks (cdecl), called from inside the host
// inflateBack on the guest thread that called it.
struct BackCalls {
    X86 *c;
    uint32_t in, in_desc, out, out_desc, slot;
};
unsigned back_in(void *d, unsigned char **buf) {
    BackCalls *b = (BackCalls *)d;
    if (!b->in) { // no in(): the input already at next_in is all there is
        *buf = nullptr;
        return 0;
    }
    wr32(b->slot, 0);
    uint32_t n = guest_call(b->c, b->in, b->in_desc, b->slot);
    uint32_t p = rd32(b->slot);
    if (!n || !p || !gm_valid(p, n)) {
        *buf = nullptr;
        return 0;
    }
    *buf = g_mem + p;
    return n;
}
int back_out(void *d, unsigned char *buf, unsigned len) {
    BackCalls *b = (BackCalls *)d;
    return (int)guest_call(b->c, b->out, b->out_desc, (uint32_t)(buf - g_mem), len);
}

// inflateBack(strm, in, in_desc, out, out_desc)
void z_inflateBack(X86 *c) {
    uint32_t strm = arg(c, 0);
    HostStream *s = strm ? find_stream(strm) : nullptr;
    if (!s || !s->back || !load_cursors(strm, s->z)) {
        set_eax(c, (uint32_t)Z_STREAM_ERROR);
        return;
    }
    // Read every argument before the first callback moves ESP.
    BackCalls b{c, arg(c, 1), arg(c, 2), arg(c, 3), arg(c, 4), heap_alloc(4, true)};
    if (!b.slot) {
        set_eax(c, (uint32_t)Z_MEM_ERROR);
        return;
    }
    int r = inflateBack(&s->z, back_in, &b, back_out, &b);
    heap_free(b.slot);
    store_cursors(strm, s->z);
    set_eax(c, (uint32_t)r);
}

void z_inflateBackEnd(X86 *c) {
    uint32_t strm = arg(c, 0);
    HostStream *s = strm ? find_stream(strm) : nullptr;
    if (!s || !s->back) {
        set_eax(c, (uint32_t)Z_STREAM_ERROR);
        return;
    }
    inflateBackEnd(&s->z);
    close_stream(strm);
    set_eax(c, Z_OK);
}

// adler32(adler, buf, len)
void z_adler32(X86 *c) {
    uint32_t buf = arg(c, 1), len = arg(c, 2);
    if (!buf) {
        set_eax(c, (uint32_t)adler32(arg(c, 0), nullptr, 0));
        return;
    }
    if (!gm_valid(buf, len)) {
        set_eax(c, 1);
        return;
    }
    set_eax(c, (uint32_t)adler32(arg(c, 0), g_mem + buf, len));
}
// crc32(crc, buf, len)
void z_crc32(X86 *c) {
    uint32_t buf = arg(c, 1), len = arg(c, 2);
    if (!buf) {
        set_eax(c, (uint32_t)crc32(arg(c, 0), nullptr, 0));
        return;
    }
    if (!gm_valid(buf, len)) {
        set_eax(c, 0);
        return;
    }
    set_eax(c, (uint32_t)crc32(arg(c, 0), g_mem + buf, len));
}
// zlibVersion(): the guest's own 1.2.3 build reported this.
void z_zlibVersion(X86 *c) {
    static uint32_t version = 0;
    if (!version) {
        version = heap_alloc(8, true);
        if (version)
            gm_put_str(version, "1.2.3", 8);
    }
    set_eax(c, version);
}

// -------------------------------------------------------------- dbghelp --
// The crash reporter's symbol engine: never initialised, so it falls back to
// printing raw addresses (or nothing).
void dbghelp_false(X86 *c) {
    set_last_error(1); // ERROR_INVALID_FUNCTION
    set_eax(c, 0);
}
void dbghelp_true(X86 *c) {
    set_eax(c, 1);
}
void dbghelp_zero(X86 *c) {
    set_eax(c, 0);
}

// --------------------------------------------------------------- misc ----
void rpc_UuidCreate(X86 *c) {
    uint32_t uuid = arg(c, 0);
    if (!uuid || !gm_valid(uuid, 16)) {
        set_eax(c, 1783); // RPC_X_NULL_REF_POINTER
        return;
    }
    static std::mt19937 rng{std::random_device{}()};
    for (uint32_t i = 0; i < 16; i += 4)
        wr32(uuid + i, (uint32_t)rng());
    // A version 4 (random) UUID in RFC 4122 form: Data3's top nibble and
    // Data4[0]'s top two bits.
    wr16(uuid + 6, (uint16_t)((rd16(uuid + 6) & 0x0fff) | 0x4000));
    wr8(uuid + 8, (uint8_t)((rd8(uuid + 8) & 0x3f) | 0x80));
    set_eax(c, 0); // RPC_S_OK
}
void nothing(X86 *c) {
    set_eax(c, 0);
}

// -------------------------------------------------------------- WSOCK32 --
// No sockets: the address helpers do their arithmetic, every socket call
// fails with WSAENETDOWN, and select() reports nothing ready.
const uint32_t WSAENETDOWN = 10050, SOCKET_ERROR = 0xffffffffu, INVALID_SOCKET = 0xffffffffu;

void ws_fail(X86 *c) {
    set_last_error(WSAENETDOWN);
    set_eax(c, SOCKET_ERROR);
}
void ws_socket(X86 *c) {
    log_once("WSOCK32 socket",
             "WSOCK32: no network in this build; socket() fails with WSAENETDOWN");
    set_last_error(WSAENETDOWN);
    set_eax(c, INVALID_SOCKET);
}
void ws_closesocket(X86 *c) {
    set_eax(c, 0);
}
void ws_select(X86 *c) {
    // Nothing is ever ready; clear the caller's sets as Windows does.
    for (int i = 1; i <= 3; ++i)
        if (uint32_t set = arg(c, i))
            wr32(set, 0);
    set_eax(c, 0);
}
void ws_null(X86 *c) {
    set_last_error(WSAENETDOWN);
    set_eax(c, 0);
}
void ws_last_error(X86 *c) {
    set_eax(c, get_last_error());
}
void ws_fd_isset(X86 *c) {
    set_eax(c, 0);
}
void ws_htonl(X86 *c) {
    set_eax(c, __builtin_bswap32(arg(c, 0)));
}
void ws_htons(X86 *c) {
    set_eax(c, __builtin_bswap16((uint16_t)arg(c, 0)));
}
// inet_addr("a.b.c.d"): network order, or INADDR_NONE for anything else.
void ws_inet_addr(X86 *c) {
    std::string s = gm_str(arg(c, 0), 64);
    unsigned v[4];
    char tail;
    if (sscanf(s.c_str(), "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4 || v[0] > 255 ||
        v[1] > 255 || v[2] > 255 || v[3] > 255) {
        set_eax(c, 0xffffffffu);
        return;
    }
    set_eax(c, v[0] | (v[1] << 8) | (v[2] << 16) | (v[3] << 24));
}

// The host's sockets when the game plays over a network (winsock.h), else the
// stand-in above. A real call that fails goes to the log.
#define REAL_OR(real, stub)                                                                        \
    [](X86 *c) {                                                                                   \
        if (winsock_real()) {                                                                      \
            real(c);                                                                               \
            winsock_log_failure(c, #real);                                                         \
        } else {                                                                                   \
            stub(c);                                                                               \
        }                                                                                          \
    }

// Every WSOCK32 call by name and by its ordinal ("ordN", as the loader names it).
#define WS(name, ord, argc, fn)                                                                    \
    {"WSOCK32.dll", name, argc, fn}, {                                                             \
        "WSOCK32.dll", "ord" #ord, argc, fn                                                        \
    }

const ImportShim shims[] = {
    {"zlib1.dll", "inflateInit_", ARGC_CDECL, z_inflateInit_},
    {"zlib1.dll", "inflate", ARGC_CDECL, z_inflate},
    {"zlib1.dll", "inflateEnd", ARGC_CDECL, z_inflateEnd},
    {"zlib1.dll", "deflateInit_", ARGC_CDECL, z_deflateInit_},
    {"zlib1.dll", "deflateInit2_", ARGC_CDECL, z_deflateInit2_},
    {"zlib1.dll", "deflate", ARGC_CDECL, z_deflate},
    {"zlib1.dll", "deflateEnd", ARGC_CDECL, z_deflateEnd},
    {"zlib1.dll", "inflateBackInit_", ARGC_CDECL, z_inflateBackInit_},
    {"zlib1.dll", "inflateBack", ARGC_CDECL, z_inflateBack},
    {"zlib1.dll", "inflateBackEnd", ARGC_CDECL, z_inflateBackEnd},
    {"zlib1.dll", "adler32", ARGC_CDECL, z_adler32},
    {"zlib1.dll", "crc32", ARGC_CDECL, z_crc32},
    {"zlib1.dll", "zlibVersion", ARGC_CDECL, z_zlibVersion},

    // libcurl's stand-in is curl_http.cpp: a GS server list, and nothing else.
    {"libcurl.dll", "curl_global_init", ARGC_CDECL, curl_http_global_init},
    {"libcurl.dll", "curl_global_cleanup", ARGC_CDECL, curl_http_global_cleanup},
    {"libcurl.dll", "curl_easy_init", ARGC_CDECL, curl_http_easy_init},
    {"libcurl.dll", "curl_easy_setopt", ARGC_CDECL, curl_http_easy_setopt},
    {"libcurl.dll", "curl_easy_perform", ARGC_CDECL, curl_http_easy_perform},
    {"libcurl.dll", "curl_easy_cleanup", ARGC_CDECL, curl_http_easy_cleanup},
    {"libcurl.dll", "curl_easy_strerror", ARGC_CDECL, curl_http_easy_strerror},
    {"libcurl.dll", "curl_slist_append", ARGC_CDECL, curl_http_slist_append},
    {"libcurl.dll", "curl_slist_free_all", ARGC_CDECL, curl_http_slist_free_all},

    // Called nowhere directly in the one game that imports it; cdecl keeps
    // the stack balanced whatever its real argument count.
    {"UbiStats.dll", "TrackingCount", ARGC_CDECL, nothing},
    // Tribes of the East's name for the same tracking DLL.
    {"um.dll", "TrackingCount", ARGC_CDECL, nothing},

    {"dbghelp.dll", "SymInitialize", 3, dbghelp_false},
    {"dbghelp.dll", "SymCleanup", 1, dbghelp_true},
    {"dbghelp.dll", "SymSetOptions", 1, dbghelp_zero},
    {"dbghelp.dll", "SymGetOptions", 0, dbghelp_zero},
    {"dbghelp.dll", "SymFromAddr", 5, dbghelp_false},
    {"dbghelp.dll", "SymGetLineFromAddr64", 5, dbghelp_false},
    {"dbghelp.dll", "SymGetModuleInfo64", 4, dbghelp_false},
    {"dbghelp.dll", "SymGetModuleBase64", 3, dbghelp_zero},
    {"dbghelp.dll", "SymFunctionTableAccess64", 3, dbghelp_zero},
    {"dbghelp.dll", "StackWalk64", 9, dbghelp_false},

    {"RPCRT4.dll", "UuidCreate", 1, rpc_UuidCreate},
    {"COMCTL32.dll", "ord17", 0, nothing}, // InitCommonControls
    {"COMCTL32.dll", "InitCommonControls", 0, nothing},

    WS("accept", 1, 3, REAL_OR(winsock_accept, ws_socket)),
    WS("bind", 2, 3, REAL_OR(winsock_bind, ws_fail)),
    WS("closesocket", 3, 1, REAL_OR(winsock_closesocket, ws_closesocket)),
    WS("connect", 4, 3, REAL_OR(winsock_connect, ws_fail)),
    WS("getpeername", 5, 3, REAL_OR(winsock_getpeername, ws_fail)),
    WS("getsockname", 6, 3, REAL_OR(winsock_getsockname, ws_fail)),
    WS("getsockopt", 7, 5, REAL_OR(winsock_getsockopt, ws_fail)),
    WS("htonl", 8, 1, ws_htonl),
    WS("htons", 9, 1, ws_htons),
    WS("inet_addr", 10, 1, ws_inet_addr),
    WS("ioctlsocket", 12, 3, REAL_OR(winsock_ioctlsocket, ws_fail)),
    WS("listen", 13, 2, REAL_OR(winsock_listen, ws_fail)),
    WS("ntohl", 14, 1, ws_htonl),
    WS("ntohs", 15, 1, ws_htons),
    WS("recv", 16, 4, REAL_OR(winsock_recv, ws_fail)),
    WS("recvfrom", 17, 6, REAL_OR(winsock_recvfrom, ws_fail)),
    WS("select", 18, 5, REAL_OR(winsock_select, ws_select)),
    WS("send", 19, 4, REAL_OR(winsock_send, ws_fail)),
    WS("sendto", 20, 6, REAL_OR(winsock_sendto, ws_fail)),
    WS("setsockopt", 21, 5, REAL_OR(winsock_setsockopt, ws_fail)),
    WS("shutdown", 22, 2, REAL_OR(winsock_shutdown, ws_fail)),
    WS("socket", 23, 3, REAL_OR(winsock_socket, ws_socket)),
    WS("gethostbyaddr", 51, 3, REAL_OR(winsock_gethostbyaddr, ws_null)),
    WS("WSAGetLastError", 111, 0, ws_last_error),
    WS("__WSAFDIsSet", 151, 2, REAL_OR(winsock_fd_isset, ws_fd_isset)),
    {"WS2_32.dll", "WSAIoctl", 9, REAL_OR(winsock_WSAIoctl, ws_fail)},
};
#undef WS
#undef REAL_OR

} // namespace

void legacy_dlls_register() {
    imports_register(shims, sizeof shims / sizeof shims[0]);
}

#ifdef POPM_TESTING
// Lets the tests start each case with no host streams left over.
void legacy_dlls_reset_for_test() {
    streams().clear();
}
#endif
