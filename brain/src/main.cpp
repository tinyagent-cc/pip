#include <httplib.h>
#include <nlohmann/json.hpp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include "body.hpp"
#include "brain.hpp"
#include "judgment.hpp"
#include "log.hpp"
#include "policy.hpp"

using json = nlohmann::json;
using pip::brain::Brain;
using pip::brain::BrainConfig;
using pip::brain::EventLog;
using pip::brain::HttpBody;
using pip::brain::JudgmentConfig;
using pip::brain::Policy;

namespace {

struct Config {
    std::string pip_url;
    int listen_port = 8080;
    std::string llm_url;
    std::string model = "Qwen2.5-3B-Instruct";
    std::string llm2_url;
    double hot_c = 35.0;
    int night_cap = 40;
    int chirp_gap_ms = 5000;
    int senses_poll_ms = 10000;
    int llm_timeout_s = 90;
};

void print_usage(FILE* out) {
    std::fprintf(out,
        "usage: pip-brain --pip URL [--listen-port N] [--llm URL] [--model NAME]\n"
        "                  [--llm2 URL] [--hot-c F] [--night-cap N] [--chirp-gap-ms N]\n"
        "                  [--senses-poll-ms N] [--llm-timeout-s N] [--help]\n");
}

// Parses argv into cfg. On any problem (unknown flag, missing value, missing
// --pip) it prints usage to stderr and returns false; the caller exits 2.
// --help prints usage to stdout and exits 0 immediately.
bool parse_args(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help") { print_usage(stdout); std::exit(0); }
        auto need_value = [&]() -> const char* {
            if (i + 1 >= argc) return nullptr;
            return argv[++i];
        };
        const char* v = nullptr;
        bool known = true;
        if (a == "--pip") { if ((v = need_value())) cfg.pip_url = v; }
        else if (a == "--listen-port") { if ((v = need_value())) cfg.listen_port = std::atoi(v); }
        else if (a == "--llm") { if ((v = need_value())) cfg.llm_url = v; }
        else if (a == "--model") { if ((v = need_value())) cfg.model = v; }
        else if (a == "--llm2") { if ((v = need_value())) cfg.llm2_url = v; }
        else if (a == "--hot-c") { if ((v = need_value())) cfg.hot_c = std::atof(v); }
        else if (a == "--night-cap") { if ((v = need_value())) cfg.night_cap = std::atoi(v); }
        else if (a == "--chirp-gap-ms") { if ((v = need_value())) cfg.chirp_gap_ms = std::atoi(v); }
        else if (a == "--senses-poll-ms") { if ((v = need_value())) cfg.senses_poll_ms = std::atoi(v); }
        else if (a == "--llm-timeout-s") { if ((v = need_value())) cfg.llm_timeout_s = std::atoi(v); }
        else known = false;
        if (!known) { std::fprintf(stderr, "pip-brain: unknown flag %s\n", a.c_str()); print_usage(stderr); return false; }
        if (!v) { std::fprintf(stderr, "pip-brain: %s needs a value\n", a.c_str()); print_usage(stderr); return false; }
    }
    if (cfg.pip_url.empty()) { std::fprintf(stderr, "pip-brain: --pip is required\n"); print_usage(stderr); return false; }
    return true;
}

httplib::Server* g_server = nullptr;
extern "C" void handle_stop_signal(int) {
    if (g_server) g_server->stop();
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) return 2;

    HttpBody body(cfg.pip_url);
    Policy policy;
    policy.night_cap = cfg.night_cap;
    policy.chirp_gap_ms = cfg.chirp_gap_ms;
    policy.hot_c = cfg.hot_c;
    EventLog log;

    Brain brain({.senses_poll_ms = cfg.senses_poll_ms}, body, policy, log,
                {.llm_url = cfg.llm_url, .model = cfg.model, .llm2_url = cfg.llm2_url, .timeout_s = cfg.llm_timeout_s});

    httplib::Server svr;
    g_server = &svr;

    svr.Post("/event", [&](const httplib::Request& rq, httplib::Response& rs) {
        auto j = json::parse(rq.body, nullptr, false);
        bool ok = j.is_object() && brain.post_event(j);
        rs.status = ok ? 200 : 400;
        rs.set_content(ok ? R"({"ok":true})" : R"({"ok":false,"error":"bad event"})", "application/json");
    });
    svr.Get("/health", [&](const httplib::Request&, httplib::Response& rs) {
        rs.set_content(brain.health().dump(), "application/json");
    });
    svr.Get("/log", [&](const httplib::Request& rq, httplib::Response& rs) {
        size_t n = 50;
        if (rq.has_param("n")) {
            try { n = std::stoul(rq.get_param_value("n")); } catch (const std::exception&) { n = 50; }
        }
        rs.set_content(brain.log_tail(n).dump(), "application/json");
    });

    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);

    log.note("pip-brain listening on :" + std::to_string(cfg.listen_port) + " body=" + cfg.pip_url +
              " llm=" + (cfg.llm_url.empty() ? "off" : cfg.llm_url));
    bool body_up = body.senses().ok;
    log.note(std::string("startup body probe: ") + (body_up ? "ok" : "no reply yet"));

    svr.listen("0.0.0.0", cfg.listen_port);
    return 0;
}
