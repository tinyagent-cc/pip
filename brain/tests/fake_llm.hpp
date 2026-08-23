#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pip::brain {
using json = nlohmann::json;

// A canned OpenAI-compatible /v1/chat/completions endpoint. Tests push
// replies into `replies`; each POST pops the next one off the front. Every
// request body lands in `requests` for the test to inspect afterwards --
// single-threaded once react() has returned, so no lock is needed for reads,
// but pushes/pops during the request are guarded.
inline json tool_call_reply(const std::string& name, const json& args, int ptok = 40, int ctok = 12) {
    return json{{"id","x"},{"object","chat.completion"},{"model","fake"},
        {"choices", json::array({json{{"index",0},{"finish_reason","tool_calls"},{"message", json{{"role","assistant"},{"content",nullptr},
            {"tool_calls", json::array({json{{"id","call_1"},{"type","function"},{"function", json{{"name",name},{"arguments",args.dump()}}}}})}}}}})},
        {"usage", json{{"prompt_tokens",ptok},{"completion_tokens",ctok},{"total_tokens",ptok+ctok}}}};
}
inline json text_reply(const std::string& text, int ptok = 60, int ctok = 8) {
    return json{{"id","x"},{"object","chat.completion"},{"model","fake"},
        {"choices", json::array({json{{"index",0},{"finish_reason","stop"},{"message", json{{"role","assistant"},{"content",text}}}}})},
        {"usage", json{{"prompt_tokens",ptok},{"completion_tokens",ctok},{"total_tokens",ptok+ctok}}}};
}

// Not thread-safe to read `requests`/`replies` while a request is in
// flight -- react() is a blocking call, so by the time it returns control to
// the test both are quiescent. The mutex only protects the server handler's
// own read/pop of `replies` and push to `requests` against each other.
class FakeLlm {
public:
    std::vector<json> replies;
    std::vector<json> requests;

    FakeLlm() {
        svr_.Post("/v1/chat/completions", [this](const httplib::Request& rq, httplib::Response& rs) {
            std::lock_guard<std::mutex> g(m_);
            json req = json::parse(rq.body, nullptr, false);
            requests.push_back(req.is_discarded() ? json::object() : req);
            json reply;
            if (!replies.empty()) { reply = replies.front(); replies.erase(replies.begin()); }
            else reply = text_reply("(no more replies)");
            rs.set_content(reply.dump(), "application/json");
        });
        port_ = svr_.bind_to_any_port("127.0.0.1");
        th_ = std::thread([this] { svr_.listen_after_bind(); });
        svr_.wait_until_ready();
    }
    ~FakeLlm() { svr_.stop(); th_.join(); }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

private:
    std::mutex m_;
    httplib::Server svr_;
    int port_ = 0;
    std::thread th_;
};
}  // namespace pip::brain
