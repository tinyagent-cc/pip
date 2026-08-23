#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pip::brain {

// The Jetson: ears (whisper) and eyes (a VLM). Every call is total -- it
// returns nullopt rather than throwing, because a cortex that is down must
// cost the demo a caption, not a crash.
class Cortex {
public:
    explicit Cortex(std::string base_url);
    bool enabled() const { return !base_url_.empty(); }
    bool ok();                                          // GET /health, 1 s
    std::optional<std::string> listen(int seconds);     // POST /listen, seconds + 10 s
    std::optional<std::string> see(const std::string& question);   // POST /see, 25 s
    std::string last_error() const;

private:
    std::optional<std::string> ask(const char* path, const std::string& body, int read_timeout_s);
    void set_error(std::string e);
    std::string base_url_;
    mutable std::mutex m_;
    std::string last_error_;
};

// The Pi 5: Piper, returning raw s16le mono 16 kHz PCM.
class Voice {
public:
    explicit Voice(std::string base_url);
    bool enabled() const { return !base_url_.empty(); }
    bool ok();                                          // GET /health, 1 s
    std::optional<std::vector<int16_t>> tts(const std::string& text);   // POST /tts, 15 s
    std::string last_error() const;

private:
    void set_error(std::string e);
    std::string base_url_;
    mutable std::mutex m_;
    std::string last_error_;
};

}  // namespace pip::brain
