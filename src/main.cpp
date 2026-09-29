#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <vector>
#include "vision/camera_v4l2.hpp"
#include "vision/mjpeg_decoder.hpp"

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

    // camera config
    Camera camera(cameras.front());

    if (!camera.configure()) {
        std::cerr << "Failed to configure camera" << std::endl;
        return 1;
    }

    const auto& settings = camera.settings();

    std::cout << "Using " << settings.width << "x" 
    << settings.height << " at " << settings.fps << "fps" << std::endl;

    // capture frame
    std::vector<uint8_t> frame;
    if (!camera.capture_frame(frame)) {
        std::cerr << "Failed to capture frame" << std::endl;
        return 1;
    }

    std::cout << "Captured frame, size " << frame.size() << "B" << std::endl;

    // decode frame
    std::vector<uint8_t> rgb;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!decode_mjpeg(frame, rgb, width, height)) {
        std::cerr << "Failed to decode MJPEG frame" << std::endl;
        return 1;
    }

    std::cout << "Decoded frame" << width << "x" << height << ", size " << rgb.size() << "B" << std::endl;
/*
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
*/
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    while(running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}