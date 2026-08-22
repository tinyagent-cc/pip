#include <cstring>
#include "check.h"
#include "pip/http.hpp"
using namespace pip::http;
static void run() {
    Request r{};
    const char* get = "GET /senses HTTP/1.1\r\nHost: pip\r\n\r\n";
    CHECK(parse_request(get, std::strlen(get), r) == Parse::Complete);
    CHECK_STREQ(r.method, "GET"); CHECK_STREQ(r.path, "/senses"); CHECK_EQ(r.body_len, (size_t)0);

    const char* post = "POST /express HTTP/1.0\r\ncontent-length: 19\r\nContent-Type: application/json\r\n\r\n{\"emotion\":\"happy\"}";
    CHECK(parse_request(post, std::strlen(post), r) == Parse::Complete);
    CHECK_STREQ(r.method, "POST"); CHECK_STREQ(r.path, "/express");
    CHECK_EQ(r.body_len, (size_t)19); CHECK(std::memcmp(r.body, "{\"emotion\":\"happy\"}", 19) == 0);

    CHECK(parse_request(post, std::strlen(post) - 5, r) == Parse::Incomplete);   // body short
    CHECK(parse_request(post, 10, r) == Parse::Incomplete);                      // headers unfinished
    const char* bad = "GARBAGE\r\n\r\n";
    CHECK(parse_request(bad, std::strlen(bad), r) == Parse::Bad);
    const char* huge = "POST /x HTTP/1.0\r\nContent-Length: 99999\r\n\r\n";
    CHECK(parse_request(huge, std::strlen(huge), r) == Parse::Bad);

    // Regression: a *complete* header block (terminator present) that is itself
    // over 2048 bytes must still be Bad, not Complete. The header comment says
    // "header blocks over 2048 are Bad" with no carve-out for the terminated case.
    {
        static char big[4096];
        size_t pos = 0;
        const char* head = "GET / HTTP/1.1\r\n";
        std::memcpy(big + pos, head, std::strlen(head)); pos += std::strlen(head);
        const char* pad = "X-Pad: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n";
        size_t pad_len = std::strlen(pad);
        while (pos + pad_len < 2200) { std::memcpy(big + pos, pad, pad_len); pos += pad_len; }
        std::memcpy(big + pos, "\r\n", 2); pos += 2;
        CHECK(pos > 2048);
        CHECK(parse_request(big, pos, r) == Parse::Bad);
    }

    char out[256];
    size_t n = build_response(out, sizeof out, 200, "{\"ok\":true}", nullptr);
    CHECK(n > 0); out[n] = 0;
    CHECK(std::strstr(out, "HTTP/1.0 200 OK\r\n") == out);
    CHECK(std::strstr(out, "Content-Type: application/json\r\n"));
    CHECK(std::strstr(out, "Content-Length: 11\r\n"));
    CHECK(std::strstr(out, "X-Pip-Protocol: 0\r\n"));
    CHECK(std::strstr(out, "Connection: close\r\n\r\n{\"ok\":true}"));
    n = build_response(out, sizeof out, 400, "{\"error\":\"x\"}", "X-Extra: 1");
    out[n] = 0; CHECK(std::strstr(out, "400 Bad Request")); CHECK(std::strstr(out, "X-Extra: 1\r\n"));
    CHECK_EQ(build_response(out, 8, 200, "{}", nullptr), (size_t)0);           // does not fit -> 0
}
TEST_MAIN()
