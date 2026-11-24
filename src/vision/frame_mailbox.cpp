#include "vision/frame_mailbox.hpp"

#include <utility>

void FrameMailbox::publish_frame(std::vector<std::uint8_t> frame) {
    {
        std::lock_guard lock(mutex_);
        latest_frame_ = std::move(frame);
        ++latest_sequence_;
    }
    frame_ready_.notify_one();
}

bool FrameMailbox::wait_for_new_frame(
    std::vector<std::uint8_t>& frame,
    std::uint64_t& processed_sequence) {
    std::unique_lock lock(mutex_);
    frame_ready_.wait(lock, [&] {
        return latest_sequence_ != processed_sequence ||
            capture_done_ ||
            stopping_;
    });

    if (stopping_) {
        return false;
    }
    if (capture_done_ && latest_sequence_ == processed_sequence) {
        return false;
    }

    frame = latest_frame_;
    processed_sequence = latest_sequence_;
    return true;
}

void FrameMailbox::mark_capture_done() {
    {
        std::lock_guard lock(mutex_);
        capture_done_ = true;
    }
    frame_ready_.notify_all();
}

void FrameMailbox::stop() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    frame_ready_.notify_all();
}
