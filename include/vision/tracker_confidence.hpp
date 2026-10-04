#pragma once

#include <atomic>

struct TrackerConfidence {
    std::atomic<float> palm{0.4f};
    std::atomic<float> landmark{0.4f};
};