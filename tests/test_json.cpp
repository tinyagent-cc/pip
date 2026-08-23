#include <cstring>
#include "check.h"
#include "pip/json_mini.hpp"
using namespace pip::json;
static void run() {
    const char* o = R"({"emotion": "happy", "r":12,"g": 0 , "b":-3, "name":"x\"y"})";
    size_t n = std::strlen(o);
    char s[16]; long v;
    CHECK(get_string(o, n, "emotion", s, sizeof s)); CHECK_STREQ(s, "happy");
    CHECK(get_int(o, n, "r", &v)); CHECK_EQ(v, 12L);
    CHECK(get_int(o, n, "g", &v)); CHECK_EQ(v, 0L);
    CHECK(get_int(o, n, "b", &v)); CHECK_EQ(v, -3L);
    CHECK(!get_int(o, n, "emotion", &v));          // string where int expected
    CHECK(!get_string(o, n, "r", s, sizeof s));      // int where string expected
    CHECK(!get_string(o, n, "missing", s, sizeof s));
    CHECK(!get_string(o, n, "name", s, sizeof s));   // escapes unsupported -> false, never garbage
    CHECK(!get_string(o, n, "emotion", s, 4));       // does not fit -> false
    const char* trick = R"({"a":"emotion","emotion":"wink"})";
    CHECK(get_string(trick, std::strlen(trick), "emotion", s, sizeof s)); CHECK_STREQ(s, "wink");
    const char* trunc = R"({"emotion":"hap)";
    CHECK(!get_string(trunc, std::strlen(trunc), "emotion", s, sizeof s));
    const char* f = R"({"temp":12.5,"r":12abc,"g":7 ,"b":8})";
    CHECK(!get_int(f, std::strlen(f), "temp", &v));
    CHECK(!get_int(f, std::strlen(f), "r", &v));
    CHECK(get_int(f, std::strlen(f), "g", &v) && v == 7);
    CHECK(get_int(f, std::strlen(f), "b", &v) && v == 8);

    const char* bo = R"({"brain":true,"cortex": false ,"n":1,"s":"true","t":truthy})";
    size_t bn = std::strlen(bo);
    bool bv = false;
    CHECK(get_bool(bo, bn, "brain", &bv) && bv);
    CHECK(get_bool(bo, bn, "cortex", &bv) && !bv);
    CHECK(!get_bool(bo, bn, "missing", &bv));
    CHECK(!get_bool(bo, bn, "n", &bv));       // number is not a bool
    CHECK(!get_bool(bo, bn, "s", &bv));       // quoted "true" is not a bool
    CHECK(!get_bool(bo, bn, "t", &bv));       // a prefix of "true" that keeps going is not a bool

    // Truncating reader: /say has to shorten a long line, not reject it.
    const char* ts = R"({"text":"hello from the wire"})";
    char small[8];
    CHECK(get_string_trunc(ts, std::strlen(ts), "text", small, sizeof small)); CHECK_STREQ(small, "hello f");
    CHECK(!get_string_trunc(ts, std::strlen(ts), "nope", small, sizeof small));
}
TEST_MAIN()
