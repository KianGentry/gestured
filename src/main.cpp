#include <chrono>
#include <csignal>
#include <atomic>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>
#include "vision/capture_loop.hpp"
#include "vision/camera_v4l2.hpp"
#include "vision/frame_mailbox.hpp"
#include "vision/mjpeg_decoder.hpp"
#include "vision/onnx_tracker.hpp"
#include "vision/recognition_scheduler.hpp"
#include "vision/tracking_json.hpp"
#include "web/web_server.hpp"

namespace 
{

// shared stop flag, safe to update from a signal handler
std::atomic<bool> running{true};

// request an orderly exit from the capture loop
void handle_signal(int) {
    running.store(false);
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
    FrameMailbox frame_mailbox;
    std::thread capture_thread(
        capture_frames,
        std::ref(camera),
        std::ref(web_server),
        std::ref(frame_mailbox),
        std::ref(running));

    constexpr auto palm_interval = std::chrono::milliseconds(75); // 13 Hz
    constexpr auto landmark_interval = std::chrono::milliseconds(50); // 20 Hz
    RecognitionScheduler scheduler(palm_interval, landmark_interval);
    std::vector<PalmDetection> palms;
    std::vector<HandLandmarkResult> hands;
    // auto last_tracking_log = std::chrono::steady_clock::now();

    // infer at bounded rates on the newest captured frame, skipping stale frames
    std::uint64_t processed_sequence = 0;
    while(running.load()) {
        std::vector<uint8_t> frame;
        if (!frame_mailbox.wait_for_new_frame(frame, processed_sequence)) break;

        const auto schedule =
            scheduler.schedule(std::chrono::steady_clock::now());
        if (!schedule.run_palms && !schedule.run_landmarks) continue;

        uint32_t width = 0;
        uint32_t height = 0;

        if (!decode_mjpeg(frame, rgb, width, height)) {
            std::cerr << "Failed to decode camera frame" << std::endl;
            continue;
        }

        if (schedule.run_palms) {
            palms = tracker.detect_palms(rgb, width, height);
            if (palms.empty()) hands.clear();
        }

        if (schedule.run_landmarks) {
            if (palms.empty()) {
                hands.clear();
            } else {
                hands = tracker.detect_landmarks(rgb, width, height, palms);
            }
        }

        web_server.publish_tracking(make_tracking_json(palms, hands, width, height));

        // const auto now = std::chrono::steady_clock::now();
        // if (now - last_tracking_log >= std::chrono::seconds(1)) {
        //     std::cout << "[tracking] palms=" << palms.size()
        //               << " hands=" << hands.size() << std::endl;
        //     last_tracking_log = now;
        // }
    }

    running.store(false);
    frame_mailbox.stop();
    capture_thread.join();

    return 0;
}