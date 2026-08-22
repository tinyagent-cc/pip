#pragma once
#include <cstddef>
namespace pip::json {
// Flat-object getters for Pip's tiny bodies. Keys are top-level; values are a string
// without escapes or a decimal integer. Return false when absent or the wrong shape.
bool get_string(const char* obj, size_t len, const char* key, char* out, size_t out_cap);
bool get_int(const char* obj, size_t len, const char* key, long* out);
}
