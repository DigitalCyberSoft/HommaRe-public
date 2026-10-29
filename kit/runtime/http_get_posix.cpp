// http_get_posix.cpp - http_get (http_get.h) over POSIX sockets: Linux,
// macOS, iOS and Android alike. Only what all of them have: getaddrinfo,
// non-blocking connect and poll for the deadline, and no MSG_NOSIGNAL where it
// does not exist (Apple's sockets take SO_NOSIGPIPE instead), so a server that
// hangs up mid-request is an error here and never a SIGPIPE that ends the game.
//
// iOS: an address on the player's own network (192.168.x.x, a .local name)
// may make the system ask for Local Network permission the first time, and is
// refused until the player allows it; the app's Info.plist has to say why it
// asks (NSLocalNetworkUsageDescription). A server on the Internet needs
// nothing.
#include "http_get.h"
#include "../platform/os.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

double now_ms() {
    return double(os_monotonic_ns()) / 1e6;
}

// What an errno means to the person who typed the address.
std::string describe(int err) {
    switch (err) {
    case ECONNREFUSED:
        return "connection refused (nothing is listening on that port)";
    case ETIMEDOUT:
        return "the connection timed out";
    case EHOSTUNREACH:
        return "the host is unreachable";
    case ENETUNREACH:
        return "the network is unreachable";
    case ECONNRESET:
        return "the server closed the connection";
    case EPIPE:
        return "the server closed the connection";
    case EACCES:
    case EPERM:
        return "the system did not allow the connection";
    default:
        return strerror(err);
    }
}

// Waits until `fd` can do `events` or the deadline passes. 1 ready, 0 timed
// out, -1 failed (errno set).
int wait_for(int fd, short events, double deadline) {
    for (;;) {
        const double left = deadline - now_ms();
        if (left <= 0)
            return 0;
        pollfd p{fd, events, 0};
        const int n = poll(&p, 1, int(left) + 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return n;
        return 1;
    }
}

// A connected, non-blocking socket to one of `ai`'s addresses, or -1 with
// *err set to the last address's failure.
int connect_any(const addrinfo *ai, double deadline, int *err, bool *timed_out) {
    *err = 0;
    for (const addrinfo *a = ai; a; a = a->ai_next) {
        int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            *err = errno;
            continue;
        }
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        const int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            return fd;
        if (errno != EINPROGRESS && errno != EINTR) {
            *err = errno;
            close(fd);
            continue;
        }
        const int ready = wait_for(fd, POLLOUT, deadline);
        if (ready == 1) {
            int so = 0;
            socklen_t len = sizeof so;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &len) == 0 && so == 0)
                return fd;
            *err = so ? so : errno;
        } else if (ready == 0) {
            *timed_out = true;
            *err = ETIMEDOUT;
        } else {
            *err = errno;
        }
        close(fd);
        if (*timed_out)
            return -1; // the deadline is shared: no time left for another address
    }
    return -1;
}

} // namespace

HttpGetResult http_get(const std::string &host, uint16_t port, const std::string &target,
                       int timeout_ms, size_t max_body) {
    HttpGetResult r;
    const double deadline = now_ms() + timeout_ms;
    const int seconds = (timeout_ms + 999) / 1000;
    if (host.empty() || !port || target.empty() || target[0] != '/') {
        r.error = "not a usable address";
        return r;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *ai = nullptr;
    char service[8];
    snprintf(service, sizeof service, "%u", unsigned(port));
    const int gai = getaddrinfo(host.c_str(), service, &hints, &ai);
    if (gai != 0 || !ai) {
        r.unresolved = true;
        r.error = "cannot find the host \"" + host + "\" (" + gai_strerror(gai) + ")";
        return r;
    }
    int err = 0;
    bool timed_out = false;
    const int fd = connect_any(ai, deadline, &err, &timed_out);
    freeaddrinfo(ai);
    if (fd < 0) {
        r.timed_out = timed_out;
        r.error = timed_out ? "no answer within " + std::to_string(seconds) + " s" : describe(err);
        return r;
    }

    // An IPv6 literal goes back into brackets for the Host header.
    const bool v6 = host.find(':') != std::string::npos;
    std::string authority = v6 ? "[" + host + "]" : host;
    if (port != 80)
        authority += ":" + std::to_string(port);
    const std::string request =
        "GET " + target + " HTTP/1.0\r\nHost: " + authority +
        "\r\nUser-Agent: recomp-kit\r\nAccept: */*\r\nConnection: close\r\n\r\n";
#ifdef MSG_NOSIGNAL
    const int send_flags = MSG_NOSIGNAL;
#else
    const int send_flags = 0; // SO_NOSIGPIPE, set above, does the same
#endif
    size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t n = send(fd, request.data() + sent, request.size() - sent, send_flags);
        if (n > 0) {
            sent += size_t(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            const int ready = wait_for(fd, POLLOUT, deadline);
            if (ready == 1)
                continue;
            r.timed_out = ready == 0;
            r.error =
                ready == 0 ? "no answer within " + std::to_string(seconds) + " s" : describe(errno);
        } else {
            r.error = describe(n < 0 ? errno : EPIPE);
        }
        close(fd);
        return r;
    }

    // Everything until the server closes, a little over max_body for the headers.
    std::string raw;
    char buf[4096];
    for (;;) {
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n > 0) {
            raw.append(buf, size_t(n));
            if (raw.size() > max_body + 16384) {
                close(fd);
                r.error = "the reply is too large";
                return r;
            }
            continue;
        }
        if (n == 0)
            break;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            const int ready = wait_for(fd, POLLIN, deadline);
            if (ready == 1)
                continue;
            r.timed_out = ready == 0;
            r.error = ready == 0 ? "no complete answer within " + std::to_string(seconds) + " s"
                                 : describe(errno);
            close(fd);
            return r;
        }
        if (errno == ECONNRESET && !raw.empty())
            break; // some servers reset rather than close once they are done
        r.error = describe(errno);
        close(fd);
        return r;
    }
    close(fd);
    if (!http_parse_response(raw, max_body, &r) && r.error.empty())
        r.error = raw.empty() ? "the server closed the connection without answering"
                              : "the reply is not HTTP";
    return r;
}
