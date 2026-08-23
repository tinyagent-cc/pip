#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "body.hpp"
#include "brain.hpp"
#include "director.hpp"
#include "judgment.hpp"
#include "link_body.hpp"
#include "log.hpp"
#include "policy.hpp"
#include "services.hpp"

using json = nlohmann::json;
using pip::brain::Brain;
using pip::brain::BrainConfig;
using pip::brain::Cortex;
using pip::brain::Director;
using pip::brain::EventLog;
using pip::brain::HttpBody;
using pip::brain::IBody;
using pip::brain::JudgmentConfig;
using pip::brain::LinkBody;
using pip::brain::Policy;
using pip::brain::SteadyClock;
using pip::brain::Voice;

namespace {

struct Config {
    std::string link_dev;
    int baud = 921600;
    std::string pip_url;
    int listen_port = 8080;
    std::string llm_url;
    std::string model = "Qwen2.5-3B-Instruct";
    std::string llm2_url;
    std::string cortex_url;
    std::string voice_url;
    bool tour = false;
    int listen_seconds = 4;
    double hot_c = 35.0;
    int night_cap = 40;
    int chirp_gap_ms = 5000;
    int senses_poll_ms = 10000;
    int llm_timeout_s = 90;
    int max_tokens = 200;      // per model turn; thinking models want ~1500
};

void print_usage(FILE* out) {
    std::fprintf(out,
        "usage: pip-brain (--link DEV | --pip URL) [--baud N] [--listen-port N]\n"
        "                  [--llm URL] [--model NAME] [--llm2 URL]\n"
        "                  [--cortex URL] [--voice URL] [--tour] [--listen-seconds N]\n"
        "                  [--hot-c F] [--night-cap N] [--chirp-gap-ms N]\n"
        "                  [--senses-poll-ms N] [--llm-timeout-s N] [--max-tokens N] [--help]\n"
        "\n"
        "  --link DEV        UART to the body, e.g. /dev/ttyAMA0 (events and audio)\n"
        "  --baud N          link speed, default 921600\n"
        "  --pip URL         body over HTTP; with --link it becomes the fallback\n"
        "  --cortex URL      Jetson ears and eyes, e.g. http://orin-desktop.local:8090\n"
        "  --voice URL       Pi 5 text to speech, e.g. http://pi5.local:8091\n"
        "  --tour            run the tour scene once, ten seconds after start\n");
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
// value, no body at all) it prints an error plus usage to stderr and returns
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
        if (a == "--tour") { cfg.tour = true; continue; }
        // A value that itself looks like a flag ("--foo") means the real
        // value was omitted, not that "--foo" is the value.
        auto need_value = [&]() -> const char* {
            if (i + 1 >= argc) return nullptr;
            std::string next = argv[i + 1];
            if (next.rfind("--", 0) == 0) return nullptr;
            return argv[++i];
        };
        const char* v = need_value();
        if (!v) return fail(a + " needs a value");
        if (a == "--link") cfg.link_dev = v;
        else if (a == "--pip") cfg.pip_url = v;
        else if (a == "--llm") cfg.llm_url = v;
        else if (a == "--model") cfg.model = v;
        else if (a == "--llm2") cfg.llm2_url = v;
        else if (a == "--cortex") cfg.cortex_url = v;
        else if (a == "--voice") cfg.voice_url = v;
        else if (a == "--baud") {
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 1200) return fail(a + " must be at least 1200");
            cfg.baud = n;
        } else if (a == "--listen-port") {
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 1 || n > 65535) return fail(a + " must be between 1 and 65535");
            cfg.listen_port = n;
        } else if (a == "--listen-seconds") {
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 1 || n > 30) return fail(a + " must be between 1 and 30");
            cfg.listen_seconds = n;
        } else if (a == "--hot-c") {
            double f; if (!parse_double(v, f)) return fail(a + " needs a number");
            cfg.hot_c = f;
        } else if (a == "--night-cap") {
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 0 || n > 255) return fail(a + " must be between 0 and 255");
            cfg.night_cap = n;
        } else if (a == "--chirp-gap-ms") {
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 0) return fail(a + " must not be negative");
            cfg.chirp_gap_ms = n;
        } else if (a == "--senses-poll-ms") {
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 100) return fail(a + " must be at least 100");
            cfg.senses_poll_ms = n;
        } else if (a == "--max-tokens") {
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 1) return fail(a + " must be at least 1");
            cfg.max_tokens = n;
        } else if (a == "--llm-timeout-s") {
            int n; if (!parse_int(v, n)) return fail(a + " needs a number");
            if (n < 0) return fail(a + " must not be negative");
            cfg.llm_timeout_s = n;
        } else {
            return fail("unknown flag " + a);
        }
    }
    if (cfg.link_dev.empty() && cfg.pip_url.empty()) return fail("one of --link or --pip is required");
    return true;
}

