#include "vision/recognition_scheduler.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

void requireIntervalsRunIndependently() {
    RecognitionScheduler scheduler(
        std::chrono::milliseconds(10),
        std::chrono::milliseconds(30));

    const auto start = std::chrono::steady_clock::now();
    const auto first = scheduler.schedule(start);
    require(first.run_palms, "first schedule should run palms");
    require(first.run_landmarks, "first schedule should run landmarks");

    const auto tooSoon = scheduler.schedule(start + std::chrono::milliseconds(5));
    require(!tooSoon.run_palms, "palm should not run before its interval");
    require(!tooSoon.run_landmarks, "landmark should not run before its interval");

    const auto palmTime = scheduler.schedule(start + std::chrono::milliseconds(11));
    require(palmTime.run_palms, "palm should run after ten milliseconds");
    require(!palmTime.run_landmarks, "landmark should wait for thirty milliseconds");

    const auto landmarkTime = scheduler.schedule(start + std::chrono::milliseconds(31));
    require(landmarkTime.run_palms, "palm should run again after its interval");
    require(landmarkTime.run_landmarks, "landmark should run after thirty milliseconds");
}

void requireImmediatelyRepeatedSchedulesWait() {
    RecognitionScheduler scheduler(
        std::chrono::milliseconds(10),
        std::chrono::milliseconds(30));

    const auto start = std::chrono::steady_clock::now();
    scheduler.schedule(start);

    const auto repeated = scheduler.schedule(start);
    require(!repeated.run_palms, "palm should wait after a scheduled run");
    require(!repeated.run_landmarks, "landmark should wait after a scheduled run");
}

int main() {
    requireIntervalsRunIndependently();
    requireImmediatelyRepeatedSchedulesWait();
    return 0;
}
