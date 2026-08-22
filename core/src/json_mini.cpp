#include "pip/json_mini.hpp"
#include <cstring>
namespace pip::json {
namespace {
bool ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
// Returns a pointer to the first byte of the value for "key": ..., or nullptr.
const char* find_value(const char* obj, size_t len, const char* key) {
    size_t klen = std::strlen(key);
    const char* end = obj + len;
    for (const char* p = obj; p + klen + 2 <= end; ++p) {
        if (p[0] != '"' || std::memcmp(p + 1, key, klen) != 0 || p[klen + 1] != '"') continue;
        const char* q = p + klen + 2;
        while (q < end && ws(*q)) ++q;
        if (q >= end || *q != ':') continue;
        ++q;
        while (q < end && ws(*q)) ++q;
        return q < end ? q : nullptr;
    }
    return nullptr;
}
}
bool get_string(const char* obj, size_t len, const char* key, char* out, size_t cap) {
    const char* v = find_value(obj, len, key);
    if (!v || *v != '"' || cap == 0) return false;
    const char* end = obj + len;
    ++v;
    size_t n = 0;
    while (v < end && *v != '"') {
        if (*v == '\\' || n + 1 >= cap) return false;
        out[n++] = *v++;
    }
    if (v >= end) return false;
    out[n] = '\0';
    return true;
}
bool get_int(const char* obj, size_t len, const char* key, long* out) {
    const char* v = find_value(obj, len, key);
    if (!v) return false;
    const char* end = obj + len;
    bool neg = false;
    if (*v == '-') { neg = true; ++v; }
    if (v >= end || *v < '0' || *v > '9') return false;
    long acc = 0;
    while (v < end && *v >= '0' && *v <= '9') { acc = acc * 10 + (*v - '0'); ++v; }
    if (v < end && *v != ',' && *v != '}' && !ws(*v)) return false;
    *out = neg ? -acc : acc;
    return true;
}
}
