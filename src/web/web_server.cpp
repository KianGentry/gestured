#include "web/web_server.hpp"
#include "web/httplib.h"
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <utility>

#define GESTURED_WEBUI_DIR "src/webui"

struct WebServer::State {
    // keep port, shared snapshot, server, worker in one owned state
    explicit State(std::uint16_t selected_port) : port(selected_port) {}

    std::uint16_t port;
    std::mutex mutex;
    WebSnapshot latest;
    httplib::Server server;
    std::thread worker;
};

WebServer::WebServer(std::uint16_t port) : state_(std::make_unique<State>(port)) {}

WebServer::~WebServer() {
    // stop the worker before releasing server state
    stop();
}

void WebServer::publish(WebSnapshot snapshot) {
    // protect shared data, move in the latest camera snapshot
    std::lock_guard lock(state_->mutex);
    state_->latest = std::move(snapshot);
}

bool WebServer::start() {
    // do not start a second worker for the same server
    if (state_->worker.joinable()) return false;

    // serve the web interface files from the configured directory
    state_->server.set_mount_point("/", GESTURED_WEBUI_DIR);
    state_->server.Get("/frame.jpg", [this](const httplib::Request&, httplib::Response& response) {
        std::vector<std::uint8_t> jpeg;
        // take a consistent copy, publish may update the snapshot concurrently
        std::lock_guard lock(state_->mutex);
        jpeg = state_->latest.jpeg;

        // report unavailable until the first camera frame arrives
        if (jpeg.empty()) {
            response.status = 503;
            response.set_content("No frame", "text/plain");
            return;
        }

        response.set_content(reinterpret_cast<const char*>(jpeg.data()), jpeg.size(), "image/jpeg");
    });

    // expose current tracking data as a json response
    state_->server.Get("/tracking.json", [this](const httplib::Request&, httplib::Response& response) {
        std::string json;
        // protect the snapshot while copying its tracking data
        std::lock_guard lock(state_->mutex);
        json = state_->latest.tracking_json;
        response.set_content(json, "application/json");
    });

    // bind to loopback, keep the web interface local to this machine
    if (!state_->server.bind_to_port("127.0.0.1", state_->port)) return false;

    // run the blocking request loop on a worker thread
    state_->worker = std::thread([this]{
        state_->server.listen_after_bind();
    });

    return true;
}

void WebServer::stop() {
    // ask the request loop to exit, then wait for its thread to finish
    state_->server.stop();
    
    if (state_->worker.joinable()) state_->worker.join();
}