#include <chrono>
#include <csignal>
#include <thread>
#include "vision/camera_v4l2.hpp"
#include <iostream>

namespace 
{
volatile std::sig_atomic_t running = 1;

void handle_signal(int) {
    running = 0;
}
} // namespace

int main() {
    const auto cameras = discover_cameras();

    for (const auto& camera : cameras) {
        std::cout << camera.path << ": " << camera.name << std::endl;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    while(running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}