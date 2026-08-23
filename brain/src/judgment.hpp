#pragma once
#include <tiny_agent/tiny_agent.hpp>
#include <string>
#include <vector>
#include "body.hpp"
#include "log.hpp"
#include "reflex.hpp"

namespace pip::brain {

struct JudgmentConfig {
    std::string llm_url;          // e.g. http://pi5.local:8081 ; empty disables the layer
    std::string model = "Qwen2.5-3B-Instruct";
    std::string llm2_url;         // optional fallback OpenAI-compatible server
    int timeout_s = 90;
    int max_tokens = 200;
    double temperature = 0.7;
    int max_iterations = 4;
};

struct Context { std::vector<std::string> recent_events; Senses senses; };  // recent_events newest last, "12s ago button.press"

// The slow path: a ReAct loop over an OpenAI-compatible chat endpoint, with
// the body exposed as tools and the reflex engine's guardrails wrapped
// around every tool call the model makes. Blocking; call from the trigger
// thread, not from inside a reflex action.
class Judgment {
public:
    Judgment(JudgmentConfig cfg, IBody& body, Reflex& reflex, EventLog& log);
    bool enabled() const { return !cfg_.llm_url.empty(); }
    // Runs the agent once with the body tools; returns the model's final
    // sentence, or "" if the layer is disabled or the call failed.
    std::string react(const std::string& trigger, const Context& ctx);

    static std::string system_prompt();
    static std::string user_prompt(const Context& ctx);

private:
    std::vector<tiny_agent::DynamicTool> tools();

    JudgmentConfig cfg_;
    IBody& body_;
    Reflex& reflex_;
    EventLog& log_;
};

}  // namespace pip::brain
