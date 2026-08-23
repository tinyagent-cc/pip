#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include "body.hpp"
#include "log.hpp"
#include "services.hpp"

namespace pip::brain {

// Speech on its own thread. The bubble goes up the moment say() is called --
// that is the part the demo cannot afford to lag -- and the TTS round trip
// plus the paced PCM stream happen behind it. The worker thread that runs the
// judgment never blocks on the voice.
class Speaker {
public:
    static constexpr size_t QUEUE_CAP = 4;

    Speaker(IBody& body, Voice& voice, EventLog& log);
    ~Speaker();
    Speaker(const Speaker&) = delete;
    Speaker& operator=(const Speaker&) = delete;

    // Shows `text` on the body immediately; queues it for the voice unless
    // also_speak is false. Over QUEUE_CAP pending lines the oldest is dropped
    // with a log note: falling behind by five sentences means the demo has
    // moved on, and the newest line is the one worth hearing.
    // lang picks the Piper voice ("en", "fr", "ar"); empty means default.
    void say(const std::string& text, bool also_speak = true, const std::string& lang = "");
    bool busy() const;
    void wait_idle();
    bool last_voice_ok() const { return voice_ok_.load(); }

private:
    void loop();

    IBody& body_;
    Voice& voice_;
    EventLog& log_;
    mutable std::mutex m_;
    std::condition_variable cv_, idle_cv_;
    struct Line { std::string text; std::string lang; };
    std::deque<Line> queue_;
    bool speaking_ = false;
    bool stop_ = false;
    std::atomic<bool> voice_ok_{true};
    std::thread th_;
};

}  // namespace pip::brain
