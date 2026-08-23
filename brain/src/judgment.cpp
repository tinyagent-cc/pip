#include "judgment.hpp"
#include <tiny_agent/agents/create.hpp>
#include <tiny_agent/init_chat_model.hpp>
#include <tiny_agent/middleware/model_fallback.hpp>
#include <tiny_agent/providers/openai.hpp>
#include <chrono>
#include <cstdio>

namespace pip::brain {
using namespace tiny_agent;

Judgment::Judgment(JudgmentConfig cfg, IBody& body, Reflex& reflex, EventLog& log)
    : cfg_(std::move(cfg)), body_(body), reflex_(reflex), log_(log) {}

std::string Judgment::system_prompt() {
    return "You are Pip, a small desk companion robot with a face, an RGB LED, a chirp speaker and light and temperature sensors. "
           "Someone is holding your button: they want your attention. React with at most one express call, at most one chirp call and "
           "at most one led call, then answer with one short sentence saying what you did. Emotions: idle, happy, sleepy, thinking, alert, wink. "
           "Chirps: rise, trill, drop, purr. LED channels are 0-255.";
}

std::string Judgment::user_prompt(const Context& ctx) {
    std::string s = "Recent events (oldest first):";
    if (ctx.recent_events.empty()) s += " none";
    for (auto& e : ctx.recent_events) s += "\n- " + e;
    char buf[160];
    std::snprintf(buf, sizeof buf, "\nSenses now: light_lux=%.1f temp_c=%.1f button=%s",
                  ctx.senses.light_lux, ctx.senses.temp_c, ctx.senses.button_down ? "down" : "up");
    return s + buf + "\nReact now.";
}

std::vector<DynamicTool> Judgment::tools() {
    return {
        DynamicTool::create("express", "Show an emotion on Pip's face",
            [this](const json& p) -> json {
                auto e = p.value("emotion", "");
                log_.tool("express", e);
                return json{{"ok", body_.express(e)}};
            },
            json{{"type","object"},
                 {"properties", {{"emotion", {{"type","string"},{"enum", {"idle","happy","sleepy","thinking","alert","wink"}}}}}},
                 {"required", {"emotion"}}}),

        DynamicTool::create("chirp", "Play a short chirp",
            [this](const json& p) -> json {
                auto n = p.value("name", "");
                log_.tool("chirp", n);
                return json{{"ok", body_.chirp(n)}};
            },
            json{{"type","object"},
                 {"properties", {{"name", {{"type","string"},{"enum", {"rise","trill","drop","purr"}}}}}},
                 {"required", {"name"}}}),

        DynamicTool::create("led", "Set the RGB mood LED",
            [this](const json& p) -> json {
                int r = p.value("r", 0), g = p.value("g", 0), b = p.value("b", 0);
                log_.tool("led", std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b));
                return json{{"ok", body_.led(r, g, b)}};
            },
            json{{"type","object"},
                 {"properties", {{"r", {{"type","integer"}}}, {"g", {{"type","integer"}}}, {"b", {{"type","integer"}}}}},
                 {"required", {"r","g","b"}}}),

        DynamicTool::create("senses", "Read light, temperature and button",
            [this](const json&) -> json {
                auto s = body_.senses();
                log_.tool("senses", "-");
                return json{{"light_lux", s.light_lux}, {"temp_c", s.temp_c}, {"button", s.button_down ? "down" : "up"}, {"ok", s.ok}};
            },
            json{{"type","object"},{"properties", json::object()}}),
    };
}

std::string Judgment::react(const std::string& trigger, const Context& ctx) {
    if (!enabled()) return "";

    // Usage + latency logger: one log line per model round trip. Wraps the
    // guardrail middleware, so the latency it records covers guardrail
    // processing too, and the tool-call count it logs is the post-guardrail
    // one the agent loop actually dispatches.
    MiddlewareFn usage = [this, trigger](std::vector<Message>& msgs, Next next) -> LLMResponse {
        auto t0 = std::chrono::steady_clock::now();
        LLMResponse r = next(msgs);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        int64_t pt = r.usage.is_object() ? r.usage.value("prompt_tokens", int64_t(-1)) : -1;
        int64_t ct = r.usage.is_object() ? r.usage.value("completion_tokens", int64_t(-1)) : -1;
        std::string what = r.message.has_tool_calls() ? "tool_calls=" + std::to_string(r.message.tool_calls.size()) : "text";
        log_.llm(trigger, ms, pt, ct, what + " finish=" + r.finish_reason);
        return r;
    };

    AgentConfig cfg;
    cfg.name = "pip-judgment";
    cfg.system_prompt = system_prompt();
    cfg.tools = tools();
    cfg.max_iterations = cfg_.max_iterations;
    cfg.middlewares.push_back(usage);
    cfg.middlewares.push_back(reflex_.guardrail_middleware());
    if (!cfg_.llm2_url.empty()) {
        // AnyChat holds an httplib::Client and is not copyable, so the
        // fallback vector has to be built with push_back/move rather than a
        // brace-init list (which would copy-construct from an
        // std::initializer_list element).
        std::vector<AnyChat> fallbacks;
        fallbacks.push_back(init_chat_model("openai:" + cfg_.model,
            LLMConfig{.base_url = cfg_.llm2_url, .timeout_seconds = cfg_.timeout_s}));
        cfg.middlewares.push_back(middleware::model_fallback(std::move(fallbacks)));
    }

    auto agent = make_agent(
        OpenAIChat{.model = cfg_.model, .api_key = "", .base_url = cfg_.llm_url,
                   .temperature = cfg_.temperature, .max_tokens = cfg_.max_tokens, .timeout_seconds = cfg_.timeout_s},
        cfg);

    try {
        return agent.run(user_prompt(ctx));
    } catch (const std::exception& e) {
        log_.note(std::string("llm failed: ") + e.what());
        return "";
    }
}

}  // namespace pip::brain
