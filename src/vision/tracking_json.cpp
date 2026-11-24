#include "vision/tracking_json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

using PalmPolygon = std::array<std::array<float, 2>, 4>;

namespace {

PalmPolygon make_palm_polygon(
    const PalmDetection& palm,
    std::uint32_t width,
    std::uint32_t height,
    float angle) {
    const float half = palm.size * std::max(width, height) * 0.5f;
    const float center_x = palm.center_x * width;
    const float center_y = palm.center_y * height;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);

    const std::array<std::array<float, 2>, 4> corners = {{
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f}
    }};

    PalmPolygon result;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const float x = corners[i][0] * half;
        const float y = corners[i][1] * half;
        result[i] = {
            (center_x + cosine * x - sine * y) / width,
            (sine * x + cosine * y + center_y) / height
        };
    }
    return result;
}

void append_polygon(std::ostringstream& json, const PalmPolygon& points) {
    json << "[";
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i) {
            json << ",";
        }
        json << "[" << points[i][0] << "," << points[i][1] << "]";
    }
    json << "]";
}

void append_palm(
    std::ostringstream& json,
    const PalmDetection& palm,
    std::uint32_t width,
    std::uint32_t height) {
    const auto box = make_palm_polygon(palm, width, height, 0.0f);
    const auto rotated_box =
        make_palm_polygon(palm, width, height, palm.rotation);

    json << "{\"box\":";
    append_polygon(json, box);
    json << ",\"rotated_box\":";
    append_polygon(json, rotated_box);
    json << "}";
}

void append_hand(
    std::ostringstream& json,
    const HandLandmarkResult& hand) {
    json << "{\"landmarks\":[";
    for (std::size_t point = 0; point < 21; ++point) {
        if (point) {
            json << ",";
        }
        json << "["
             << hand.frame_xy[point * 2]
             << ","
             << hand.frame_xy[point * 2 + 1]
             << "]";
    }
    json << "]}";
}

} // namespace

std::string make_tracking_json(
    const std::vector<PalmDetection>& palms,
    const std::vector<HandLandmarkResult>& hands,
    std::uint32_t width,
    std::uint32_t height) {
    std::ostringstream json;

    json << "{\"palms\":[";
    for (std::size_t i = 0; i < palms.size(); ++i) {
        if (i) {
            json << ",";
        }
        append_palm(json, palms[i], width, height);
    }

    json << "],\"hands\":[";
    for (std::size_t i = 0; i < hands.size(); ++i) {
        if (i) {
            json << ",";
        }
        append_hand(json, hands[i]);
    }
    json << "]}";

    return json.str();
}
