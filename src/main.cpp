#include <chrono>
#include <csignal>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "vision/camera_v4l2.hpp"
#include "vision/mjpeg_decoder.hpp"
#include "vision/onnx_tracker.hpp"
#include "web/web_server.hpp"
#include <sstream>
#include <algorithm>
#include <array>
#include <cmath>

namespace 
{

// shared stop flag, safe to update from a signal handler
std::atomic<bool> running{true};

// request an orderly exit from the capture loop
void handle_signal(int) {
    running.store(false);
}

} // namespace

using Polygon = std::array<std::array<float, 2>, 4>;

// build a frame relative square, rotate its corners around the palm centre
Polygon palm_polygon(const PalmDetection& palm, uint32_t width, uint32_t height, float angle) {
    // scale the detected size against the longer frame dimension
    const float half = palm.size * std::max(width,height) * 0.5f;
    const float cx = palm.center_x * width;
    const float cy = palm.center_y * height;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    // unit square corners, scaled and rotated below
    const std::array<std::array<float, 2>, 4> corners = {{
        {-1.0f, -1.0f}, {1.0f, -1.0f},
        {1.0f, 1.0f}, {-1.0f, 1.0f}
    }}; 

    Polygon result;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const float x = corners[i][0] * half;
        const float y = corners[i][1] * half;
        // rotate around the palm centre, normalise for the frame
        result[i] = {(cx + c * x - s * y) / width,
        (s * x + c * y + cy) / height};
    }

    return result;
}


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

    // keep shared thresholds alive, tracker and web server hold references
    TrackerConfidence confidence;
    // load the palm detector, load the landmark model
    OnnxTracker tracker(
        "models/palm_detection_full_inf_post_192x192.onnx",
        "models/hand_landmark_sparse_Nx3x224x224.onnx",
        confidence);

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

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // set manual exposure, reduce motion blur
    if (!camera.set_manual_exposure(200)) {
        std::cerr << "Failed to set manual exposure" << std::endl;
        return 1;
    }

    // set manual gain, brighten the image
    if (!camera.set_gain(63)) {
        std::cerr << "Failed to set gain" << std::endl;
        return 1;
    }

    // queue camera buffers, start continuous capture
    if (!camera.start_streaming()) {
        std::cerr << "Failed to start camera stream" << std::endl;
        return 1;
    }

    // serve the web ui, expose frame and tracking routes
    WebServer web_server(confidence, 2026);
    if (!web_server.start()) {
        std::cerr << "Failed to start web server on http://127.0.0.1:2026" << std::endl;
        return 1;
    }
    std::cout << "Web server started on http://127.0.0.1:2026" << std::endl;

    std::vector<uint8_t> rgb;
    std::mutex frame_mutex;
    std::condition_variable frame_ready;
    std::vector<uint8_t> latest_frame;
    std::uint64_t latest_sequence = 0;
    bool capture_done = false;

    std::thread capture_thread([&] {
        std::vector<uint8_t> captured_frame;
        while (running.load()) {
            if (!camera.capture_frame(captured_frame)) {
                if (running.load()) std::cerr << "Camera capture failed" << std::endl;
                break;
            }
            {
                std::lock_guard lock(frame_mutex);
                latest_frame = captured_frame;
                ++latest_sequence;
            }
            web_server.publish_frame(std::move(captured_frame));
            frame_ready.notify_one();
        }
        {
            std::lock_guard lock(frame_mutex);
            capture_done = true;
        }
        frame_ready.notify_all();
    });

    using Clock = std::chrono::steady_clock;
    constexpr auto palm_interval = std::chrono::milliseconds(75); // 16 hz
    constexpr auto landmark_interval = std::chrono::milliseconds(50); // 20 hz
    auto last_palm_run = Clock::now() - palm_interval;
    auto last_landmark_run = Clock::now() - landmark_interval;
    auto report_time = Clock::now();
    std::uint64_t palm_runs = 0;
    std::uint64_t landmark_runs = 0;
    std::vector<PalmDetection> palms;
    std::vector<HandLandmarkResult> hands;

    // infer at bounded rates on the newest captured frame, skipping stale frames
    std::uint64_t processed_sequence = 0;
    while(running.load()) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock lock(frame_mutex);
            frame_ready.wait(lock, [&] {
                return latest_sequence != processed_sequence || capture_done || !running.load();
            });
            if (!running.load() ||
                (capture_done && latest_sequence == processed_sequence)) break;
            frame = latest_frame;
            processed_sequence = latest_sequence;
        }

        const auto now = Clock::now();
        const bool run_palm = now - last_palm_run >= palm_interval;
        const bool run_landmark = now - last_landmark_run >= landmark_interval;
        if (!run_palm && !run_landmark) continue;

        uint32_t width = 0;
        uint32_t height = 0;

        if (!decode_mjpeg(frame, rgb, width, height)) {
            std::cerr << "Failed to decode camera frame" << std::endl;
            continue;
        }

        if (run_palm) {
            palms = tracker.detect_palms(rgb, width, height);
            last_palm_run = now;
            ++palm_runs;
            if (palms.empty()) hands.clear();
        }

        if (run_landmark) {
            if (palms.empty()) {
                hands.clear();
            } else {
                hands = tracker.detect_landmarks(rgb, width, height, palms);
                ++landmark_runs;
            }
            last_landmark_run = now;
        }

        std::ostringstream tracking;

        // serialise axis aligned and rotated palm outlines
        tracking << "{\"palms\":[";
        for (std::size_t i = 0; i < palms.size(); ++i) {
            if (i) tracking << ",";
            const auto box = palm_polygon(palms[i], width, height, 0.0f);
            const auto rotated = palm_polygon(palms[i], width, height, palms[i].rotation);

            const auto write_polygon = [&tracking](const Polygon& points) {
                tracking << "[";
                for (std::size_t j = 0; j < points.size(); ++j) {
                    if (j) tracking << ",";
                    tracking << "[" << points[j][0] << "," << points[j][1] << "]";
                }
                tracking << "]";
            };
            tracking << "{\"box\":";
            write_polygon(box);
            tracking << ",\"rotated_box\":";
            write_polygon(rotated);
            tracking << "}";
        }

        // serialise frame relative hand landmark coordinates
        tracking << "],\"hands\":[";
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
        web_server.publish_tracking(tracking.str());
/*
        if (now - report_time >= std::chrono::seconds(1)) {
            const double seconds = std::chrono::duration<double>(now - report_time).count();
            std::cout << "Inference rates: palms " << palm_runs / seconds << " Hz, landmarks " << landmark_runs / seconds << " Hz" << std::endl;
            palm_runs = 0;
            landmark_runs = 0;
            report_time = now;
        }
*/
    }

    running.store(false);
    frame_ready.notify_all();
    capture_thread.join();

    return 0;
}