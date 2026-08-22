#include <cstdio>
#include <cstring>
#include <string>
#include "check.h"
#include "pip/http.hpp"
#include "pip/protocol.hpp"
using namespace pip::http;

// Minimal Body for the feed() tests: records what the router asked for.
struct FeedBody : pip::Body {
    int n_express = 0, n_chirp = 0, n_led = 0;
    void express(pip::Emotion) override { ++n_express; }
    void chirp(pip::Chirp) override { ++n_chirp; }
    void led(uint8_t, uint8_t, uint8_t) override { ++n_led; }
    pip::Senses senses() override { return pip::Senses{12.5f, 21.0f, false}; }
};

static void feed_tests() {
    // One chunk, one complete request, routed and answered.
    {
        FeedBody body; char buf[1536], resp[512]; size_t len = 0, rlen = 0;
        const char* req = "POST /express HTTP/1.0\r\nContent-Length: 19\r\n\r\n{\"emotion\":\"happy\"}";
        CHECK(feed(buf, sizeof buf, len, req, std::strlen(req), body, resp, sizeof resp, rlen) == Feed::Responded);
        CHECK_EQ(body.n_express, 1);
        std::string s(resp, rlen);
        CHECK(s.rfind("HTTP/1.0 200", 0) == 0);
        CHECK(s.find("X-Pip-Protocol: 0\r\n") != std::string::npos);
        CHECK(s.find("Connection: close\r\n\r\n{\"ok\":true}") != std::string::npos);
    }
    // Split on the header/body boundary: the first chunk cannot be answered.
    {
        FeedBody body; char buf[1536], resp[512]; size_t len = 0, rlen = 0;
        const char* head = "POST /express HTTP/1.0\r\nContent-Length: 19\r\n\r\n";
        const char* tail = "{\"emotion\":\"happy\"}";
        CHECK(feed(buf, sizeof buf, len, head, std::strlen(head), body, resp, sizeof resp, rlen) == Feed::NeedMore);
        CHECK_EQ(rlen, (size_t)0); CHECK_EQ(body.n_express, 0);
        CHECK(feed(buf, sizeof buf, len, tail, std::strlen(tail), body, resp, sizeof resp, rlen) == Feed::Responded);
        CHECK_EQ(body.n_express, 1);
        CHECK(std::string(resp, rlen).rfind("HTTP/1.0 200", 0) == 0);
    }
    // Split mid-header, the nastier boundary: the terminator itself straddles the chunks.
    {
        FeedBody body; char buf[1536], resp[512]; size_t len = 0, rlen = 0;
        const char* a = "GET /senses HTTP/1.1\r\nHost: p";
        const char* b = "ip\r\n\r\n";
        CHECK(feed(buf, sizeof buf, len, a, std::strlen(a), body, resp, sizeof resp, rlen) == Feed::NeedMore);
        CHECK(feed(buf, sizeof buf, len, b, std::strlen(b), body, resp, sizeof resp, rlen) == Feed::Responded);
        std::string s(resp, rlen);
        CHECK(s.rfind("HTTP/1.0 200", 0) == 0);
        CHECK(s.find("\"light_lux\"") != std::string::npos);
    }
    // A GET with no body at all is Complete on the first chunk.
    {
        FeedBody body; char buf[1536], resp[512]; size_t len = 0, rlen = 0;
        const char* req = "GET /senses HTTP/1.0\r\n\r\n";
        CHECK(feed(buf, sizeof buf, len, req, std::strlen(req), body, resp, sizeof resp, rlen) == Feed::Responded);
        CHECK(std::string(resp, rlen).find("\"temp_c\"") != std::string::npos);
    }
    // A zero-length chunk changes nothing and asks for more.
    {
        FeedBody body; char buf[1536], resp[512]; size_t len = 0, rlen = 0;
        CHECK(feed(buf, sizeof buf, len, "", 0, body, resp, sizeof resp, rlen) == Feed::NeedMore);
        CHECK_EQ(len, (size_t)0); CHECK_EQ(rlen, (size_t)0);
        const char* a = "GET /senses HTTP/1.1\r\n";
        CHECK(feed(buf, sizeof buf, len, a, std::strlen(a), body, resp, sizeof resp, rlen) == Feed::NeedMore);
        CHECK_EQ(len, std::strlen(a));
        CHECK(feed(buf, sizeof buf, len, "", 0, body, resp, sizeof resp, rlen) == Feed::NeedMore);
        CHECK_EQ(len, std::strlen(a));
    }
    // The buffer fills with no header terminator in sight: bounded 400, not an endless wait.
    {
        FeedBody body; char buf[128], resp[512]; size_t len = 0, rlen = 0;
        static char junk[64]; std::memset(junk, 'A', sizeof junk);
        Feed f = feed(buf, sizeof buf, len, junk, sizeof junk, body, resp, sizeof resp, rlen);
        CHECK(f == Feed::NeedMore);
        f = feed(buf, sizeof buf, len, junk, sizeof junk, body, resp, sizeof resp, rlen);
        CHECK(f == Feed::Bad);
        std::string s(resp, rlen);
        CHECK(s.rfind("HTTP/1.0 400", 0) == 0);
        CHECK(s.find("X-Pip-Protocol: 0\r\n") != std::string::npos);
        CHECK(rlen < sizeof resp);
        // A full buffer stays Bad rather than silently swallowing the overflow bytes.
        CHECK(feed(buf, sizeof buf, len, junk, sizeof junk, body, resp, sizeof resp, rlen) == Feed::Bad);
    }
    // A body bigger than the buffer is rejected, and the endpoint is never called.
    {
        FeedBody body; char buf[128], resp[512]; size_t len = 0, rlen = 0;
        const char* head = "POST /express HTTP/1.0\r\nContent-Length: 300\r\n\r\n";
        Feed f = feed(buf, sizeof buf, len, head, std::strlen(head), body, resp, sizeof resp, rlen);
        CHECK(f == Feed::NeedMore);
        static char blob[300]; std::memset(blob, 'x', sizeof blob);
        f = feed(buf, sizeof buf, len, blob, sizeof blob, body, resp, sizeof resp, rlen);
        CHECK(f == Feed::Bad);
        CHECK_EQ(body.n_express, 0);
        CHECK(std::string(resp, rlen).rfind("HTTP/1.0 400", 0) == 0);
    }
    // Content-Length over the parser's own 1024 ceiling is Bad on sight, whatever the buffer size.
    {
        FeedBody body; char buf[1536], resp[512]; size_t len = 0, rlen = 0;
        const char* req = "POST /express HTTP/1.0\r\nContent-Length: 2000\r\n\r\n";
        CHECK(feed(buf, sizeof buf, len, req, std::strlen(req), body, resp, sizeof resp, rlen) == Feed::Bad);
        CHECK(std::string(resp, rlen).rfind("HTTP/1.0 400", 0) == 0);
    }
}

static void run() {
    feed_tests();
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

    // Regression: the request line must be tokenized within its own CRLF, not all
    // the way to hdr_end. A missing version token (no space at all after the method)
    // must not let a later header's space stand in for it.
    const char* no_version = "GET /senses\r\nHost: pip\r\n\r\n";
    CHECK(parse_request(no_version, std::strlen(no_version), r) == Parse::Bad);
    // A header with spaces in its value must not disturb request-line tokenizing.
    const char* spacey_header = "GET /senses HTTP/1.1\r\nX: a b c\r\n\r\n";
    CHECK(parse_request(spacey_header, std::strlen(spacey_header), r) == Parse::Complete
          && std::strcmp(r.path, "/senses") == 0);
    // Trailing space with nothing after it is a missing version token too.
    const char* no_version2 = "GET /senses \r\n\r\n";
    CHECK(parse_request(no_version2, std::strlen(no_version2), r) == Parse::Bad);

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
