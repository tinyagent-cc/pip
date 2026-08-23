#pragma once
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "wire.hpp"

namespace pip::brain {
using json = nlohmann::json;

// The body's end of the link. A socketpair stands in for the UART: it is a
// bidirectional byte stream with no framing of its own, which is exactly what
// the wire codec has to cope with. The peer thread decodes everything the
// brain writes and hands the test JSON commands and audio payloads.
class FakeLink {
public:
    FakeLink() {
        int fds[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) throw std::runtime_error("FakeLink: socketpair failed");
        brain_fd_ = fds[0];
        peer_fd_ = fds[1];
        th_ = std::thread([this] { loop(); });
    }
    ~FakeLink() {
        stop_.store(true);
        if (peer_fd_ >= 0) { ::shutdown(peer_fd_, SHUT_RDWR); }
        th_.join();
        if (peer_fd_ >= 0) ::close(peer_fd_);
        if (brain_fd_ >= 0) ::close(brain_fd_);
    }

    // Hands the brain's end to LinkBody, which owns and closes it.
    int take_brain_fd() { int fd = brain_fd_; brain_fd_ = -1; return fd; }

    void send_json(const json& j) { send_raw(wire::encode_json(j)); }
    void send_raw(const std::vector<uint8_t>& bytes) {
        size_t off = 0;
        while (off < bytes.size()) {
            ssize_t n = ::write(peer_fd_, bytes.data() + off, bytes.size() - off);
            if (n <= 0) return;
            off += static_cast<size_t>(n);
        }
    }

    std::vector<json> commands() const { std::lock_guard<std::mutex> g(m_); return commands_; }
    std::vector<std::vector<uint8_t>> audio() const { std::lock_guard<std::mutex> g(m_); return audio_; }
    uint32_t bad() const { std::lock_guard<std::mutex> g(m_); return dec_.bad(); }

    // Polls rather than signals: the reader is a separate thread and the
    // tests care about "it arrived within a second", not about the exact
    // wake-up.
    bool wait_commands(size_t n, int timeout_ms = 2000) const { return wait_for([&] { return commands().size() >= n; }, timeout_ms); }
    bool wait_audio(size_t n, int timeout_ms = 2000) const { return wait_for([&] { return audio().size() >= n; }, timeout_ms); }
    template <typename Pred>
    static bool wait_for(Pred p, int timeout_ms = 2000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (p()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return p();
    }

private:
    void loop() {
        std::vector<uint8_t> buf(1024);
        while (!stop_.load()) {
            ssize_t n = ::read(peer_fd_, buf.data(), buf.size());
            if (n <= 0) { if (n < 0 && errno == EINTR) continue; return; }
            std::lock_guard<std::mutex> g(m_);
            for (ssize_t i = 0; i < n; ++i) {
                if (!dec_.push(buf[static_cast<size_t>(i)])) continue;
                const auto& f = dec_.frame();
                if (f.type == wire::Type::Json) {
                    auto j = json::parse(f.payload.begin(), f.payload.end(), nullptr, false);
                    commands_.push_back(j.is_discarded() ? json::object() : j);
                } else {
                    audio_.push_back(f.payload);
                }
            }
        }
    }

    int brain_fd_ = -1, peer_fd_ = -1;
    std::atomic<bool> stop_{false};
    mutable std::mutex m_;
    wire::Decoder dec_;
    std::vector<json> commands_;
    std::vector<std::vector<uint8_t>> audio_;
    std::thread th_;
};
}  // namespace pip::brain
