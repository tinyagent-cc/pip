#include "judgment.hpp"
#include <algorithm>
#include <tiny_agent/agents/create.hpp>
#include <tiny_agent/init_chat_model.hpp>
#include <tiny_agent/middleware/model_fallback.hpp>
#include <tiny_agent/providers/openai.hpp>
#include <chrono>
#include <cstdio>
#include <thread>

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
           "Answer in the language the person spoke: English, French or Arabic. "
           "If the question needs fresh facts (news, weather, prices, sport, anything after your training), "
           "call search first and answer from the results. "
           "Work in at most two turns: first call all the tools you want at once (express, chirp, led, look, search), "
           "then call say with your answer and stop. Use at most one express, one chirp and one led. "
           "Only call look when they ask about what you can see, and search only for fresh facts. "
           "Never describe what your tools did; just answer the question. "
           "Plain text only, no emojis. Keep every sentence under 90 characters.";
}

std::string Judgment::user_prompt(const Context& ctx) {
    std::string s = "Recent events (oldest first):";
    if (ctx.recent_events.empty()) s += " none";
    for (auto& e : ctx.recent_events) s += "\n- " + e;
    char buf[160];
    std::snprintf(buf, sizeof buf, "\nSenses now: light_lux=%.1f temp_c=%.1f button=%s",
                  ctx.senses.light_lux, ctx.senses.temp_c, ctx.senses.button_down ? "down" : "up");
    s += buf;
    if (!ctx.dialogue.empty()) {
        s += "\nThe conversation so far (oldest first):";
        for (auto& [theirs, mine] : ctx.dialogue) {
            if (!theirs.empty()) s += "\n- They said: \"" + theirs + "\"";
            if (!mine.empty()) s += "\n- You replied: \"" + mine + "\"";
        }
    }
    if (!ctx.transcript.empty()) {
        s += "\nThey just said: \"" + ctx.transcript + "\"";
        if (!ctx.lang.empty() && ctx.lang != "en") s += " (language: " + ctx.lang + ")";
    } else {
        s += "\nSomeone held your button and said nothing.";
    }
    return s + "\nReact now.";
}

// Speech goes through the Speaker when there is one, so the worker thread is
// never blocked on TTS; without one the bubble is all Pip can manage.
// Machinery ticker: the HUD caption names what the deep agent is doing
// right now (which tool, which phase). 15 chars is the body's caption cap.
void Judgment::ticker(const std::string& s) {
    HudFields f;
    f.scene = s.size() > 15 ? s.substr(0, 15) : s;
    body_.hud(f);
}

void Judgment::speak(const std::string& text) {
    std::string plain = strip_markdown(text);   // the say tool is a leak path too
    if (speaker_) speaker_->say(plain, true, lang_);
    else body_.say(plain);
}

std::vector<DynamicTool> Judgment::tools(std::string& said) {
    return {
        DynamicTool::create("express", "Show an emotion on Pip's face",
            [this](const json& p) -> json {
                auto e = p.value("emotion", "");
                log_.tool("express", e);
                ticker("tool express");
                return json{{"ok", body_.express(e)}};
            },
            json{{"type","object"},
                 {"properties", {{"emotion", {{"type","string"},{"enum", {"idle","happy","sleepy","thinking","alert","wink","surprised","sad","listening","talking"}}}}}},
                 {"required", {"emotion"}}}),

        DynamicTool::create("chirp", "Play a short chirp",
            [this](const json& p) -> json {
                auto n = p.value("name", "");
                log_.tool("chirp", n);
                ticker("tool chirp");
                return json{{"ok", body_.chirp(n)}};
            },
            json{{"type","object"},
                 {"properties", {{"name", {{"type","string"},{"enum", {"rise","trill","drop","purr","boot","sad"}}}}}},
                 {"required", {"name"}}}),

        DynamicTool::create("led", "Set the RGB mood LED",
            [this](const json& p) -> json {
                int r = p.value("r", 0), g = p.value("g", 0), b = p.value("b", 0);
                log_.tool("led", std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b));
                ticker("tool led");
                return json{{"ok", body_.led(r, g, b)}};
            },
            json{{"type","object"},
                 {"properties", {{"r", {{"type","integer"}}}, {"g", {{"type","integer"}}}, {"b", {{"type","integer"}}}}},
                 {"required", {"r","g","b"}}}),

        DynamicTool::create("senses", "Read light, temperature and button",
            [this](const json&) -> json {
                auto s = body_.senses();
                log_.tool("senses", "-");
                ticker("tool senses");
                return json{{"light_lux", s.light_lux}, {"temp_c", s.temp_c}, {"button", s.button_down ? "down" : "up"}, {"ok", s.ok}};
            },
            json{{"type","object"},{"properties", json::object()}}),

        DynamicTool::create("say", "Say one short sentence out loud",
            [this, &said](const json& p) -> json {
                auto text = p.value("text", "");
                log_.tool("say", text);
                ticker("tool say");
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
                ticker("tool look");
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

        DynamicTool::create("search", "Search the web (DuckDuckGo) for fresh facts",
            [this](const json& p) -> json {
                auto q = p.value("query", "");
                log_.tool("search", q);
                ticker("tool search");
                if (q.empty()) return json{{"results", "nothing to search for"}};
                if (cortex_) {
                    if (auto hits = cortex_->search(q)) return json{{"results", *hits}};
                }
                // Same shape as a blind look: words the model can pass on.
                return json{{"results", "the web is out of reach right now"}};
            },
            json{{"type","object"},
                 {"properties", {{"query", {{"type","string"}}}}},
                 {"required", {"query"}}}),
    };
}

// Models decorate despite "plain text only" (Granite bolds, others backtick).
// The bubble font and the TTS can do nothing with markdown, so it goes here,
// whatever mind is on the dial.
std::string Judgment::strip_markdown(std::string text) {
    std::string out; out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '*' || c == '`') continue;
        if (c == '#' && (i == 0 || text[i-1] == '\n')) { while (i < text.size() && text[i] == '#') ++i; if (i < text.size() && text[i] == ' ') ++i; --i; continue; }
        out += c;
    }
    return out;
}

