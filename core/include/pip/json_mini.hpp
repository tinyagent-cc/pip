#pragma once
#include <cstddef>
namespace pip::json {
// Flat-object getters for Pip's tiny bodies. Keys are top-level; values are a string
// without escapes, a decimal integer, or a true/false literal. Return false when absent
// or the wrong shape.
bool get_string(const char* obj, size_t len, const char* key, char* out, size_t out_cap);
// Same, except a value that does not fit is cut to out_cap - 1 characters instead of
// refused. For fields where a long line is still usable (/say), not for identifiers.
bool get_string_trunc(const char* obj, size_t len, const char* key, char* out, size_t out_cap);
bool get_int(const char* obj, size_t len, const char* key, long* out);
bool get_bool(const char* obj, size_t len, const char* key, bool* out);
}
