// winsock.cpp - WSOCK32 / WS2_32 on the host's BSD sockets, for a game that
// plays over a network (game.toml [game] sockets = true; RECOMP_SOCKETS=0 or 1
// overrides). Without it legacy_dlls.cpp's stand-ins fail every socket call.
//
// A guest SOCKET is a small handle naming a host descriptor. Every host socket
// is non-blocking; a guest socket left blocking (the WinSock default) waits by
// retrying with the scheduler's sleep between tries, so the other guest
// threads run meanwhile. Addresses: the guest's sockaddr_in starts with a
// 16-bit family, the host's with a length byte and a family byte; ports and
// addresses are network order on both. Errors map from errno to WSAE*.
#include "winsock.h"

#include "game_config.h"
#include "guest.h"
#include "imports.h"
#include "memory.h"
#include "win32.h"

#include <algorithm>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <dns_sd.h>
#endif

void sched_sleep_for_host_call(uint32_t ms); // kernel32.cpp

#ifndef RECOMP_GAME_SOCKETS
#define RECOMP_GAME_SOCKETS 0
#endif

namespace {

const uint32_t SOCKET_ERROR = 0xffffffffu, INVALID_SOCKET = 0xffffffffu;
enum : uint32_t {
    WSAEINTR = 10004, WSAEBADF = 10009, WSAEACCES = 10013, WSAEFAULT = 10014, WSAEINVAL = 10022,
    WSAEMFILE = 10024, WSAEWOULDBLOCK = 10035, WSAEINPROGRESS = 10036, WSAEALREADY = 10037,
    WSAENOTSOCK = 10038, WSAEDESTADDRREQ = 10039, WSAEMSGSIZE = 10040, WSAEPROTOTYPE = 10041,
    WSAENOPROTOOPT = 10042, WSAEPROTONOSUPPORT = 10043, WSAEOPNOTSUPP = 10045, WSAEAFNOSUPPORT = 10047,
    WSAEADDRINUSE = 10048, WSAEADDRNOTAVAIL = 10049, WSAENETDOWN = 10050, WSAENETUNREACH = 10051,
    WSAECONNABORTED = 10053, WSAECONNRESET = 10054, WSAENOBUFS = 10055, WSAEISCONN = 10056,
    WSAENOTCONN = 10057, WSAETIMEDOUT = 10060, WSAECONNREFUSED = 10061, WSAEHOSTUNREACH = 10065,
    WSAHOST_NOT_FOUND = 11001,
};

uint32_t wsa_error(int e) {
    switch (e) {
    case EWOULDBLOCK: return WSAEWOULDBLOCK;
    case EINPROGRESS: return WSAEWOULDBLOCK; // a non-blocking connect under way
    case EALREADY: return WSAEALREADY;
    case ENOTSOCK: return WSAENOTSOCK;
    case EDESTADDRREQ: return WSAEDESTADDRREQ;
    case EMSGSIZE: return WSAEMSGSIZE;
    case EPROTOTYPE: return WSAEPROTOTYPE;
    case ENOPROTOOPT: return WSAENOPROTOOPT;
    case EPROTONOSUPPORT: return WSAEPROTONOSUPPORT;
    case EOPNOTSUPP: return WSAEOPNOTSUPP;
    case EAFNOSUPPORT: return WSAEAFNOSUPPORT;
    case EADDRINUSE: return WSAEADDRINUSE;
    case EADDRNOTAVAIL: return WSAEADDRNOTAVAIL;
    case ENETDOWN: return WSAENETDOWN;
    case ENETUNREACH: return WSAENETUNREACH;
    case ECONNABORTED: return WSAECONNABORTED;
    case ECONNRESET: return WSAECONNRESET;
    case EPIPE: return WSAECONNRESET;
    case ENOBUFS: return WSAENOBUFS;
    case EISCONN: return WSAEISCONN;
    case ENOTCONN: return WSAENOTCONN;
    case ETIMEDOUT: return WSAETIMEDOUT;
    case ECONNREFUSED: return WSAECONNREFUSED;
    case EHOSTUNREACH: return WSAEHOSTUNREACH;
    case EACCES: return WSAEACCES;
    case EBADF: return WSAENOTSOCK;
    case EFAULT: return WSAEFAULT;
    case EINTR: return WSAEINTR;
    case EMFILE: return WSAEMFILE;
    default: return WSAEINVAL;
    }
}

bool trace() {
    static int on = -1;
    if (on < 0)
        on = getenv("RECOMP_TRACE_SOCKETS") != nullptr;
    return on;
}

struct Sock {
    int fd = -1;
    int type = 0;            // SOCK_STREAM 1, SOCK_DGRAM 2
    bool blocking = true;    // the guest's view (FIONBIO)
    uint32_t rcv_timeout = 0; // ms, SO_RCVTIMEO; 0 = forever
    uint32_t snd_timeout = 0;
    bool broadcast = false;
};

std::map<uint32_t, Sock> &socks() {
    static std::map<uint32_t, Sock> m;
    return m;
}
uint32_t g_next_handle = 0x100;

Sock *find(uint32_t s) {
    auto it = socks().find(s);
    return it == socks().end() ? nullptr : &it->second;
}

uint32_t fail(X86 *c, uint32_t wsa) {
    set_last_error(wsa);
    set_eax(c, SOCKET_ERROR);
    return SOCKET_ERROR;
}

double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// ------------------------------------------------------------ addresses --

// The guest's sockaddr_in (16 bytes: family16, port, addr, zero[8]) at `a`.
bool to_host(uint32_t a, uint32_t len, sockaddr_in *out) {
    if (!a || len < 8)
        return false;
    memset(out, 0, sizeof *out);
    out->sin_len = sizeof *out;
    out->sin_family = (sa_family_t)rd16(a);
    memcpy(&out->sin_port, g_mem + a + 2, 2);
    memcpy(&out->sin_addr, g_mem + a + 4, 4);
    return out->sin_family == AF_INET;
}

void to_guest(const sockaddr_in &in, uint32_t a, uint32_t lenp) {
    if (!a)
        return;
    uint32_t cap = lenp ? rd32(lenp) : 16;
    uint8_t buf[16] = {};
    buf[0] = AF_INET;
    memcpy(buf + 2, &in.sin_port, 2);
    memcpy(buf + 4, &in.sin_addr, 4);
    memcpy(g_mem + a, buf, cap < 16 ? cap : 16);
    if (lenp)
        wr32(lenp, 16);
}

std::string addr_str(const sockaddr_in &a) {
    char s[32];
    snprintf(s, sizeof s, "%s:%u", inet_ntoa(a.sin_addr), ntohs(a.sin_port));
    return s;
}

// The host's IPv4 interfaces that are up: {address, netmask, broadcast, flags}.
struct Iface {
    in_addr addr, mask, bcast;
    unsigned flags;
};
std::vector<Iface> ifaces() {
    std::vector<Iface> out;
    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0)
        return out;
    for (struct ifaddrs *i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP))
            continue;
        Iface f{};
        f.addr = ((sockaddr_in *)i->ifa_addr)->sin_addr;
        f.mask = i->ifa_netmask ? ((sockaddr_in *)i->ifa_netmask)->sin_addr : in_addr{0};
        f.bcast.s_addr = f.addr.s_addr | ~f.mask.s_addr;
        f.flags = i->ifa_flags;
        out.push_back(f);
    }
    freeifaddrs(list);
    // Real interfaces first, loopback last.
    std::stable_sort(out.begin(), out.end(),
                     [](const Iface &a, const Iface &b) { return !(a.flags & IFF_LOOPBACK) && (b.flags & IFF_LOOPBACK); });
    return out;
}

