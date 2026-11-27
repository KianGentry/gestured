#include "vision/capture_loop.hpp"

#include <iostream>
#include <utility>
#include <vector>

void capture_frames(Camera& camera, WebServer& web_server, FrameMailbox& frame_mailbox, CameraControls& controls, std::atomic<bool>& running) {
    std::vector<std::uint8_t> captured_frame;
    int applied_exposure = -1;
    int applied_gain = -1;

    while (running.load()) {
        const int requested_exposure = controls.exposure.load();
        if (requested_exposure != applied_exposure) {
            if (camera.set_manual_exposure(requested_exposure)) {
                applied_exposure = requested_exposure;
                std::cout << "[camera] requested exposure=" << requested_exposure << std::endl;
            } else {
                std::cerr << "Failed to set manual exposure" << std::endl;
                applied_exposure = requested_exposure;
            }
        }

        const int requested_gain = controls.gain.load();
        if (requested_gain != applied_gain) {
            if (camera.set_gain(requested_gain)) {
                applied_gain = requested_gain;
                std::cout << "[camera] requested gain=" << requested_gain << std::endl;
            } else {
                std::cerr << "Failed to set gain" << std::endl;
                applied_gain = requested_gain;
            }
        }

        int actual_exposure = 0;
        int actual_gain = 0;
        if (camera.get_controls(actual_exposure, actual_gain)) {
            if (actual_exposure != controls.exposure.load()) {
                std::cout << "[camera] actual exposure=" << actual_exposure << std::endl;
                controls.exposure.store(actual_exposure);
                applied_exposure = actual_exposure;
            }
            if (actual_gain != controls.gain.load()) {
                std::cout << "[camera] actual gain=" << actual_gain << std::endl;
                controls.gain.store(actual_gain);
                applied_gain = actual_gain;
            }
        }

        if (!camera.capture_frame(captured_frame)) {
            if (running.load()) {
                std::cerr << "Camera capture failed" << std::endl;
            }
            break;
        }

        frame_mailbox.publish_frame(captured_frame);
        web_server.publish_frame(std::move(captured_frame));
    }

    frame_mailbox.mark_capture_done();
}
