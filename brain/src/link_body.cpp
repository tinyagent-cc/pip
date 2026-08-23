#include "link_body.hpp"
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace pip::brain {
namespace {

constexpr int64_t ALIVE_MS = 2000;      // no frame for this long and the link is dead
constexpr int64_t SENSES_FRESH_MS = 3000;
constexpr size_t AUDIO_SAMPLES = 256;   // 512 bytes, the wire's payload cap
constexpr int64_t AUDIO_FRAME_MS = 16;  // 256 samples at 16 kHz
constexpr int64_t AUDIO_LEAD_MS = 200;  // how far ahead of real time we may write

// Linux names every rate; Darwin takes the number itself. Falling through to
// the raw value keeps this compiling on the Mac, where the tests never open a
// real device anyway.
speed_t baud_constant(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default: return static_cast<speed_t>(baud);
    }
}

}  // namespace

LinkBody::LinkBody(const std::string& dev, int baud) {
    fd_ = ::open(dev.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) throw std::runtime_error("pip link: cannot open " + dev + ": " + std::strerror(errno));

    termios tio{};
    if (::tcgetattr(fd_, &tio) != 0) {
        int err = errno;
        ::close(fd_);
        throw std::runtime_error("pip link: tcgetattr " + dev + ": " + std::strerror(err));
    }
    ::cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= static_cast<tcflag_t>(~(CSTOPB | PARENB | CRTSCTS));
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;
    speed_t sp = baud_constant(baud);
    ::cfsetispeed(&tio, sp);
    ::cfsetospeed(&tio, sp);
    if (::tcsetattr(fd_, TCSANOW, &tio) != 0) {
        int err = errno;
        ::close(fd_);
        throw std::runtime_error("pip link: tcsetattr " + dev + ": " + std::strerror(err));
    }
    ::tcflush(fd_, TCIOFLUSH);
    start_reader();
}

LinkBody::LinkBody(int fd) : fd_(fd) { start_reader(); }

void LinkBody::start_reader() { reader_ = std::thread(&LinkBody::reader_loop, this); }

LinkBody::~LinkBody() {
    stop_.store(true);
    if (reader_.joinable()) reader_.join();
    if (fd_ >= 0) ::close(fd_);
}

void LinkBody::set_event_sink(std::function<void(const json&)> sink) {
    std::lock_guard<std::mutex> g(sink_m_);
    sink_ = std::move(sink);
}
void LinkBody::set_fallback(IBody* http) { fallback_.store(http); }
void LinkBody::set_log(EventLog* log) { log_.store(log); }

// The poll timeout is what makes the destructor prompt: the thread never
// blocks on read() for longer than that, so stop_ is noticed within 200 ms.
void LinkBody::reader_loop() {
    std::vector<uint8_t> buf(1024);
    wire::Decoder dec;
    while (!stop_.load()) {
        pollfd p{fd_, POLLIN, 0};
        int pr = ::poll(&p, 1, 200);
        if (pr <= 0) continue;                     // timeout, or EINTR: re-check stop_
        ssize_t n = ::read(fd_, buf.data(), buf.size());
        if (n == 0) return;                        // peer closed the link
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (auto* l = log_.load()) l->note(std::string("link read failed: ") + std::strerror(errno));
            return;
        }
        for (ssize_t i = 0; i < n; ++i)
            if (dec.push(buf[static_cast<size_t>(i)])) on_frame(dec.frame());
        rx_frames_.store(dec.frames());
        rx_bad_.store(dec.bad());
    }
}

void LinkBody::on_frame(const wire::Frame& f) {
    last_rx_ms_.store(now_ms());               // any framed byte proves the body is there
    if (f.type != wire::Type::Json) return;    // the body sends no audio up
    auto j = json::parse(f.payload.begin(), f.payload.end(), nullptr, false);
    if (!j.is_object()) {
        if (auto* l = log_.load()) l->note("link: undecodable JSON frame");
        return;
    }
    if (j.contains("event") && j["event"].is_string()) {
        std::function<void(const json&)> sink;
        { std::lock_guard<std::mutex> g(sink_m_); sink = sink_; }
        if (sink) sink(j);
        return;
    }
    if (j.contains("senses") && j["senses"].is_object()) {
        const json& s = j["senses"];
        Senses out;
        out.light_lux = s.value("light_lux", -1.0);
        out.temp_c = s.value("temp_c", 0.0);
        out.button_down = s.value("button", "up") == "down";
        out.ok = true;
        std::lock_guard<std::mutex> g(senses_m_);
        senses_ = out;
        senses_ms_ = now_ms();
        return;
    }
    if (auto* l = log_.load()) {
        if (j.contains("hello")) l->note("link: body hello " + j["hello"].dump());
        else if (!j.contains("pong")) l->note("link: unhandled frame " + j.dump().substr(0, 80));
    }
}