// ------------------------------------------------------ broadcast relay --
// iOS lets an app send and receive UDP broadcasts only with Apple's multicast
// entitlement, so a game's LAN discovery (a broadcast probe, unicast answers)
// would see no one. Every copy of the app on the network announces itself
// over Bonjour (_hommare._udp, named after the game, so each game sees its
// own), and a datagram the guest broadcasts also goes straight to each such
// peer, at the same port. Peers that can broadcast simply get it twice.
// RECOMP_LAN_RELAY=0 turns it off.

std::mutex g_relay_lock;
std::map<std::string, in_addr> g_peers; // Bonjour name -> IPv4

#ifdef __APPLE__
std::string g_relay_self;
DNSServiceRef g_relay_conn = nullptr;

void relay_got_addr(DNSServiceRef, DNSServiceFlags flags, uint32_t, DNSServiceErrorType err, const char *,
                    const struct sockaddr *addr, uint32_t, void *ctx) {
    std::string *name = (std::string *)ctx;
    if (!err && addr && addr->sa_family == AF_INET && (flags & kDNSServiceFlagsAdd)) {
        std::lock_guard<std::mutex> l(g_relay_lock);
        g_peers[*name] = ((const sockaddr_in *)addr)->sin_addr;
        if (trace())
            fprintf(stderr, "[sockets] relay peer %s at %s\n", name->c_str(),
                    inet_ntoa(((const sockaddr_in *)addr)->sin_addr));
    }
}

