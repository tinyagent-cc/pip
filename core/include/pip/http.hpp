#pragma once
#include <cstddef>
namespace pip::http {
enum class Parse { Incomplete, Complete, Bad };
struct Request {
    char method[8];
    char path[64];
    const char* body;   // points into the caller's buffer
    size_t body_len;
};
// Parses one HTTP/1.x request. Complete once the header block ended and Content-Length
// bytes of body are present. Bodies over 1024 bytes and header blocks over 2048 are Bad.
Parse parse_request(const char* buf, size_t len, Request& out);
// Writes a complete HTTP/1.0 JSON response; returns bytes written, 0 if it did not fit.
// extra_header, when non-null, is one "Name: value" line without CRLF.
size_t build_response(char* out, size_t cap, int status, const char* json_body, const char* extra_header);
const char* reason(int status);
}
