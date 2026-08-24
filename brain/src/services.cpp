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

std::optional<json> Cortex::ask(const char* path, const std::string& body, int read_timeout_s) {
    if (!enabled()) { set_error("cortex not configured"); return std::nullopt; }
    auto cli = make_client(base_url_, read_timeout_s);
    auto res = cli.Post(path, body, "application/json");
    if (!res || res->status != 200) { set_error(std::string(path) + ": " + http_error(res)); return std::nullopt; }
    auto j = json::parse(res->body, nullptr, false);
    if (!j.is_object()) { set_error(std::string(path) + ": unreadable reply"); return std::nullopt; }
    set_error("");
    return j;
}

// The read timeout has to outlast the recording plus the transcription. The
// ears live on the Pi 5 now (~6 s warm, ~10 s on a cold model), so the margin
// is 20 s, not 10.
std::optional<Heard> Cortex::listen(int seconds) {
    auto j = ask("/listen", json{{"seconds", seconds}}.dump(), seconds + 20);
    if (!j || !j->contains("text") || !(*j)["text"].is_string()) {
        if (j) set_error("/listen: reply had no text");
        return std::nullopt;
    }
    return Heard{(*j)["text"].get<std::string>(), j->value("lang", std::string())};
}
std::optional<std::string> Cortex::see(const std::string& question) {
    auto j = ask("/see", json{{"question", question}}.dump(), 25);
    if (!j || !j->contains("text") || !(*j)["text"].is_string()) {
        if (j) set_error("/see: reply had no text");
        return std::nullopt;
    }
    return (*j)["text"].get<std::string>();
}
std::optional<std::string> Cortex::search(const std::string& query) {
    auto j = ask("/search", json{{"query", query}, {"max_results", 4}}.dump(), 20);
    if (!j || !j->contains("results") || !(*j)["results"].is_array()) {
        if (j) set_error("/search: reply had no results");
        return std::nullopt;
    }
    std::string out;
    for (const auto& r : (*j)["results"]) {
        if (!r.is_object()) continue;
        if (!out.empty()) out += "\n";
        out += r.value("title", std::string()) + ": " + r.value("snippet", std::string());
    }
    if (out.empty()) { set_error("/search: no hits"); return std::nullopt; }
    return out;
}

Voice::Voice(std::string base_url) : base_url_(std::move(base_url)) {}

void Voice::set_error(std::string e) { std::lock_guard<std::mutex> g(m_); last_error_ = std::move(e); }
std::string Voice::last_error() const { std::lock_guard<std::mutex> g(m_); return last_error_; }

bool Voice::ok() { return health(base_url_); }

std::optional<std::vector<int16_t>> Voice::tts(const std::string& text, const std::string& lang) {
    if (!enabled()) { set_error("voice not configured"); return std::nullopt; }
    auto cli = make_client(base_url_, 15);
    json req{{"text", text}};
    if (!lang.empty()) req["lang"] = lang;
    auto res = cli.Post("/tts", req.dump(), "application/json");
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
