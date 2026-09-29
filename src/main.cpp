#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "vision/camera_v4l2.hpp"

namespace 
{

volatile std::sig_atomic_t running = 1;

void handle_signal(int) {
    running = 0;
}

} // namespace

int main() {
    const auto cameras = discover_cameras();

    if (cameras.empty()) {
        std::cout << "No cameras found" << std::endl;
        return 1;
    }

    // configure the first compatible camera before loop
    Camera camera(cameras.front());

    if (!camera.configure()) {
        std::cerr << "Failed to configure camera" << std::endl;
        return 1;
    }

    const auto& settings = camera.settings();

    std::cout << "Using " << settings.width << "x" 
    << settings.height << " at " << settings.fps << "fps" << std::endl;

    for (const auto& device : cameras) {
        std::cout << device.path << ": " << device.name << std::endl;

        for (const auto& format : discover_camera_formats(device)) {
            std::cout << "  " << format.pixel_format << std::endl;

            for (const auto& size : format.frame_sizes) {
                std::cout << "    " << size << std::endl;
            }
            
            std::cout << std::endl;
        }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    while(running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}