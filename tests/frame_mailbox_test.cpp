#include "vision/frame_mailbox.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

void requirePublishedFramesAreReturned() {
    FrameMailbox mailbox;
    mailbox.publish_frame({1, 2, 3});

    std::vector<std::uint8_t> frame;
    std::uint64_t processed_sequence = 0;

    require(mailbox.wait_for_new_frame(frame, processed_sequence), "published frame should be available");
    require(frame == (std::vector<std::uint8_t>{1, 2, 3}), "published frame should be unchanged");
    require(processed_sequence == 1, "processed frame sequence should be one");
}

void requireLatestFrameReplacesOlderFrames() {
    FrameMailbox mailbox;
    mailbox.publish_frame({1});
    mailbox.publish_frame({2});

    std::vector<std::uint8_t> frame;
    std::uint64_t processed_sequence = 0;

    require(mailbox.wait_for_new_frame(frame, processed_sequence), "latest frame should be available");
    require(frame == (std::vector<std::uint8_t>{2}), "latest frame should replace the older frame");
    require(processed_sequence == 2, "latest frame sequence should be two");
}

void requireCaptureCompletionStopsWaiting() {
    FrameMailbox mailbox;
    mailbox.mark_capture_done();

    std::vector<std::uint8_t> frame;
    std::uint64_t processed_sequence = 0;

    require(!mailbox.wait_for_new_frame(frame, processed_sequence), "capture completion should stop waiting");
}

void requireShutdownStopsWaiting() {
    FrameMailbox mailbox;
    mailbox.stop();

    std::vector<std::uint8_t> frame;
    std::uint64_t processed_sequence = 0;

    require(!mailbox.wait_for_new_frame(frame, processed_sequence), "shutdown should stop waiting");
}

int main() {
    requirePublishedFramesAreReturned();
    requireLatestFrameReplacesOlderFrames();
    requireCaptureCompletionStopsWaiting();
    requireShutdownStopsWaiting();
    return 0;
}
