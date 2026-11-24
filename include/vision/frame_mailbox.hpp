#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

class FrameMailbox {
public:
    void publish_frame(std::vector<std::uint8_t> frame);
    bool wait_for_new_frame(
        std::vector<std::uint8_t>& frame,
        std::uint64_t& processed_sequence);
    void mark_capture_done();
    void stop();

private:
    std::mutex mutex_;
    std::condition_variable frame_ready_;
    std::vector<std::uint8_t> latest_frame_;
    std::uint64_t latest_sequence_ = 0;
    bool capture_done_ = false;
    bool stopping_ = false;
};
