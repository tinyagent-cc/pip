#include "pip/http.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
namespace pip::http {
namespace {
const char* find(const char* hay, size_t len, const char* needle) {
    size_t n = std::strlen(needle);
    for (size_t i = 0; i + n <= len; ++i)
        if (std::memcmp(hay + i, needle, n) == 0) return hay + i;
    return nullptr;
}
// Case-insensitive header lookup inside [hdr, hdr_end). Returns the value start or nullptr.
const char* header_value(const char* hdr, const char* hdr_end, const char* name) {
    size_t n = std::strlen(name);
    const char* line = hdr;
    while (line < hdr_end) {
        const char* eol = find(line, (size_t)(hdr_end - line), "\r\n");
        if (!eol) eol = hdr_end;
        if ((size_t)(eol - line) > n && line[n] == ':' && strncasecmp(line, name, n) == 0) {
            const char* v = line + n + 1;
            while (v < eol && *v == ' ') ++v;
            return v;
        }
        line = eol + 2;
    }
    return nullptr;
}
}
Parse parse_request(const char* buf, size_t len, Request& out) {
    const char* hdr_end = find(buf, len, "\r\n\r\n");
    if (!hdr_end) return len > 2048 ? Parse::Bad : Parse::Incomplete;
    // The header block, terminator included, is still subject to the 2048 cap even
    // when it is complete; a request that never gets flagged Incomplete because the
    // buffer already holds the terminator must not slip past the same limit.
    if ((size_t)(hdr_end - buf) + 4 > 2048) return Parse::Bad;
    const char* sp1 = static_cast<const char*>(std::memchr(buf, ' ', (size_t)(hdr_end - buf)));
    if (!sp1) return Parse::Bad;
    const char* sp2 = static_cast<const char*>(std::memchr(sp1 + 1, ' ', (size_t)(hdr_end - sp1 - 1)));
    if (!sp2) return Parse::Bad;
    size_t ml = (size_t)(sp1 - buf), pl = (size_t)(sp2 - sp1 - 1);
    if (ml == 0 || ml >= sizeof out.method || pl == 0 || pl >= sizeof out.path) return Parse::Bad;
    std::memcpy(out.method, buf, ml); out.method[ml] = '\0';
    std::memcpy(out.path, sp1 + 1, pl); out.path[pl] = '\0';
    const char* first_eol = find(buf, (size_t)(hdr_end - buf) + 2, "\r\n");
    const char* hdrs = first_eol ? first_eol + 2 : hdr_end;
    size_t clen = 0;
    if (const char* cl = header_value(hdrs, hdr_end, "Content-Length")) clen = std::strtoul(cl, nullptr, 10);
    if (clen > 1024) return Parse::Bad;
    const char* body = hdr_end + 4;
    size_t have = len - (size_t)(body - buf);
    if (have < clen) return Parse::Incomplete;
    out.body = body;
    out.body_len = clen;
    return Parse::Complete;
}
const char* reason(int s) {
    switch (s) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default: return "Error";
    }
}
size_t build_response(char* out, size_t cap, int status, const char* body, const char* extra) {
    unsigned blen = (unsigned)std::strlen(body);
    int n = std::snprintf(out, cap,
        "HTTP/1.0 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nX-Pip-Protocol: 0\r\n%s%sConnection: close\r\n\r\n%s",
        status, reason(status), blen, extra ? extra : "", extra ? "\r\n" : "", body);
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}
}
