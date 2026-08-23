#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace pip::brain {
using json = nlohmann::json;

// A canned OpenAI-compatible /v1/chat/completions endpoint. Tests push
// replies into `replies`; each POST pops the next one off the front. Every
// request body is recorded for the test to inspect afterwards via
// `requests_snapshot()`.
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

// `replies` is written by the test before react() ever connects (there is no
// request in flight yet, so no lock needed there) and popped by the server
// handler under `m_`. `requests` is written by the handler on its own thread
// and read by the test after react() returns; it is private and only
// reachable through the locked `requests_snapshot()` so a test can never read
// it unsynchronized with the handler thread.
class FakeLlm {
public:
    std::vector<json> replies;

    FakeLlm() {
        svr_.Post("/v1/chat/completions", [this](const httplib::Request& rq, httplib::Response& rs) {
            json req = json::parse(rq.body, nullptr, false);
            json reply;
            {
                std::lock_guard<std::mutex> g(m_);
                requests_.push_back(req.is_discarded() ? json::object() : req);
                if (!replies.empty()) { reply = replies.front(); replies.erase(replies.begin()); }
                else reply = text_reply("(no more replies)");
            }
            rs.set_content(reply.dump(), "application/json");
        });
        port_ = svr_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0)
            throw std::runtime_error("FakeLlm: bind_to_any_port failed on 127.0.0.1");
        th_ = std::thread([this] { svr_.listen_after_bind(); });
        svr_.wait_until_ready();
    }
    ~FakeLlm() { svr_.stop(); th_.join(); }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

    std::vector<json> requests_snapshot() const { std::lock_guard<std::mutex> g(m_); return requests_; }

private:
    mutable std::mutex m_;
    std::vector<json> requests_;
    httplib::Server svr_;
    int port_ = 0;
    std::thread th_;
};
}  // namespace pip::brain
