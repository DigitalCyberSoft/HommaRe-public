// http_get_none.cpp - http_get (http_get.h) for a build without POSIX
// sockets (Windows): every request fails, as libcurl's offline stubs did.
// Once the platform layer carries sockets (platform/os.h), this becomes one
// implementation over those.
#include "http_get.h"

HttpGetResult http_get(const std::string &, uint16_t, const std::string &, int, size_t) {
    HttpGetResult r;
    r.error = "network access is not available in this build";
    return r;
}
