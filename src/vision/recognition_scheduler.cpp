#include "vision/recognition_scheduler.hpp"

RecognitionScheduler::RecognitionScheduler(
    std::chrono::milliseconds palm_interval,
    std::chrono::milliseconds landmark_interval)
    : palm_interval_(palm_interval),
      landmark_interval_(landmark_interval),
      last_palm_run_(std::chrono::steady_clock::now() - palm_interval),
      last_landmark_run_(std::chrono::steady_clock::now() - landmark_interval) {}

RecognitionSchedule RecognitionScheduler::schedule(
    std::chrono::steady_clock::time_point now) {
    RecognitionSchedule schedule{
        now - last_palm_run_ >= palm_interval_,
        now - last_landmark_run_ >= landmark_interval_
    };

    if (schedule.run_palms) {
        last_palm_run_ = now;
    }
    if (schedule.run_landmarks) {
        last_landmark_run_ = now;
    }
    return schedule;
}
