#include "services.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace pip::brain {
namespace {
using json = nlohmann::json;

constexpr int CONNECT_TIMEOUT_S = 2;   // a service that is not there must not cost the demo a scene

// A client per call. These calls are rare (one per hold, one per look) and
// can come from the worker and the director threads at once; a shared client
// would need a lock that serialises a 4 s listen behind a 25 s see.
httplib::Client make_client(const std::string& base_url, int read_timeout_s) {
    httplib::Client cli(base_url);
    cli.set_connection_timeout(CONNECT_TIMEOUT_S, 0);
    cli.set_read_timeout(read_timeout_s, 0);
    cli.set_write_timeout(CONNECT_TIMEOUT_S, 0);
    cli.set_keep_alive(false);
    return cli;
}

bool health(const std::string& base_url) {
    if (base_url.empty()) return false;
    auto cli = make_client(base_url, 1);
    cli.set_connection_timeout(1, 0);
    auto res = cli.Get("/health");
    return res && res->status == 200;
}

std::string http_error(const httplib::Result& res) {
    if (!res) return "unreachable: " + httplib::to_string(res.error());
    return "HTTP " + std::to_string(res->status);
}
}  // namespace

Cortex::Cortex(std::string base_url) : base_url_(std::move(base_url)) {}

void Cortex::set_error(std::string e) { std::lock_guard<std::mutex> g(m_); last_error_ = std::move(e); }
std::string Cortex::last_error() const { std::lock_guard<std::mutex> g(m_); return last_error_; }

bool Cortex::ok() { return health(base_url_); }

std::optional<std::string> Cortex::ask(const char* path, const std::string& body, int read_timeout_s) {
    if (!enabled()) { set_error("cortex not configured"); return std::nullopt; }
    auto cli = make_client(base_url_, read_timeout_s);
    auto res = cli.Post(path, body, "application/json");
    if (!res || res->status != 200) { set_error(std::string(path) + ": " + http_error(res)); return std::nullopt; }
    auto j = json::parse(res->body, nullptr, false);
    if (!j.is_object() || !j.contains("text") || !j["text"].is_string()) {
        set_error(std::string(path) + ": reply had no text");
        return std::nullopt;
    }
    set_error("");
    return j["text"].get<std::string>();
}

// The read timeout has to outlast the recording itself, so it grows with the
// window the caller asked for.
std::optional<std::string> Cortex::listen(int seconds) {
    return ask("/listen", json{{"seconds", seconds}}.dump(), seconds + 10);
}
std::optional<std::string> Cortex::see(const std::string& question) {
    return ask("/see", json{{"question", question}}.dump(), 25);
}

Voice::Voice(std::string base_url) : base_url_(std::move(base_url)) {}

void Voice::set_error(std::string e) { std::lock_guard<std::mutex> g(m_); last_error_ = std::move(e); }
std::string Voice::last_error() const { std::lock_guard<std::mutex> g(m_); return last_error_; }

bool Voice::ok() { return health(base_url_); }

std::optional<std::vector<int16_t>> Voice::tts(const std::string& text) {
    if (!enabled()) { set_error("voice not configured"); return std::nullopt; }
    auto cli = make_client(base_url_, 15);
    auto res = cli.Post("/tts", json{{"text", text}}.dump(), "application/json");
    if (!res || res->status != 200) { set_error("/tts: " + http_error(res)); return std::nullopt; }
    const std::string& b = res->body;
    // A trailing odd byte means a truncated reply; keep the whole samples and
    // let the last half-sample go rather than reading past the buffer.
    std::vector<int16_t> pcm(b.size() / 2);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<int16_t>(static_cast<uint16_t>(static_cast<unsigned char>(b[2 * i])) |
                                      (static_cast<uint16_t>(static_cast<unsigned char>(b[2 * i + 1])) << 8));
    set_error("");
    return pcm;
}

}  // namespace pip::brain
