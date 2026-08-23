#include "judgment.hpp"
#include <tiny_agent/agents/create.hpp>
#include <tiny_agent/init_chat_model.hpp>
#include <tiny_agent/middleware/model_fallback.hpp>
#include <tiny_agent/providers/openai.hpp>
#include <chrono>
#include <cstdio>

namespace pip::brain {
using namespace tiny_agent;

Judgment::Judgment(JudgmentConfig cfg, IBody& body, Reflex& reflex, EventLog& log, Speaker* speaker, Cortex* cortex)
    : cfg_(std::move(cfg)), body_(body), reflex_(reflex), log_(log), speaker_(speaker), cortex_(cortex) {}

std::string Judgment::system_prompt() {
    return "You are Pip, a small desk companion robot with a face, an RGB LED, a chirp speaker, a camera and light and "
           "temperature sensors. Someone is holding your button: they want your attention. "
           "You can express an emotion (idle, happy, sleepy, thinking, alert, wink, surprised, sad, listening, talking), "
           "chirp (rise, trill, drop, purr, boot, sad), set the led (channels 0-255), read your senses, "
           "say one short sentence out loud, and look through your camera. "
           "If the person asked a question, answer it in one or two short sentences with say; otherwise react to the event. "
           "Work in at most two turns: first call all the tools you want at once (express, chirp, led, look), "
           "then call say with your answer and stop. Use at most one express, one chirp and one led. "
           "Keep every sentence under 90 characters.";
}

std::string Judgment::user_prompt(const Context& ctx) {
    std::string s = "Recent events (oldest first):";
    if (ctx.recent_events.empty()) s += " none";
    for (auto& e : ctx.recent_events) s += "\n- " + e;
    char buf[160];
    std::snprintf(buf, sizeof buf, "\nSenses now: light_lux=%.1f temp_c=%.1f button=%s",
                  ctx.senses.light_lux, ctx.senses.temp_c, ctx.senses.button_down ? "down" : "up");
    s += buf;
    if (!ctx.transcript.empty()) s += "\nTranscript: \"" + ctx.transcript + "\"";
    else s += "\nSomeone held your button and said nothing.";
    return s + "\nReact now.";
}

// Speech goes through the Speaker when there is one, so the worker thread is
// never blocked on TTS; without one the bubble is all Pip can manage.
void Judgment::speak(const std::string& text) {
    if (speaker_) speaker_->say(text);
    else body_.say(text);
}

std::vector<DynamicTool> Judgment::tools(std::string& said) {
    return {
        DynamicTool::create("express", "Show an emotion on Pip's face",
            [this](const json& p) -> json {
                auto e = p.value("emotion", "");
                log_.tool("express", e);
                return json{{"ok", body_.express(e)}};
            },
            json{{"type","object"},
                 {"properties", {{"emotion", {{"type","string"},{"enum", {"idle","happy","sleepy","thinking","alert","wink","surprised","sad","listening","talking"}}}}}},
                 {"required", {"emotion"}}}),

        DynamicTool::create("chirp", "Play a short chirp",
            [this](const json& p) -> json {
                auto n = p.value("name", "");
                log_.tool("chirp", n);
                return json{{"ok", body_.chirp(n)}};
            },
            json{{"type","object"},
                 {"properties", {{"name", {{"type","string"},{"enum", {"rise","trill","drop","purr","boot","sad"}}}}}},
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

        DynamicTool::create("say", "Say one short sentence out loud",
            [this, &said](const json& p) -> json {
                auto text = p.value("text", "");
                log_.tool("say", text);
                if (text.empty()) return json{{"ok", false}};
                said = text;                 // so the closing sentence is not repeated
                speak(text);
                return json{{"ok", true}};
            },
            json{{"type","object"},
                 {"properties", {{"text", {{"type","string"}}}}},
                 {"required", {"text"}}}),

        DynamicTool::create("look", "Look through Pip's camera and describe what is there",
            [this](const json& p) -> json {
                auto q = p.value("question", "What do you see?");
                log_.tool("look", q);
                if (cortex_) {
                    if (auto seen = cortex_->see(q)) return json{{"seen", *seen}};
                }
                // A blind Pip has to say so in words the model can pass on,
                // not fail the tool call and derail the whole answer.
                return json{{"seen", "I can't see right now"}};
            },
            json{{"type","object"},
                 {"properties", {{"question", {{"type","string"}}}}},
                 {"required", {"question"}}}),
    };
}

Verdict Judgment::react(const std::string& trigger, const Context& ctx) {
    Verdict v;
    if (!enabled()) return v;
    auto t_start = std::chrono::steady_clock::now();
    std::string said;                 // set by the say tool
    bool primary_answered = false;
    const bool forced = this->forced();

    // Everything from here down -- config assembly, agent construction, and
    // the run itself -- funnels into the catch below. DeepAgent's
    // constructor can throw (e.g. max_iterations <= 0), and construction is
    // driven by cfg_ values that are not validated on the way in, so react()
    // must not let that escape as an unhandled exception.
    try {
        // Usage + latency logger: one log line per model round trip. Wraps
        // the guardrail middleware, so the latency it records covers
        // guardrail processing too, and the tool-call count it logs is the
        // post-guardrail one the agent loop actually dispatches.
        MiddlewareFn usage = [this, trigger, &primary_answered](std::vector<Message>& msgs, Next next) -> LLMResponse {
            primary_answered = false;   // the marker below sets it again if the primary replies
            auto t0 = std::chrono::steady_clock::now();
            LLMResponse r = next(msgs);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            int64_t pt = r.usage.is_object() ? r.usage.value("prompt_tokens", int64_t(-1)) : -1;
            int64_t ct = r.usage.is_object() ? r.usage.value("completion_tokens", int64_t(-1)) : -1;
            std::string what = r.message.has_tool_calls() ? "tool_calls=" + std::to_string(r.message.tool_calls.size()) : "text";
            log_.llm(trigger, ms, pt, ct, what + " finish=" + r.finish_reason);
            return r;
        };

        // The innermost middleware, so its next() is the primary model
        // itself. model_fallback sits outside it and calls its fallbacks
        // directly, bypassing the rest of the chain -- which is what makes
        // this marker mean "the Jetson answered", not "somebody answered".
        MiddlewareFn marker = [&primary_answered](std::vector<Message>& msgs, Next next) -> LLMResponse {
            LLMResponse r = next(msgs);
            primary_answered = true;
            return r;
        };

        AgentConfig cfg;
        cfg.name = "pip-judgment";
        cfg.system_prompt = system_prompt();
        cfg.tools = tools(said);
        cfg.max_iterations = cfg_.max_iterations;
        cfg.middlewares.push_back(usage);
        cfg.middlewares.push_back(reflex_.guardrail_middleware());
        if (!cfg_.llm2_url.empty() && !forced) {
            // AnyChat holds an httplib::Client and is not copyable, so the
            // fallback vector has to be built with push_back/move rather than
            // a brace-init list (which would copy-construct from an
            // std::initializer_list element).
            std::vector<AnyChat> fallbacks;
            LLMConfig fb;  // explicit members: gcc 14 -Wextra flags partial designated init
            fb.base_url = cfg_.llm2_url;
            fb.timeout_seconds = cfg_.timeout_s;
            fallbacks.push_back(init_chat_model("openai:" + cfg_.model, fb));
            cfg.middlewares.push_back(middleware::model_fallback(std::move(fallbacks)));
        }
        cfg.middlewares.push_back(marker);

        OpenAIChat llm;
        llm.model = cfg_.model;
        llm.api_key = "";
        // The fallback scene points the primary at the Pi 5 outright: there
        // is no Jetson outage to wait for, the demo just wants the slow mind.
        llm.base_url = forced ? cfg_.llm2_url : cfg_.llm_url;
        llm.temperature = cfg_.temperature;
        llm.max_tokens = cfg_.max_tokens;
        llm.timeout_seconds = cfg_.timeout_s;
        auto agent = make_agent(std::move(llm), cfg);

        v.reply = agent.run(user_prompt(ctx));
        if (forced) v.mind = '5';
        else v.mind = primary_answered ? 'J' : (cfg_.llm2_url.empty() ? 'J' : '5');
        // The closing sentence is spoken too, unless the model already said
        // it through the say tool.
        // The agent loop reports its own failures as text ("Error: agent reached
        // maximum iterations"); that is a log line, never something Pip says.
        if (v.reply.rfind("Error:", 0) == 0) {
            log_.note("judgment: " + v.reply);
            v.reply.clear();
            if (said.empty()) speak("I got lost in thought. Ask me again?");
        } else if (!v.reply.empty() && v.reply != said) {
            speak(v.reply);
        }
    } catch (const std::exception& e) {
        log_.note(std::string("llm failed: ") + e.what());
        v.reply.clear();
        v.mind = '-';
    }
    v.ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count());
    return v;
}

}  // namespace pip::brain
