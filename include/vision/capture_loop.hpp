#pragma once

#include <atomic>

#include "vision/camera_v4l2.hpp"
#include "vision/frame_mailbox.hpp"
#include "web/web_server.hpp"

void capture_frames(
    Camera& camera,
    WebServer& web_server,
    FrameMailbox& frame_mailbox,
    CameraControls& controls,
    std::atomic<bool>& running);