std::atomic<httplib::Server*> g_server{nullptr};
extern "C" void handle_stop_signal(int) {
    httplib::Server* s = g_server.load();
    if (s) s->stop();
}

// A delayed one-shot that can be cancelled, so shutting down inside the
// ten-second window does not leave a thread poking at destroyed objects.
class DelayedTour {
public:
    DelayedTour(Director& director, int delay_ms)
        : th_([this, &director, delay_ms] {
              std::unique_lock<std::mutex> lock(m_);
              if (cv_.wait_for(lock, std::chrono::milliseconds(delay_ms), [this] { return cancel_; })) return;
              lock.unlock();
              director.run("tour");
          }) {}
    ~DelayedTour() {
        { std::lock_guard<std::mutex> g(m_); cancel_ = true; }
        cv_.notify_all();
        if (th_.joinable()) th_.join();
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    bool cancel_ = false;
    std::thread th_;
};

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) return 2;

    EventLog log;
    Policy policy;
    policy.night_cap = cfg.night_cap;
    policy.chirp_gap_ms = cfg.chirp_gap_ms;
    policy.hot_c = cfg.hot_c;

    std::unique_ptr<HttpBody> http_body;
    if (!cfg.pip_url.empty()) http_body = std::make_unique<HttpBody>(cfg.pip_url);

    std::unique_ptr<LinkBody> link_body;
    if (!cfg.link_dev.empty()) {
        try {
            link_body = std::make_unique<LinkBody>(cfg.link_dev, cfg.baud);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "pip-brain: %s\n", e.what());
            return 1;
        }
        link_body->set_log(&log);
        if (http_body) link_body->set_fallback(http_body.get());
    }
    IBody& body = link_body ? static_cast<IBody&>(*link_body) : static_cast<IBody&>(*http_body);

    Cortex cortex(cfg.cortex_url);
    Voice voice(cfg.voice_url);

    BrainConfig bcfg;
    bcfg.senses_poll_ms = cfg.senses_poll_ms;
    bcfg.listen_seconds = cfg.listen_seconds;
    JudgmentConfig jcfg;
    jcfg.llm_url = cfg.llm_url;
    jcfg.model = cfg.model;
    jcfg.llm2_url = cfg.llm2_url;
    jcfg.timeout_s = cfg.llm_timeout_s;
    jcfg.max_tokens = cfg.max_tokens;

    Brain brain(bcfg, body, policy, log, jcfg,
                cortex.enabled() ? &cortex : nullptr,
                voice.enabled() ? &voice : nullptr);

    // Events off the wire go through the same door as events off the LAN.
    if (link_body) link_body->set_event_sink([&brain](const json& j) { brain.post_event(j); });

    SteadyClock clock;
    Director director(brain, body, log, clock);

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
    svr.Get("/scenes", [&](const httplib::Request&, httplib::Response& rs) {
        rs.set_content(json(Director::names()).dump(), "application/json");
    });
    svr.Post("/scene", [&](const httplib::Request& rq, httplib::Response& rs) {
        auto j = json::parse(rq.body, nullptr, false);
        std::string name = j.is_object() ? j.value("name", std::string()) : std::string();
        auto all = Director::names();
        if (std::find(all.begin(), all.end(), name) == all.end()) {
            rs.status = 404;
            rs.set_content(R"({"ok":false,"error":"no such scene"})", "application/json");
            return;
        }
        if (!director.run(name)) {          // known name, so this is the busy case
            rs.status = 409;
            rs.set_content(json{{"ok", false}, {"error", "scene already running"}, {"current", director.current()}}.dump(),
                           "application/json");
            return;
        }
        rs.set_content(json{{"ok", true}, {"scene", name}}.dump(), "application/json");
    });

    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);

    if (!svr.bind_to_port("0.0.0.0", cfg.listen_port)) {
        std::fprintf(stderr, "pip-brain: failed to bind 0.0.0.0:%d (port in use?)\n", cfg.listen_port);
        return 1;
    }

    log.note("pip-brain listening on :" + std::to_string(cfg.listen_port) +
             " body=" + (link_body ? cfg.link_dev + "@" + std::to_string(cfg.baud) : cfg.pip_url) +
             (link_body && http_body ? " fallback=" + cfg.pip_url : "") +
             " llm=" + (cfg.llm_url.empty() ? "off" : cfg.llm_url) +
             " cortex=" + (cfg.cortex_url.empty() ? "off" : cfg.cortex_url) +
             " voice=" + (cfg.voice_url.empty() ? "off" : cfg.voice_url));
    if (link_body) link_body->ping();
    bool body_up = body.senses().ok;
    log.note(std::string("startup body probe: ") + (body_up ? "ok" : "no reply yet"));

    {
        std::unique_ptr<DelayedTour> tour;
        if (cfg.tour) tour = std::make_unique<DelayedTour>(director, 10000);
        g_server.store(&svr);
        svr.listen_after_bind();
        g_server.store(nullptr);
    }
    director.wait();
    return 0;
}
