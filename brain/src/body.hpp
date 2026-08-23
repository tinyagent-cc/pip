#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <string>

namespace pip::brain {
using json = nlohmann::json;

struct Senses { double light_lux = -1; double temp_c = 0; bool button_down = false; bool ok = false; };

class IBody {
public:
    virtual ~IBody() = default;
    virtual bool express(const std::string& emotion) = 0;
    virtual bool chirp(const std::string& name) = 0;
    virtual bool led(int r, int g, int b) = 0;
    virtual Senses senses() = 0;
};

class HttpBody : public IBody {   // base_url like "http://192.168.1.110"; 1 s timeouts
public:
    explicit HttpBody(std::string base_url, int timeout_ms = 1000);
    bool express(const std::string& emotion) override;
    bool chirp(const std::string& name) override;
    bool led(int r, int g, int b) override;
    Senses senses() override;
private:
    bool post(const char* path, const json& body);
    std::string base_url_; int timeout_ms_;
    std::mutex m_;                       // one client, one request at a time
    std::unique_ptr<httplib::Client> cli_;
};
}  // namespace pip::brain
