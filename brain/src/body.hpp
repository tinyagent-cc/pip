#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pip::brain {
using json = nlohmann::json;

struct Senses { double light_lux = -1; double temp_c = 0; bool button_down = false; bool ok = false; };

// The HUD strip along the top of the body's screen. Every field is optional
// because a push updates only what changed: a reflex pushes reflex_us, the
// judgment pushes judge_ms and mind, the poller pushes the service booleans.
// Unset fields are absent from the JSON and the body leaves those captions
// alone.
struct HudFields {
    std::optional<long> reflex_us;
    std::optional<long> judge_ms;
    std::optional<bool> brain;
    std::optional<bool> cortex;
    std::optional<char> mind;          // 'J' Jetson, '5' Pi 5, '-' nobody
    std::optional<std::string> scene;
    json to_json() const {
        json j = json::object();
        if (reflex_us) j["reflex_us"] = *reflex_us;
        if (judge_ms) j["judge_ms"] = *judge_ms;
        if (brain) j["brain"] = *brain;
        if (cortex) j["cortex"] = *cortex;
        if (mind) j["mind"] = std::string(1, *mind);
        if (scene) j["scene"] = *scene;
        return j;
    }
    bool empty() const { return !reflex_us && !judge_ms && !brain && !cortex && !mind && !scene; }
};

// The body's speech bubble holds 95 characters; anything longer is cut here
// rather than on the wire, so the log shows what the body actually displayed.
constexpr size_t SAY_MAX = 95;

class IBody {
public:
    virtual ~IBody() = default;
    virtual bool express(const std::string& emotion) = 0;
    virtual bool chirp(const std::string& name) = 0;
    virtual bool led(int r, int g, int b) = 0;
    virtual bool say(const std::string& text) = 0;               // truncated to SAY_MAX
    virtual bool hud(const HudFields& f) = 0;
    virtual bool scene(const std::string& name) = 0;
    virtual bool speak(const std::vector<int16_t>& pcm16k) = 0;  // blocking, paced at real time
    virtual bool ping() = 0;
    virtual bool alive() const = 0;
    virtual Senses senses() = 0;
};

class HttpBody : public IBody {   // base_url like "http://192.168.1.110"; 1 s timeouts
public:
    explicit HttpBody(std::string base_url, int timeout_ms = 1000);
    bool express(const std::string& emotion) override;
    bool chirp(const std::string& name) override;
    bool led(int r, int g, int b) override;
    bool say(const std::string& text) override;
    bool hud(const HudFields& f) override;
    bool scene(const std::string& name) override;
    bool speak(const std::vector<int16_t>& pcm16k) override;   // always false: HTTP carries no audio
    bool ping() override;
    bool alive() const override;
    Senses senses() override;
private:
    bool post(const char* path, const json& body);
    std::string base_url_; int timeout_ms_;
    std::mutex m_;                       // one client, one request at a time
    std::atomic<bool> alive_{false};     // whether the last call to the body worked
    std::unique_ptr<httplib::Client> cli_;
};
}  // namespace pip::brain
