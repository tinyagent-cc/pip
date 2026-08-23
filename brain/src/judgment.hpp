#pragma once
#include <tiny_agent/tiny_agent.hpp>
#include <atomic>
#include <string>
#include <vector>
#include "body.hpp"
#include "log.hpp"
#include "reflex.hpp"
#include "services.hpp"
#include "speaker.hpp"

namespace pip::brain {

struct JudgmentConfig {
    std::string llm_url;          // e.g. http://orin-desktop.local:8081 ; empty disables the layer
    std::string model = "Qwen2.5-3B-Instruct";
    std::string llm2_url;         // optional fallback OpenAI-compatible server
    int timeout_s = 90;
    int max_tokens = 200;
    double temperature = 0.7;
    int max_iterations = 6;
};

// recent_events newest last, "12s ago button.press". transcript is what the
// cortex heard, empty when it heard nothing or was never asked.
struct Context {
    std::vector<std::string> recent_events;
    Senses senses;
    std::string transcript;
    std::string lang;             // language of the transcript ("en", "fr", ...); "" unknown
    // Earlier exchanges, oldest first: {what the person said, what Pip replied}.
    std::vector<std::pair<std::string, std::string>> dialogue;
};

// mind is who answered: 'J' the Jetson (the primary), '5' the Pi 5 (the
// fallback), '-' nobody. It is the HUD's headline during the fallback scene.
struct Verdict {
    std::string reply;
    char mind = '-';
    long ms = 0;
};

// The slow path: a ReAct loop over an OpenAI-compatible chat endpoint, with
// the body, the speaker and the cortex exposed as tools and the reflex
// engine's guardrails wrapped around every tool call the model makes.
// Blocking; call from the trigger thread, not from inside a reflex action.
class Judgment {
public:
    Judgment(JudgmentConfig cfg, IBody& body, Reflex& reflex, EventLog& log,
             Speaker* speaker = nullptr, Cortex* cortex = nullptr);
    bool enabled() const { return !cfg_.llm_url.empty(); }

    Verdict react(const std::string& trigger, const Context& ctx);

    // The fallback scene: answer from the Pi 5 even though the Jetson is up.
    // Ignored when no llm2 is configured, since there would be nothing to
    // fall back to.
    void force_fallback(bool on) { force_fallback_.store(on); }
    bool forced() const { return force_fallback_.load() && !cfg_.llm2_url.empty(); }

    // Fire one request at the Pi 5 so llama-server caches the shared prompt
    // prefix (system prompt + tool schemas) before the fallback scene asks for
    // a real answer: 36.8 s of the 49.8 s cold run was that first prompt eval.
    // Detached thread; a second call while one is in flight is a no-op.
    void warm_fallback_async();

    // Qwen3-style thinking models leak "<think>...</think>" ahead of the
    // answer when the server does not strip it; Pip never speaks its thoughts.
    void ticker(const std::string& s);
    static std::string strip_think(std::string text);

    static std::string system_prompt();
    static std::string user_prompt(const Context& ctx);

private:
    std::vector<tiny_agent::DynamicTool> tools(std::string& said);
    void speak(const std::string& text);

    JudgmentConfig cfg_;
    IBody& body_;
    Reflex& reflex_;
    EventLog& log_;
    Speaker* speaker_;
    Cortex* cortex_;
    std::atomic<bool> force_fallback_{false};
    std::atomic<bool> warm_inflight_{false};
    std::string lang_;            // language of the exchange being reacted to
};

}  // namespace pip::brain