void relay_resolved(DNSServiceRef, DNSServiceFlags, uint32_t ifindex, DNSServiceErrorType err, const char *,
                    const char *host, uint16_t, uint16_t, const unsigned char *, void *ctx) {
    if (err)
        return;
    DNSServiceRef ref = g_relay_conn;
    DNSServiceGetAddrInfo(&ref, kDNSServiceFlagsShareConnection, ifindex, kDNSServiceProtocol_IPv4, host,
                          relay_got_addr, ctx);
}

void relay_browsed(DNSServiceRef, DNSServiceFlags flags, uint32_t ifindex, DNSServiceErrorType err,
                   const char *name, const char *type, const char *domain, void *) {
    if (err || name == g_relay_self)
        return;
    std::string prefix = std::string(RECOMP_GAME_ID) + " ";
    if (strncmp(name, prefix.c_str(), prefix.size()) != 0)
        return; // another game's
    if (!(flags & kDNSServiceFlagsAdd)) {
        std::lock_guard<std::mutex> l(g_relay_lock);
        g_peers.erase(name);
        return;
    }
    // Kept for the callbacks' lifetime: the app's (few peers come and go).
    std::string *keep = new std::string(name);
    DNSServiceRef ref = g_relay_conn;
    DNSServiceResolve(&ref, kDNSServiceFlagsShareConnection, ifindex, name, type, domain, relay_resolved, keep);
}
#endif

void relay_start() {
#ifdef __APPLE__
    static std::once_flag once;
    std::call_once(once, [] {
        const char *v = getenv("RECOMP_LAN_RELAY");
        if (v && *v == '0')
            return;
        if (DNSServiceCreateConnection(&g_relay_conn) != kDNSServiceErr_NoError)
            return;
        char self[64];
        snprintf(self, sizeof self, "%s %08x", RECOMP_GAME_ID, (unsigned)arc4random());
        g_relay_self = self;
        DNSServiceRef reg = g_relay_conn, browse = g_relay_conn;
        DNSServiceRegister(&reg, kDNSServiceFlagsShareConnection, 0, self, "_hommare._udp", nullptr, nullptr,
                           htons(9), 0, nullptr, nullptr, nullptr);
        DNSServiceBrowse(&browse, kDNSServiceFlagsShareConnection, 0, "_hommare._udp", nullptr, relay_browsed,
                         nullptr);
        std::thread([] {
            int fd = DNSServiceRefSockFD(g_relay_conn);
            for (;;) {
                struct pollfd p{fd, POLLIN, 0};
                if (poll(&p, 1, 1000) > 0 && DNSServiceProcessResult(g_relay_conn) != kDNSServiceErr_NoError)
                    return;
            }
        }).detach();
        if (trace())
            fprintf(stderr, "[sockets] relay: announced as \"%s\"\n", self);
    });
#endif
}

bool is_broadcast(in_addr a) {
    if (a.s_addr == INADDR_BROADCAST)
        return true;
    for (const Iface &f : ifaces())
        if (!(f.flags & IFF_LOOPBACK) && a.s_addr == f.bcast.s_addr)
            return true;
    return false;
}

// Copies of a broadcast datagram to each Bonjour peer (once per peer address).
void relay_send(int fd, const void *data, size_t len, sockaddr_in to) {
    std::set<in_addr_t> sent;
    std::lock_guard<std::mutex> l(g_relay_lock);
    for (auto &p : g_peers) {
        if (!sent.insert(p.second.s_addr).second)
            continue;
        to.sin_addr = p.second;
        sendto(fd, data, len, 0, (sockaddr *)&to, sizeof to);
    }
}

// --------------------------------------------------------------- waiting --

// Polls `fd` for `events` until ready or `timeout_ms` (0 = forever) passes,
// letting other guest threads run between tries. True when ready.
bool wait_fd(uint32_t s, short events, uint32_t timeout_ms) {
    double end = timeout_ms ? now_ms() + timeout_ms : 0;
    for (;;) {
        Sock *k = find(s);
        if (!k)
            return false;
        struct pollfd p{k->fd, events, 0};
        if (poll(&p, 1, 0) > 0)
            return true;
        if (end && now_ms() >= end)
            return false;
        sched_sleep_for_host_call(2);
    }
}

} // namespace

