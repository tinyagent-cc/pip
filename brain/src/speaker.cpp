#include "speaker.hpp"
#include <exception>

namespace pip::brain {

Speaker::Speaker(IBody& body, Voice& voice, EventLog& log)
    : body_(body), voice_(voice), log_(log), th_(&Speaker::loop, this) {}

Speaker::~Speaker() {
    {
        std::lock_guard<std::mutex> g(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

void Speaker::say(const std::string& text, bool also_speak) {
    if (text.empty()) return;
    body_.say(text);                       // the bubble is instant; the voice catches up
    if (!also_speak || !voice_.enabled()) return;
    {
        std::lock_guard<std::mutex> g(m_);
        if (queue_.size() >= QUEUE_CAP) {
            log_.note("speaker: dropped queued line \"" + queue_.front() + "\" (queue full)");
            queue_.pop_front();
        }
        queue_.push_back(text);
    }
    cv_.notify_all();
}

void Speaker::loop() {
    std::unique_lock<std::mutex> lock(m_);
    while (true) {
        idle_cv_.notify_all();
        cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (stop_) break;
        std::string text = std::move(queue_.front());
        queue_.pop_front();
        speaking_ = true;
        lock.unlock();
        try {
            auto pcm = voice_.tts(text);
            if (!pcm) {
                voice_ok_.store(false);
                log_.note("speaker: voice failed (" + voice_.last_error() + ")");
            } else {
                voice_ok_.store(true);
                if (!body_.speak(*pcm)) log_.note("speaker: body would not take " + std::to_string(pcm->size()) + " samples");
            }
        } catch (const std::exception& e) {
            voice_ok_.store(false);
            log_.note(std::string("speaker: ") + e.what());
        }
        lock.lock();
        speaking_ = false;
    }
    speaking_ = false;
    idle_cv_.notify_all();
}

bool Speaker::busy() const {
    std::lock_guard<std::mutex> g(m_);
    return speaking_ || !queue_.empty();
}

void Speaker::wait_idle() {
    std::unique_lock<std::mutex> lock(m_);
    // stop_ short-circuits so a waiter cannot hang while the destructor runs.
    idle_cv_.wait(lock, [this] { return stop_ || (queue_.empty() && !speaking_); });
}

}  // namespace pip::brain
