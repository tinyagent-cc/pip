#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "body.hpp"
#include "brain.hpp"
#include "log.hpp"

namespace pip::brain {

// Scenes are mostly waiting, so the wait is the thing tests have to be able
// to skip. A fake clock runs a two-minute tour in milliseconds and still
// proves the script slept for the right two minutes.
struct Clock {
    virtual ~Clock() = default;
    virtual void sleep_ms(int ms) = 0;
    virtual int64_t now_ms() = 0;
};

struct SteadyClock : Clock {
    void sleep_ms(int ms) override;
    int64_t now_ms() override;
};

// The demo's stage manager. One scene at a time, on its own thread, so the
// HTTP handler that started it returns immediately and Riadh's hands stay
// free for the camera.
class Director {
public:
    Director(Brain& brain, IBody& body, EventLog& log, Clock& clock);
    ~Director();
    Director(const Director&) = delete;
    Director& operator=(const Director&) = delete;

    // False if the name is unknown or a scene is already running.
    bool run(const std::string& name);
    bool running() const { return running_.load(); }
    std::string current() const;
    void wait();

    static std::vector<std::string> names();

private:
    void play(const std::string& name);
    void say(const std::string& line);          // bubble plus voice, whichever Pip has
    void caption(const std::string& name);
    // Waits ms, and reports whether `event` arrived while it waited.
    bool wait_for_event(const std::string& event, int ms);

    void scene_reflex();
    void scene_night();
    void scene_fallback();
    void scene_fever();
    void scene_who();
    void scene_tour();

    Brain& brain_;
    IBody& body_;
    EventLog& log_;
    Clock& clock_;
    std::atomic<bool> running_{false};
    mutable std::mutex m_;
    std::string current_;
    std::thread th_;
};

}  // namespace pip::brain
