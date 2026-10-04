#include "web/web_server.hpp"
#include "web/httplib.h"
#include <fstream>
#include <iterator>
#include <cmath>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

struct WebServer::State {
    // keep port, shared snapshot, server, worker in one owned state
    State(std::uint16_t selected_port, TrackerConfidence& shared_confidence)
        : port(selected_port), confidence(shared_confidence) {}

    std::uint16_t port;
    TrackerConfidence& confidence;
    std::mutex mutex;
    WebSnapshot latest;
    httplib::Server server;
    std::thread worker;
    std::condition_variable updated;
    std::uint64_t sequence = 0;
    bool stopping = false;

    void write_stream(httplib::DataSink& sink) {
        std::uint64_t last_sequence = 0;

        while (sink.is_writable()) {
            std::vector<std::uint8_t> jpeg;

            {
                std::unique_lock lock(mutex);
                // wait for a new snapshot, copy it before network writes
                updated.wait(lock, [&] {
                    return sequence != last_sequence || stopping;
                });
                if (stopping) return;
                jpeg = latest.jpeg;
                last_sequence = sequence;
            }

            // frame boundary and content length, required by multipart mjpeg clients
            const std::string header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " 
                + std::to_string(jpeg.size()) + "\r\n\r\n";
            
            if (!sink.write(header.data(), header.size()) 
            || !sink.write(reinterpret_cast<const char*>(jpeg.data()), jpeg.size()) 
            || !sink.write("\r\n", 2)) {
                return;
            }
        }
    }

    void register_routes() {
        server.set_mount_point("/", GESTURED_WEBUI_DIR);

        // stream each published jpeg as a multipart video frame
        server.Get("/stream.mjpg",[this](const httplib::Request&, httplib::Response& response) {
            response.set_chunked_content_provider("multipart/x-mixed-replace; boundary=frame",
                [this](size_t, httplib::DataSink& sink) {
                write_stream(sink);
                return false;
            });
        });

        server.Get("/tracking.json",[this](const httplib::Request&, httplib::Response& response) {
            std::string json;
            std::lock_guard lock(mutex);
            json = latest.tracking_json;
            response.set_content(json, "application/json");
        });

        // return current thresholds, read atomically alongside inference
        server.Get("/confidence", [this](const httplib::Request&, httplib::Response& response) {
            std::ostringstream json;
            json << "{\"palm\":" << confidence.palm.load() << ",\"landmark\":" << confidence.landmark.load() << "}";
            response.set_content(json.str(), "application/json");
        });

        // accept finite thresholds only, both values must stay within zero and one
        server.Post("/confidence", [this](const httplib::Request& request, httplib::Response& response) {
            try {
                const float palm = std::stof(request.get_param_value("palm"));
                const float landmark = std::stof(request.get_param_value("landmark"));
                if (!std::isfinite(palm) || !std::isfinite(landmark) ||
                    palm < 0.0f || palm > 1.0f || landmark < 0.0f || landmark > 1.0f) {
                    response.status = 400;
                    return;
                }
                confidence.palm.store(palm);
                confidence.landmark.store(landmark);
                response.status = 204;
            } catch (const std::exception&) {
                response.status = 400;
            }
        });
    }

};

WebServer::WebServer(TrackerConfidence& confidence, std::uint16_t port)
    : state_(std::make_unique<State>(port, confidence)) {}

WebServer::~WebServer() {
    // stop the worker before releasing server state
    stop();
}

void WebServer::publish(WebSnapshot snapshot) {
    // protect shared data, move in the latest camera snapshot
    std::lock_guard lock(state_->mutex);
    state_->latest = std::move(snapshot);
    ++state_->sequence;
    // wake streaming clients, a new frame is ready
    state_->updated.notify_all();
}

void WebServer::publish_frame(std::vector<std::uint8_t> jpeg) {
    {
        std::lock_guard lock(state_->mutex);
        state_->latest.jpeg = std::move(jpeg);
        ++state_->sequence;
    }
    state_->updated.notify_all();
}

void WebServer::publish_tracking(std::string tracking_json) {
    std::lock_guard lock(state_->mutex);
    state_->latest.tracking_json = std::move(tracking_json);
}

bool WebServer::start() {
    // do not start a second worker for the same server
    if (state_->worker.joinable()) return false;

    state_->register_routes();
    // start the server worker thread
    if (!state_->server.bind_to_port("127.0.0.1", state_->port)) return false;

    state_->worker = std::thread([state = state_.get()] {
        state->server.listen_after_bind();
    });

    return true;
}

void WebServer::stop() {
    // wake blocked stream readers, stop the server loop, join its worker
    {
        std::lock_guard lock(state_->mutex);
        state_->stopping = true;
        state_->updated.notify_all();
    }
    state_->server.stop();
    if (state_->worker.joinable()) state_->worker.join();
}