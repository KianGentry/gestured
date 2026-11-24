#pragma once

#include <chrono>

struct RecognitionSchedule {
    bool run_palms;
    bool run_landmarks;
};

class RecognitionScheduler {
public:
    RecognitionScheduler(std::chrono::milliseconds palm_interval, std::chrono::milliseconds landmark_interval);

    RecognitionSchedule schedule(std::chrono::steady_clock::time_point now);

private:
    std::chrono::milliseconds palm_interval_;
    std::chrono::milliseconds landmark_interval_;
    std::chrono::steady_clock::time_point last_palm_run_;
    std::chrono::steady_clock::time_point last_landmark_run_;
};
