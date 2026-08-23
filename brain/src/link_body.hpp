#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "body.hpp"
#include "log.hpp"
#include "wire.hpp"

namespace pip::brain {

// The body on the other end of the UART. One reader thread decodes what comes
// up (events, senses, hello, pong); commands and audio go down as wire frames
// under a write mutex, so a chirp issued while Pip is speaking slots in
// between two audio frames instead of waiting for the sentence to finish.
//
// When the link is dead and a fallback IBody is set, every command and the
// senses read go over HTTP instead: the demo keeps working with the UART
// unplugged, minus speech.
class LinkBody : public IBody {
public:
    // Opens the device raw at `baud`, 8N1, VMIN 0 / VTIME 1. Throws
    // std::runtime_error if the device cannot be opened or configured: a
    // silently dead link would look exactly like a body that never boots.
    explicit LinkBody(const std::string& dev, int baud = 921600);
    explicit LinkBody(int fd);          // tests hand in a socketpair end; LinkBody owns and closes it
    ~LinkBody() override;
    LinkBody(const LinkBody&) = delete;
    LinkBody& operator=(const LinkBody&) = delete;

    // Called on the reader thread for every {"event":...} frame, so the sink
    // must return fast (Brain::post_event only enqueues).
    void set_event_sink(std::function<void(const json&)> sink);
    void set_fallback(IBody* http);     // not owned; must outlive this
    void set_log(EventLog* log);        // not owned; optional

    bool express(const std::string& emotion) override;
    bool chirp(const std::string& name) override;
    bool led(int r, int g, int b) override;
    bool say(const std::string& text) override;
    bool hud(const HudFields& f) override;
    bool scene(const std::string& name) override;
    bool speak(const std::vector<int16_t>& pcm16k) override;
    bool ping() override;
    bool alive() const override;        // a good frame arrived in the last 2 s
    Senses senses() override;

    struct Stats { uint64_t rx_frames = 0, rx_bad = 0, tx_frames = 0, tx_audio = 0; };
    Stats stats() const;

private:
    void start_reader();
    void reader_loop();
    void on_frame(const wire::Frame& f);
    bool send_json(const json& j);      // one JSON down-frame, or the fallback when the link is dead
    bool write_frame(wire::Type t, const uint8_t* payload, size_t len);
    bool fallback_command(const std::function<bool(IBody&)>& call);

    int fd_ = -1;
    std::atomic<bool> stop_{false};
    std::atomic<int64_t> last_rx_ms_{-1};
    std::atomic<uint64_t> rx_frames_{0}, rx_bad_{0}, tx_frames_{0}, tx_audio_{0};

    std::mutex write_m_;                // serialises command and audio frames
    std::mutex sink_m_;
    std::function<void(const json&)> sink_;
    std::atomic<IBody*> fallback_{nullptr};
    std::atomic<EventLog*> log_{nullptr};

    std::mutex senses_m_;
    Senses senses_{};
    int64_t senses_ms_ = -1;

    std::thread reader_;                // started last
};

}  // namespace pip::brain
