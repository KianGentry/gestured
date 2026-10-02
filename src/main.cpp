#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <vector>
#include "vision/camera_v4l2.hpp"
#include "vision/mjpeg_decoder.hpp"
#include "vision/onnx_tracker.hpp"
#include "web/web_server.hpp"
#include <sstream>

namespace 
{

// shared stop flag, safe to update from a signal handler
volatile std::sig_atomic_t running = 1;

// request an orderly exit from the capture loop
void handle_signal(int) {
    running = 0;
}

} // namespace

int main() {

    // find usable video capture devices before setting up inference
    const auto cameras = discover_cameras();

    if (cameras.empty()) {
        std::cout << "No cameras found" << std::endl;
        return 1;
    }

    // use the first discovered camera, configure its capture mode
    Camera camera(cameras.front());

    if (!camera.configure()) {
        std::cerr << "Failed to configure camera" << std::endl;
        return 1;
    }

    const auto& settings = camera.settings();

    std::cout << "Using " << settings.width << "x" 
    << settings.height << " at " << settings.fps << "fps" << std::endl;

    // load the palm detector, load the landmark model
    OnnxTracker tracker(
        "models/palm_detection_full_inf_post_192x192.onnx",
        "models/hand_landmark_sparse_Nx3x224x224.onnx");

    if (!tracker.initialise()) {
        std::cerr << "Failed to initialise tracker" << std::endl;
        return 1;
    }
    std::cout << tracker.input_description() << std::endl;

    // check both model sessions before starting camera capture
    if (!tracker.run_test()) {
        std::cerr << "Inference test failed" << std::endl;
        return 1;
    }

    std::cout << "onnx inference test passed" << std::endl;
/*
    // capture one frame, useful for a quick camera check
    std::vector<uint8_t> frame;
    if (!camera.capture_frame(frame)) {
        std::cerr << "Failed to capture frame" << std::endl;
        return 1;
    }

    std::cout << "Captured frame, size " << frame.size() << "B" << std::endl;

    // decode compressed camera data into rgb pixels
    std::vector<uint8_t> rgb;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!decode_mjpeg(frame, rgb, width, height)) {
        std::cerr << "Failed to decode MJPEG frame" << std::endl;
        return 1;
    }

    std::cout << "Decoded frame" << width << "x" << height << ", size " << rgb.size() << "B" << std::endl;

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

    // queue camera buffers, start continuous capture
    if (!camera.start_streaming()) {
        std::cerr << "Failed to start camera stream" << std::endl;
        return 1;
    }

    // serve the web ui, expose frame and tracking routes
    WebServer web_server(2026);
    if (!web_server.start()) {
        std::cerr << "Failed to start web server on http://127.0.0.1:2026" << std::endl;
        return 1;
    }
    std::cout << "Web server started on http://127.0.0.1:2026" << std::endl;

    std::vector<uint8_t> frame;
    std::vector<uint8_t> rgb;
    std::uint64_t frame_count = 0;
    std::uint64_t reported_frames = 0;
    std::uint64_t detected_frames = 0;
    std::uint64_t landmark_frames = 0;
    // use a steady clock, elapsed time must not jump with wall clock changes
    auto report_time = std::chrono::steady_clock::now();

    // capture and process frames until a signal requests shutdown
    while(running) {
        uint32_t width = 0;
        uint32_t height = 0;

        // stop on capture or decode failure, report only during normal operation
        if (!camera.capture_frame(frame) || !decode_mjpeg(frame, rgb, width, height)) {
            if (running) {
                std::cerr << "Failed to process frame" << std::endl;
            }
            break;
        }

        const auto palms = tracker.detect_palms(rgb, width, height);
        const auto hands = tracker.detect_landmarks(rgb, width, height, palms);
        if (!palms.empty()) {
            ++detected_frames;
        }
        if (!hands.empty()) {
            ++landmark_frames;
        }



        // publish compressed camera frame, tracking payload currently empty
        WebSnapshot snapshot;
        snapshot.jpeg = frame;

        std::ostringstream tracking;

        tracking << "{\"palms\":[],\"hands\":[";
        for (std::size_t i = 0; i < hands.size(); ++i) {
            if (i) tracking << ",";
            tracking << "{\"landmarks\":[";
            for (std::size_t point = 0; point < 21; ++point) {
                if (point) tracking << ",";
                tracking << "[" << hands[i].frame_xy[point * 2] << "," << hands[i].frame_xy[point * 2 + 1] << "]";
            }
            tracking << "]}";
        }
        tracking << "]}";
        snapshot.tracking_json = tracking.str();

        web_server.publish(std::move(snapshot));
/*
        ++frame_count;
        ++reported_frames;

        // report throughput and detection counts once each second
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - report_time;

        if (elapsed >= std::chrono::seconds(1)) {
            const double seconds = std::chrono::duration<double>(elapsed).count();
            std::cout << "Processed " << frame_count << " frames, " 
            << reported_frames / seconds << " fps, palms in "<< detected_frames << " frames, landmarks in " 
            << landmark_frames << " frames" << std::endl;

            reported_frames = 0;
            detected_frames = 0;
            landmark_frames = 0;
            report_time = now;
        }
    */
    }

    return 0;
}