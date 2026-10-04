#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <condition_variable>
#include "vision/tracker_confidence.hpp"

struct WebSnapshot {
    std::vector<uint8_t> jpeg;
    std::string tracking_json = "{}";
};

class WebServer {
public:
    explicit WebServer(TrackerConfidence& confidence, std::uint16_t port = 2026);
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    void publish(WebSnapshot snapshot);
    void publish_frame(std::vector<uint8_t> jpeg);
    void publish_tracking(std::string tracking_json);
    bool start();
    void stop();

private:
    struct State;
    std::unique_ptr<State> state_;
};