std::string Judgment::strip_think(std::string text) {
    // Remove every <think>...</think> block, plus an unclosed trailing one
    // (the model ran out of tokens mid-thought). Case-sensitive: that is how
    // the models emit it.
    for (;;) {
        auto a = text.find("<think>");
        if (a == std::string::npos) break;
        auto b = text.find("</think>", a);
        if (b == std::string::npos) { text.erase(a); break; }
        text.erase(a, b + 8 - a);
    }
    // Trim what the removal left behind.
    auto first = text.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    auto last = text.find_last_not_of(" \t\n\r");
    return text.substr(first, last - first + 1);
}

// One cheap round trip to the fallback server with the exact system prompt
// and tool schemas react() sends, so the shared prefix is in llama-server's
// cache before the scene asks its real question. The tools are never run:
// this calls the model directly, not the agent loop.
void Judgment::warm_fallback_async() {
    if (cfg_.llm2_url.empty()) return;
    if (warm_inflight_.exchange(true)) return;
    std::thread([this] {
        try {
            auto t0 = std::chrono::steady_clock::now();
            std::string unused;
            auto dts = tools(unused);
            std::vector<ToolSchema> schemas;
            schemas.reserve(dts.size());
            for (auto& t : dts) schemas.push_back(t.schema);
            OpenAIChat llm;
            llm.model = cfg_.model;
            llm.api_key = cfg_.api_key;
            llm.base_url = cfg_.llm2_url;
            llm.temperature = cfg_.temperature;
            llm.max_tokens = 1;
            llm.timeout_seconds = cfg_.timeout_s;
            std::vector<Message> msgs = {Message::system(system_prompt()), Message::user("warm-up")};
            llm.chat(msgs, schemas);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            log_.note("fallback warmed in " + std::to_string(ms) + " ms");
        } catch (const std::exception& e) {
            log_.note(std::string("fallback warm-up failed: ") + e.what());
        }
        warm_inflight_.store(false);
    }).detach();
}

Verdict Judgment::react(const std::string& trigger, const Context& ctx) {
    Verdict v;
    if (!enabled()) return v;
    lang_ = ctx.lang;
    auto t_start = std::chrono::steady_clock::now();
    ticker("deep-agent");
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
        MiddlewareFn usage = [this, trigger, &primary_answered, &said](std::vector<Message>& msgs, Next next) -> LLMResponse {
            // Once the model has said something, the answer is out: end the
            // loop here instead of letting a one-tool-per-turn model keep
            // calling express/chirp/say until the iteration cap.
            if (!said.empty()) {
                log_.llm(trigger, 0, 0, 0, "short-circuit after say");
                return LLMResponse{Message::assistant(said),
                                   json{{"prompt_tokens", 0}, {"completion_tokens", 0}, {"total_tokens", 0}},
                                   "stop", json{{"pip_short_circuit", true}}};
            }
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
        // A model that calls say in the same turn as search or look is
        // answering before the facts arrive (Granite does, on the bench of
        // 2026-08-24: "Ubuntu 22.04" said in parallel with the search that
        // would have corrected it). The guardrail drops that say; the loop
        // continues, and the model says its answer next turn, informed.
        MiddlewareFn facts_guard = [this](std::vector<Message>& msgs, Next next) -> LLMResponse {
            LLMResponse r = next(msgs);
            if (r.message.has_tool_calls()) {
                bool fresh = false, has_say = false;
                for (auto& c : r.message.tool_calls) {
                    if (c.name == "search" || c.name == "look") fresh = true;
                    if (c.name == "say") has_say = true;
                }
                if (fresh && has_say) {
                    auto& tc = r.message.tool_calls;
                    tc.erase(std::remove_if(tc.begin(), tc.end(),
                                            [](const ToolCall& c) { return c.name == "say"; }),
                             tc.end());
                    log_.reflex("say-needs-facts", 0, "vetoed say before search/look results");
                    ticker("guard say-veto");
                }
            }
            return r;
        };
        cfg.middlewares.push_back(usage);
        cfg.middlewares.push_back(facts_guard);
        cfg.middlewares.push_back(reflex_.guardrail_middleware());
        if (!cfg_.llm2_url.empty() && !forced) {
            // AnyChat holds an httplib::Client and is not copyable, so the
            // fallback vector has to be built with push_back/move rather than
            // a brace-init list (which would copy-construct from an
            // std::initializer_list element).
            std::vector<AnyChat> fallbacks;
            LLMConfig fb;  // explicit members: gcc 14 -Wextra flags partial designated init
            fb.base_url = cfg_.llm2_url;
            fb.api_key = cfg_.api_key;
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

        v.reply = strip_markdown(strip_think(agent.run(user_prompt(ctx))));
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
