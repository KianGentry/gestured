#include "web/web_server.hpp"
#include "web/httplib.h"
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <utility>

struct WebServer::State {
    // keep port, shared snapshot, server, worker in one owned state
    explicit State(std::uint16_t selected_port) : port(selected_port) {}

    std::uint16_t port;
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

            std::unique_lock lock(mutex);
            updated.wait(lock, [&] {
                return sequence != last_sequence || stopping;
            });
            if (stopping) return;
            jpeg = latest.jpeg;
            last_sequence = sequence;

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

        server.Get("/stream.mjpg",[this](const httplib::Request&, httplib::Response& response) {
            response.set_chunked_content_provider("multipart/x-mixed-replace; boundary=frame",
                [this](size_t, httplib::DataSink& sink) {
                write_stream(sink);
                return false;
            });
        });
    }

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
    ++state_->sequence;
    state_->updated.notify_all();
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
    std::lock_guard lock(state_->mutex);
    state_->stopping = true;
    state_->updated.notify_all();
    // ask the request loop to exit, then wait for its thread to finish
    state_->server.stop();
    
    if (state_->worker.joinable()) state_->worker.join();
}