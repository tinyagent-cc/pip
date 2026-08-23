#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cerrno>
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

// strtol/strtod with a whole-string check: rejects "", "abc", and "12abc" alike.
bool parse_int(const char* s, int& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    errno = 0;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE) return false;
    out = static_cast<int>(v);
    return true;
}
bool parse_double(const char* s, double& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE) return false;
    out = v;
    return true;
}

// Parses argv into cfg. On any problem (unknown flag, missing or malformed
// value, missing --pip) it prints an error plus usage to stderr and returns
// false; the caller exits 2. --help prints usage to stdout and exits 0
// immediately.
bool parse_args(int argc, char** argv, Config& cfg) {
    auto fail = [](const std::string& msg) {
        std::fprintf(stderr, "pip-brain: %s\n", msg.c_str());
        print_usage(stderr);
        return false;
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help") { print_usage(stdout); std::exit(0); }
        // A value that itself looks like a flag ("--foo") means the real
        // value was omitted, not that "--foo" is the value.
        auto need_value = [&]() -> const char* {
            if (i + 1 >= argc) return nullptr;
            std::string next = argv[i + 1];
            if (next.rfind("--", 0) == 0) return nullptr;
            return argv[++i];
        };
        if (a == "--pip") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            cfg.pip_url = v;
        } else if (a == "--listen-port") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 1 || n > 65535) return fail(a + " must be between 1 and 65535");
            cfg.listen_port = n;
        } else if (a == "--llm") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            cfg.llm_url = v;
        } else if (a == "--model") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            cfg.model = v;
        } else if (a == "--llm2") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            cfg.llm2_url = v;
        } else if (a == "--hot-c") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            double f; if (!parse_double(v, f)) return fail(a + " needs a number");
            cfg.hot_c = f;
        } else if (a == "--night-cap") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 0 || n > 255) return fail(a + " must be between 0 and 255");
            cfg.night_cap = n;
        } else if (a == "--chirp-gap-ms") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 0) return fail(a + " must not be negative");
            cfg.chirp_gap_ms = n;
        } else if (a == "--senses-poll-ms") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 100) return fail(a + " must be at least 100");
            cfg.senses_poll_ms = n;
        } else if (a == "--llm-timeout-s") {
            const char* v = need_value(); if (!v) return fail(a + " needs a value");
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 0) return fail(a + " must not be negative");
            cfg.llm_timeout_s = n;
        } else {
            return fail("unknown flag " + a);
        }
    }
    if (cfg.pip_url.empty()) return fail("--pip is required");
    return true;
}

std::atomic<httplib::Server*> g_server{nullptr};
extern "C" void handle_stop_signal(int) {
    httplib::Server* s = g_server.load();
    if (s) s->stop();
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

    if (!svr.bind_to_port("0.0.0.0", cfg.listen_port)) {
        std::fprintf(stderr, "pip-brain: failed to bind 0.0.0.0:%d (port in use?)\n", cfg.listen_port);
        return 1;
    }

    log.note("pip-brain listening on :" + std::to_string(cfg.listen_port) + " body=" + cfg.pip_url +
              " llm=" + (cfg.llm_url.empty() ? "off" : cfg.llm_url));
    bool body_up = body.senses().ok;
    log.note(std::string("startup body probe: ") + (body_up ? "ok" : "no reply yet"));

    g_server.store(&svr);
    svr.listen_after_bind();
    g_server.store(nullptr);
    return 0;
}