bool LinkBody::write_frame(wire::Type t, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> bytes;
    try {
        bytes = wire::encode(t, payload, len);
    } catch (const std::length_error& e) {
        if (auto* l = log_.load()) l->note(std::string("link: ") + e.what());
        return false;
    }
    std::lock_guard<std::mutex> g(write_m_);
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t n = ::write(fd_, bytes.data() + off, bytes.size() - off);
        if (n > 0) { off += static_cast<size_t>(n); continue; }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            // O_NONBLOCK plus a full kernel buffer: wait for room rather than
            // spinning, and never longer than the link's liveness window.
            pollfd p{fd_, POLLOUT, 0};
            if (::poll(&p, 1, static_cast<int>(ALIVE_MS)) <= 0) return false;
            continue;
        }
        if (auto* l = log_.load()) l->note(std::string("link write failed: ") + std::strerror(errno));
        return false;
    }
    if (t == wire::Type::Audio) ++tx_audio_; else ++tx_frames_;
    return true;
}

bool LinkBody::fallback_command(const std::function<bool(IBody&)>& call) {
    IBody* fb = fallback_.load();
    if (!fb) return false;
    return call(*fb);
}

bool LinkBody::send_json(const json& j) {
    std::string s = j.dump();
    return write_frame(wire::Type::Json, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// Each command: over the wire when the link is alive, over HTTP when it is
// not and a fallback exists, down the (probably dead) wire otherwise, which
// is what a link that has not said hello yet needs.
#define PIP_LINK_CMD(frame, http_call)                                    \
    do {                                                                  \
        if (!alive() && fallback_.load())                                 \
            return fallback_command([&](IBody& b) { return http_call; }); \
        return send_json(frame);                                          \
    } while (0)

bool LinkBody::express(const std::string& e) { PIP_LINK_CMD((json{{"cmd", "express"}, {"emotion", e}}), b.express(e)); }
bool LinkBody::chirp(const std::string& n) { PIP_LINK_CMD((json{{"cmd", "chirp"}, {"name", n}}), b.chirp(n)); }
bool LinkBody::led(int r, int g, int b_) { PIP_LINK_CMD((json{{"cmd", "led"}, {"r", r}, {"g", g}, {"b", b_}}), b.led(r, g, b_)); }
bool LinkBody::say(const std::string& text) {
    std::string cut = text.substr(0, std::min(text.size(), SAY_MAX));
    PIP_LINK_CMD((json{{"cmd", "say"}, {"text", cut}}), b.say(cut));
}
bool LinkBody::hud(const HudFields& f) {
    json j = f.to_json();
    j["cmd"] = "hud";
    PIP_LINK_CMD(j, b.hud(f));
}
bool LinkBody::scene(const std::string& name) { PIP_LINK_CMD((json{{"cmd", "scene"}, {"name", name}}), b.scene(name)); }
bool LinkBody::ping() { PIP_LINK_CMD((json{{"cmd", "ping"}}), b.ping()); }
#undef PIP_LINK_CMD

// Audio is paced so the body's ring never overflows and never starves: frame
// i is written no earlier than i*16 ms after the start, minus a 200 ms lead
// that absorbs a slow write or a busy worker. There is no HTTP path for it.
bool LinkBody::speak(const std::vector<int16_t>& pcm16k) {
    if (!alive()) return false;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> payload(AUDIO_SAMPLES * 2);
    for (size_t off = 0, i = 0; off < pcm16k.size(); off += AUDIO_SAMPLES, ++i) {
        size_t n = std::min(AUDIO_SAMPLES, pcm16k.size() - off);
        auto target = t0 + std::chrono::milliseconds(static_cast<int64_t>(i) * AUDIO_FRAME_MS - AUDIO_LEAD_MS);
        std::this_thread::sleep_until(target);   // already past: returns at once
        for (size_t k = 0; k < n; ++k) {
            auto v = static_cast<uint16_t>(pcm16k[off + k]);
            payload[2 * k] = static_cast<uint8_t>(v & 0xFF);
            payload[2 * k + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        }
        if (!write_frame(wire::Type::Audio, payload.data(), n * 2)) return false;
    }
    return true;
}

bool LinkBody::alive() const {
    int64_t last = last_rx_ms_.load();
    return last >= 0 && now_ms() - last < ALIVE_MS;
}

Senses LinkBody::senses() {
    {
        std::lock_guard<std::mutex> g(senses_m_);
        if (senses_.ok && senses_ms_ >= 0 && now_ms() - senses_ms_ < SENSES_FRESH_MS) return senses_;
    }
    if (IBody* fb = fallback_.load()) return fb->senses();
    return Senses{};
}

LinkBody::Stats LinkBody::stats() const {
    Stats s;
    s.rx_frames = rx_frames_.load();
    s.rx_bad = rx_bad_.load();
    s.tx_frames = tx_frames_.load();
    s.tx_audio = tx_audio_.load();
    return s;
}

}  // namespace pip::brain
