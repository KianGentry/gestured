#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vision/onnx_tracker.hpp"

std::string make_tracking_json(
    const std::vector<PalmDetection>& palms,
    const std::vector<HandLandmarkResult>& hands,
    std::uint32_t width,
    std::uint32_t height);
