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
namespace {
bool read_string(const char* obj, size_t len, const char* key, char* out, size_t cap, bool truncate) {
    const char* v = find_value(obj, len, key);
    if (!v || *v != '"' || cap == 0) return false;
    const char* end = obj + len;
    ++v;
    size_t n = 0;
    while (v < end && *v != '"') {
        if (*v == '\\') return false;               // escapes unsupported: false, never garbage
        if (n + 1 >= cap) { if (!truncate) return false; ++v; continue; }
        out[n++] = *v++;
    }
    if (v >= end) return false;                    // unterminated, even when truncating
    out[n] = '\0';
    return true;
}
}
bool get_string(const char* obj, size_t len, const char* key, char* out, size_t cap) {
    return read_string(obj, len, key, out, cap, false);
}
bool get_string_trunc(const char* obj, size_t len, const char* key, char* out, size_t cap) {
    return read_string(obj, len, key, out, cap, true);
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
bool get_bool(const char* obj, size_t len, const char* key, bool* out) {
    const char* v = find_value(obj, len, key);
    if (!v) return false;
    const char* end = obj + len;
    size_t n = (size_t)(end - v);
    bool val;
    size_t used;
    if (n >= 4 && std::memcmp(v, "true", 4) == 0) { val = true; used = 4; }
    else if (n >= 5 && std::memcmp(v, "false", 5) == 0) { val = false; used = 5; }
    else return false;
    // "truthy" starts with "true" but is not a boolean; the literal has to end here.
    const char* after = v + used;
    if (after < end && *after != ',' && *after != '}' && !ws(*after)) return false;
    *out = val;
    return true;
}
}
