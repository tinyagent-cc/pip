#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

namespace pip::brain {
using json = nlohmann::json;

inline int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
inline int64_t wall_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// One line per thing that happened. Reflexes carry microseconds; LLM calls carry
// milliseconds and tokens. The contrast between the two columns is the demo's headline.
class EventLog {
public:
    enum class Kind { Event, Reflex, Llm, Tool, Note };
    struct Entry {
        int64_t wall_ms = 0;
        Kind kind = Kind::Note;
        std::string name;     // event name, rule name, trigger, tool name
        std::string detail;   // free text
        int64_t micros = -1;  // reflex duration, or llm duration * 1000
        int64_t prompt_tokens = -1, completion_tokens = -1;
    };
    explicit EventLog(size_t cap = 200, FILE* out = stderr) : cap_(cap), out_(out) {}

    void event(const std::string& name, const std::string& detail = {}) { push({wall_ms(), Kind::Event, name, detail}); }
    void reflex(const std::string& rule, int64_t micros, const std::string& detail) { push({wall_ms(), Kind::Reflex, rule, detail, micros}); }
    void llm(const std::string& trigger, int64_t millis, int64_t ptok, int64_t ctok, const std::string& detail) {
        push({wall_ms(), Kind::Llm, trigger, detail, millis * 1000, ptok, ctok});
    }
    void tool(const std::string& name, const std::string& detail) { push({wall_ms(), Kind::Tool, name, detail}); }
    void note(const std::string& detail) { push({wall_ms(), Kind::Note, "", detail}); }

    size_t count(Kind k) const { std::lock_guard<std::mutex> g(m_); size_t n = 0; for (auto& e : ring_) if (e.kind == k) ++n; return n + dropped_.at(static_cast<int>(k)); }
    json tail(size_t n) const {
        std::lock_guard<std::mutex> g(m_);
        json out = json::array();
        size_t start = ring_.size() > n ? ring_.size() - n : 0;
        for (size_t i = start; i < ring_.size(); ++i) out.push_back(to_json(ring_[i]));
        return out;
    }
    static const char* kind_name(Kind k) {
        switch (k) { case Kind::Event: return "event"; case Kind::Reflex: return "reflex"; case Kind::Llm: return "llm"; case Kind::Tool: return "tool"; default: return "note"; }
    }
    static json to_json(const Entry& e) {
        json j{{"t", e.wall_ms}, {"kind", kind_name(e.kind)}, {"name", e.name}, {"detail", e.detail}};
        if (e.micros >= 0) j["micros"] = e.micros;
        if (e.prompt_tokens >= 0) { j["prompt_tokens"] = e.prompt_tokens; j["completion_tokens"] = e.completion_tokens; }
        return j;
    }
    static std::string line(const Entry& e) {
        char buf[256];
        if (e.kind == Kind::Reflex) std::snprintf(buf, sizeof buf, "reflex %-14s %8lld us  %s", e.name.c_str(), (long long)e.micros, e.detail.c_str());
        else if (e.kind == Kind::Llm) std::snprintf(buf, sizeof buf, "llm    %-14s %8lld ms  tokens=%lld/%lld %s", e.name.c_str(), (long long)(e.micros / 1000), (long long)e.prompt_tokens, (long long)e.completion_tokens, e.detail.c_str());
        else std::snprintf(buf, sizeof buf, "%-6s %-14s %s", kind_name(e.kind), e.name.c_str(), e.detail.c_str());
        return buf;
    }
private:
    void push(Entry e) {
        std::lock_guard<std::mutex> g(m_);
        if (out_) std::fprintf(out_, "[%lld] %s\n", (long long)e.wall_ms, line(e).c_str());
        ring_.push_back(std::move(e));
        if (ring_.size() > cap_) { ++dropped_[static_cast<int>(ring_.front().kind)]; ring_.pop_front(); }
    }
    size_t cap_; FILE* out_;
    mutable std::mutex m_;
    std::deque<Entry> ring_;
    std::array<size_t, 5> dropped_{};
};
}  // namespace pip::brain
