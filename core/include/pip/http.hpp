#pragma once
#include <cstddef>
#include <cstdint>
namespace pip { struct Body; }   // pip/protocol.hpp; declared here to keep the headers acyclic
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

enum class Feed : uint8_t { NeedMore, Responded, Bad };
// Drives one connection's request buffer from arriving bytes to a finished response, with
// no transport in sight, so the states a socket only reaches under load are host-testable.
//
// Appends up to (cap - 1 - len) bytes of chunk to buf, advances len, and once the request
// is complete or hopeless writes the whole response into resp and sets rlen.
//   NeedMore   nothing written, rlen is 0, keep the connection open
//   Responded  resp holds a routed response, write rlen bytes and close
//   Bad        resp holds a 400, same handling as Responded, and body was never called
// rlen comes back 0 on Responded or Bad only when even the error response did not fit
// rcap; close without writing. buf may hold junk from the caller, only [0, len) is read.
// A chunk that does not fit is not held back for later: the buffer filling with no
// complete request in it is exactly the case that becomes 400, so nothing is smuggled.
Feed feed(char* buf, size_t cap, size_t& len, const char* chunk, size_t n,
          Body& body, char* resp, size_t rcap, size_t& rlen);
}