// ------------------------------------------------------------- the calls --

bool winsock_real() {
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_SOCKETS");
        on = v && *v ? *v == '1' : RECOMP_GAME_SOCKETS;
        if (on)
            fprintf(stderr, "[sockets] WinSock on the host's sockets\n");
    }
    return on;
}

void winsock_socket(X86 *c) {
    uint32_t af = arg(c, 0), type = arg(c, 1), proto = arg(c, 2);
    if (af != AF_INET || (type != SOCK_STREAM && type != SOCK_DGRAM)) {
        set_last_error(af != AF_INET ? WSAEAFNOSUPPORT : WSAEPROTOTYPE);
        set_eax(c, INVALID_SOCKET);
        return;
    }
    int fd = socket(AF_INET, (int)type, (int)proto);
    if (fd < 0) {
        set_last_error(wsa_error(errno));
        set_eax(c, INVALID_SOCKET);
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    if (type == SOCK_DGRAM)
        relay_start();
    uint32_t h = g_next_handle++;
    Sock k;
    k.fd = fd;
    k.type = (int)type;
    socks()[h] = k;
    if (trace())
        fprintf(stderr, "[sockets] socket(%s) = %#x\n", type == SOCK_STREAM ? "tcp" : "udp", h);
    set_eax(c, h);
}

void winsock_closesocket(X86 *c) {
    uint32_t s = arg(c, 0);
    Sock *k = find(s);
    if (!k) {
        fail(c, WSAENOTSOCK);
        return;
    }
    close(k->fd);
    socks().erase(s);
    set_eax(c, 0);
}

void winsock_bind(X86 *c) {
    Sock *k = find(arg(c, 0));
    sockaddr_in a;
    if (!k)
        return (void)fail(c, WSAENOTSOCK);
    if (!to_host(arg(c, 1), arg(c, 2), &a))
        return (void)fail(c, WSAEFAULT);
    if (k->type == 2) {
        // The lobby's broadcast port, shared by copies of the game on one
        // machine (simulators side by side): each hears the broadcasts.
        int on = 1;
        setsockopt(k->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        setsockopt(k->fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof on);
    }
    if (bind(k->fd, (sockaddr *)&a, sizeof a) != 0) {
        if (trace())
            fprintf(stderr, "[sockets] bind(%#x, %s) failed: %s\n", arg(c, 0), addr_str(a).c_str(), strerror(errno));
        return (void)fail(c, wsa_error(errno));
    }
    if (trace())
        fprintf(stderr, "[sockets] bind(%#x, %s)\n", arg(c, 0), addr_str(a).c_str());
    set_eax(c, 0);
}

void winsock_connect(X86 *c) {
    uint32_t s = arg(c, 0);
    Sock *k = find(s);
    sockaddr_in a;
    if (!k)
        return (void)fail(c, WSAENOTSOCK);
    if (!to_host(arg(c, 1), arg(c, 2), &a))
        return (void)fail(c, WSAEFAULT);
    if (trace())
        fprintf(stderr, "[sockets] connect(%#x, %s)\n", s, addr_str(a).c_str());
    if (connect(k->fd, (sockaddr *)&a, sizeof a) == 0) {
        set_eax(c, 0);
        return;
    }
    int e = errno;
    if (e != EINPROGRESS || (k->type == SOCK_DGRAM))
        return (void)fail(c, wsa_error(e));
    if (!k->blocking)
        return (void)fail(c, WSAEWOULDBLOCK);
    if (!wait_fd(s, POLLOUT, 30000))
        return (void)fail(c, WSAETIMEDOUT);
    k = find(s);
    int err = 0;
    socklen_t len = sizeof err;
    if (!k || getsockopt(k->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err)
        return (void)fail(c, wsa_error(err ? err : ENOTCONN));
    set_eax(c, 0);
}

void winsock_listen(X86 *c) {
    Sock *k = find(arg(c, 0));
    if (!k)
        return (void)fail(c, WSAENOTSOCK);
    if (listen(k->fd, (int)arg(c, 1)) != 0)
        return (void)fail(c, wsa_error(errno));
    set_eax(c, 0);
}

void winsock_accept(X86 *c) {
    uint32_t s = arg(c, 0);
    for (;;) {
        Sock *k = find(s);
        if (!k) {
            fail(c, WSAENOTSOCK);
            set_eax(c, INVALID_SOCKET);
            return;
        }
        sockaddr_in a{};
        socklen_t len = sizeof a;
        int fd = accept(k->fd, (sockaddr *)&a, &len);
        if (fd >= 0) {
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
            uint32_t h = g_next_handle++;
            Sock n;
            n.fd = fd;
            n.type = SOCK_STREAM;
            socks()[h] = n;
            to_guest(a, arg(c, 1), arg(c, 2));
            set_eax(c, h);
            return;
        }
        if (errno != EWOULDBLOCK || !k->blocking) {
            set_last_error(wsa_error(errno));
            set_eax(c, INVALID_SOCKET);
            return;
        }
        if (!wait_fd(s, POLLIN, 0)) {
            set_last_error(WSAENOTSOCK);
            set_eax(c, INVALID_SOCKET);
            return;
        }
    }
}

// send / sendto: (s, buf, len, flags[, to, tolen]).
static void do_send(X86 *c, bool to) {
    uint32_t s = arg(c, 0), buf = arg(c, 1), len = arg(c, 2), flags = arg(c, 3);
    sockaddr_in a;
    bool have_to = to && arg(c, 4);
    if (have_to && !to_host(arg(c, 4), arg(c, 5), &a))
        return (void)fail(c, WSAEFAULT);
    for (;;) {
        Sock *k = find(s);
        if (!k)
            return (void)fail(c, WSAENOTSOCK);
        bool bcast = have_to && k->type == SOCK_DGRAM && is_broadcast(a.sin_addr);
        if (bcast)
            relay_send(k->fd, g_mem + buf, len, a);
        ssize_t n = have_to ? sendto(k->fd, g_mem + buf, len, (int)(flags & 0x3), (sockaddr *)&a, sizeof a)
                            : send(k->fd, g_mem + buf, len, (int)(flags & 0x3));
        // A broadcast the system refuses (iOS without the multicast
        // entitlement) still reached the relay's peers: report it sent.
        if (n < 0 && bcast && errno != EWOULDBLOCK)
            n = (ssize_t)len;
        if (n >= 0) {
            if (trace() && have_to)
                fprintf(stderr, "[sockets] sendto(%#x, %u bytes, %s)\n", s, len, addr_str(a).c_str());
            set_eax(c, (uint32_t)n);
            return;
        }
        int e = errno;
        if (trace())
            fprintf(stderr, "[sockets] send%s(%#x, %u bytes%s%s) failed: %s\n", have_to ? "to" : "", s, len,
                    have_to ? ", " : "", have_to ? addr_str(a).c_str() : "", strerror(e));
        if (e != EWOULDBLOCK || !k->blocking)
            return (void)fail(c, wsa_error(e));
        if (!wait_fd(s, POLLOUT, k->snd_timeout))
            return (void)fail(c, WSAETIMEDOUT);
    }
}
void winsock_send(X86 *c) {
    do_send(c, false);
}
void winsock_sendto(X86 *c) {
    do_send(c, true);
}

// recv / recvfrom: (s, buf, len, flags[, from, fromlen]). A datagram longer
// than the buffer fills it and fails with WSAEMSGSIZE, as on Windows.
static void do_recv(X86 *c, bool from) {
    uint32_t s = arg(c, 0), buf = arg(c, 1), len = arg(c, 2), flags = arg(c, 3);
    for (;;) {
        Sock *k = find(s);
        if (!k)
            return (void)fail(c, WSAENOTSOCK);
        sockaddr_in a{};
        struct iovec io{g_mem + buf, len};
        struct msghdr m{};
        m.msg_name = &a;
        m.msg_namelen = sizeof a;
        m.msg_iov = &io;
        m.msg_iovlen = 1;
        ssize_t n = recvmsg(k->fd, &m, (int)(flags & 0x3));
        if (n >= 0) {
            if (from)
                to_guest(a, arg(c, 4), arg(c, 5));
            if (trace() && from)
                fprintf(stderr, "[sockets] recvfrom(%#x) %zd bytes from %s\n", s, n, addr_str(a).c_str());
            if (k->type == SOCK_DGRAM && (m.msg_flags & MSG_TRUNC))
                return (void)fail(c, WSAEMSGSIZE);
            set_eax(c, (uint32_t)n);
            return;
        }
        int e = errno;
        if (e != EWOULDBLOCK || !k->blocking)
            return (void)fail(c, wsa_error(e));
        if (!wait_fd(s, POLLIN, k->rcv_timeout))
            return (void)fail(c, WSAETIMEDOUT);
    }
}
void winsock_recv(X86 *c) {
    do_recv(c, false);
}
void winsock_recvfrom(X86 *c) {
    do_recv(c, true);
}

// select(nfds, readfds, writefds, exceptfds, timeout). A guest fd_set is
// {u32 count; u32 sockets[64]}; a timeval {i32 sec; i32 usec}, none = forever.
void winsock_select(X86 *c) {
    uint32_t sets[3] = {arg(c, 1), arg(c, 2), arg(c, 3)}, tv = arg(c, 4);
    double timeout = tv ? (int32_t)rd32(tv) * 1000.0 + (int32_t)rd32(tv + 4) / 1000.0 : -1;
    double end = now_ms() + (timeout < 0 ? 0 : timeout);
    std::vector<uint32_t> want[3];
    for (int i = 0; i < 3; ++i)
        if (sets[i]) {
            uint32_t n = rd32(sets[i]);
            for (uint32_t j = 0; j < n && j < 64; ++j)
                want[i].push_back(rd32(sets[i] + 4 + 4 * j));
        }
    const short ev[3] = {POLLIN, POLLOUT, POLLPRI};
    for (;;) {
        std::vector<uint32_t> ready[3];
        uint32_t total = 0;
        for (int i = 0; i < 3; ++i)
            for (uint32_t s : want[i]) {
                Sock *k = find(s);
                if (!k)
                    continue;
                struct pollfd p{k->fd, ev[i], 0};
                // A closed or failed peer counts as readable (the recv tells).
                if (poll(&p, 1, 0) > 0 && (p.revents & (ev[i] | POLLHUP | POLLERR))) {
                    if (i == 2 && !(p.revents & POLLPRI))
                        continue;
                    ready[i].push_back(s);
                    ++total;
                }
            }
        if (total || (timeout >= 0 && now_ms() >= end)) {
            for (int i = 0; i < 3; ++i)
                if (sets[i]) {
                    wr32(sets[i], (uint32_t)ready[i].size());
                    for (size_t j = 0; j < ready[i].size(); ++j)
                        wr32(sets[i] + 4 + 4 * (uint32_t)j, ready[i][j]);
                }
            set_eax(c, total);
            return;
        }
        sched_sleep_for_host_call(timeout < 0 || end - now_ms() > 2 ? 2 : 1);
    }
}

void winsock_fd_isset(X86 *c) {
    uint32_t s = arg(c, 0), set = arg(c, 1);
    uint32_t n = set ? rd32(set) : 0;
    for (uint32_t j = 0; j < n && j < 64; ++j)
        if (rd32(set + 4 + 4 * j) == s) {
            set_eax(c, 1);
            return;
        }
    set_eax(c, 0);
}

void winsock_ioctlsocket(X86 *c) {
    Sock *k = find(arg(c, 0));
    uint32_t cmd = arg(c, 1), argp = arg(c, 2);
    if (!k)
        return (void)fail(c, WSAENOTSOCK);
    if (cmd == 0x8004667Eu) { // FIONBIO
        k->blocking = argp ? rd32(argp) == 0 : true;
    } else if (cmd == 0x4004667Fu) { // FIONREAD
        int n = 0;
        ioctl(k->fd, FIONREAD, &n);
        if (argp)
            wr32(argp, (uint32_t)n);
    } else {
        return (void)fail(c, WSAEINVAL);
    }
    set_eax(c, 0);
}

// setsockopt(s, level, name, val, len) / getsockopt(s, level, name, val, lenp).
// SOL_SOCKET is 0xffff and the common option numbers are the BSD ones.
void winsock_setsockopt(X86 *c) {
    Sock *k = find(arg(c, 0));
    uint32_t level = arg(c, 1), name = arg(c, 2), val = arg(c, 3), len = arg(c, 4);
    if (!k)
        return (void)fail(c, WSAENOTSOCK);
    uint32_t v = val && len >= 4 ? rd32(val) : val && len ? rd8(val) : 0;
    if (trace())
        fprintf(stderr, "[sockets] setsockopt(%#x, %#x, %#x, %u)\n", arg(c, 0), level, name, v);
    int iv = (int)v;
    if (level == 0xffff) {
        switch (name) {
        case 0x1005: k->snd_timeout = v; set_eax(c, 0); return; // SO_SNDTIMEO (ms)
        case 0x1006: k->rcv_timeout = v; set_eax(c, 0); return; // SO_RCVTIMEO (ms)
        case 0x20: k->broadcast = v != 0; break;                // SO_BROADCAST
        case 0xff7f: name = SO_LINGER; break;                   // SO_DONTLINGER
        default: break;
        }
        if (name == SO_LINGER) {
            struct linger l{arg(c, 2) == 0xff7f ? 0 : (int)(v & 0xffff), (int)(v >> 16)};
            setsockopt(k->fd, SOL_SOCKET, SO_LINGER, &l, sizeof l);
        } else if (setsockopt(k->fd, SOL_SOCKET, (int)name, &iv, sizeof iv) != 0) {
            if (trace())
                fprintf(stderr, "[sockets]   ignored: %s\n", strerror(errno));
        }
        if (name == SO_REUSEADDR) // two copies on one machine, as Windows lets them share a port
            setsockopt(k->fd, SOL_SOCKET, SO_REUSEPORT, &iv, sizeof iv);
    } else if (level == IPPROTO_TCP && name == 1) {
        setsockopt(k->fd, IPPROTO_TCP, TCP_NODELAY, &iv, sizeof iv);
    }
    // Other options (IP multicast, keep-alive tuning...) are accepted unchanged.
    set_eax(c, 0);
}

void winsock_getsockopt(X86 *c) {
    Sock *k = find(arg(c, 0));
    uint32_t level = arg(c, 1), name = arg(c, 2), val = arg(c, 3), lenp = arg(c, 4);
    if (!k)
        return (void)fail(c, WSAENOTSOCK);
    int iv = 0;
    socklen_t len = sizeof iv;
    if (level == 0xffff && name == 0x1007) { // SO_ERROR
        getsockopt(k->fd, SOL_SOCKET, SO_ERROR, &iv, &len);
        iv = iv ? (int)wsa_error(iv) : 0;
    } else if (level == 0xffff && name == 0x1006) {
        iv = (int)k->rcv_timeout;
    } else if (level == 0xffff && name == 0x1005) {
        iv = (int)k->snd_timeout;
    } else if (level == 0xffff) {
        getsockopt(k->fd, SOL_SOCKET, (int)name, &iv, &len);
    } else if (level == IPPROTO_TCP && name == 1) {
        getsockopt(k->fd, IPPROTO_TCP, TCP_NODELAY, &iv, &len);
    }
    if (val)
        wr32(val, (uint32_t)iv);
    if (lenp)
        wr32(lenp, 4);
    set_eax(c, 0);
}

static void name_of(X86 *c, bool peer) {
    Sock *k = find(arg(c, 0));
    if (!k)
        return (void)fail(c, WSAENOTSOCK);
    sockaddr_in a{};
    socklen_t len = sizeof a;
    int r = peer ? getpeername(k->fd, (sockaddr *)&a, &len) : getsockname(k->fd, (sockaddr *)&a, &len);
    if (r != 0)
        return (void)fail(c, wsa_error(errno));
    to_guest(a, arg(c, 1), arg(c, 2));
    set_eax(c, 0);
}
void winsock_getsockname(X86 *c) {
    name_of(c, false);
}
void winsock_getpeername(X86 *c) {
    name_of(c, true);
}

void winsock_shutdown(X86 *c) {
    Sock *k = find(arg(c, 0));
    if (!k)
        return (void)fail(c, WSAENOTSOCK);
    shutdown(k->fd, (int)arg(c, 1));
    set_eax(c, 0);
}

// ------------------------------------------------------------- host names --

// The name gethostname gives: the game's id, which gethostbyname then knows as
// this machine's own addresses.
static const char *own_name() {
    return RECOMP_GAME_ID;
}

void winsock_gethostname(X86 *c) {
    uint32_t buf = arg(c, 0), len = arg(c, 1);
    if (buf && len)
        gm_put_str(buf, own_name(), len);
    set_eax(c, 0);
}

// A guest hostent {char *name; char **aliases; i16 type; i16 length; char **addrs}
// (one per thread in WinSock; one in all here), rebuilt on each call.
static uint32_t guest_hostent(const std::string &name, const std::vector<in_addr> &addrs) {
    static uint32_t block = 0;
    const uint32_t size = 0x400;
    if (!block || !heap_owns(block))
        block = heap_alloc(size, true);
    if (!block)
        return 0;
    memset(g_mem + block, 0, size);
    uint32_t he = block, name_at = block + 0x20, aliases = block + 0x80, list = block + 0x88,
             data = block + 0x100;
    size_t n = addrs.size() < 16 ? addrs.size() : 16;
    gm_put_str(name_at, name.c_str(), 0x5f);
    wr32(he + 0, name_at);
    wr32(he + 4, aliases); // an empty list
    wr16(he + 8, AF_INET);
    wr16(he + 10, 4);
    wr32(he + 12, list);
    for (size_t i = 0; i < n; ++i) {
        memcpy(g_mem + data + 4 * i, &addrs[i], 4);
        wr32(list + 4 * (uint32_t)i, data + 4 * (uint32_t)i);
    }
    wr32(list + 4 * (uint32_t)n, 0);
    return he;
}

void winsock_gethostbyname(X86 *c) {
    std::string name = gm_str(arg(c, 0), 256);
    std::vector<in_addr> addrs;
    if (name.empty() || name == own_name()) {
        for (const Iface &f : ifaces())
            addrs.push_back(f.addr);
    } else {
        in_addr a;
        if (inet_aton(name.c_str(), &a)) {
            addrs.push_back(a);
        } else {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            if (getaddrinfo(name.c_str(), nullptr, &hints, &res) == 0) {
                for (struct addrinfo *r = res; r; r = r->ai_next)
                    addrs.push_back(((sockaddr_in *)r->ai_addr)->sin_addr);
                freeaddrinfo(res);
            }
        }
    }
    if (trace())
        fprintf(stderr, "[sockets] gethostbyname(\"%s\"): %zu addresses%s%s\n", name.c_str(), addrs.size(),
                addrs.empty() ? "" : ", first ", addrs.empty() ? "" : inet_ntoa(addrs[0]));
    if (addrs.empty()) {
        set_last_error(WSAHOST_NOT_FOUND);
        set_eax(c, 0);
        return;
    }
    set_eax(c, guest_hostent(name, addrs));
}

void winsock_gethostbyaddr(X86 *c) {
    uint32_t p = arg(c, 0), len = arg(c, 1);
    if (!p || len < 4) {
        set_last_error(WSAEFAULT);
        set_eax(c, 0);
        return;
    }
    in_addr a;
    memcpy(&a, g_mem + p, 4);
    set_eax(c, guest_hostent(inet_ntoa(a), {a}));
}

// WSAIoctl(s, code, in, inlen, out, outlen, returned, overlapped, routine):
// SIO_GET_INTERFACE_LIST fills INTERFACE_INFO records (76 bytes: flags, then
// address, broadcast and netmask as 24-byte sockaddr_gen).
void winsock_WSAIoctl(X86 *c) {
    uint32_t code = arg(c, 1), out = arg(c, 4), outlen = arg(c, 5), returned = arg(c, 6);
    if (code != 0x4004747Fu)
        return (void)fail(c, WSAEINVAL);
    std::vector<Iface> list = ifaces();
    uint32_t n = 0;
    for (const Iface &f : list) {
        if ((n + 1) * 76 > outlen)
            break;
        uint32_t r = out + 76 * n++;
        memset(g_mem + r, 0, 76);
        uint32_t flags = 1 | ((f.flags & IFF_BROADCAST) ? 2 : 0) | ((f.flags & IFF_LOOPBACK) ? 4 : 0) |
                         ((f.flags & IFF_POINTOPOINT) ? 8 : 0) | ((f.flags & IFF_MULTICAST) ? 0x10 : 0);
        wr32(r, flags);
        const in_addr *three[3] = {&f.addr, &f.bcast, &f.mask};
        for (int i = 0; i < 3; ++i) {
            uint32_t a = r + 4 + 24 * (uint32_t)i;
            wr16(a, AF_INET);
            memcpy(g_mem + a + 4, three[i], 4);
        }
    }
    if (returned)
        wr32(returned, 76 * n);
    if (trace())
        fprintf(stderr, "[sockets] WSAIoctl(SIO_GET_INTERFACE_LIST): %u interfaces\n", n);
    set_eax(c, 0);
}
