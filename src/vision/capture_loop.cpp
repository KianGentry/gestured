#include "vision/capture_loop.hpp"

#include <iostream>
#include <utility>
#include <vector>

void capture_frames(
    Camera& camera,
    WebServer& web_server,
    FrameMailbox& frame_mailbox,
    std::atomic<bool>& running) {
    std::vector<std::uint8_t> captured_frame;

    while (running.load()) {
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
