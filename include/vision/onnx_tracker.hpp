#include <array>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

struct PalmDetection {
    float size;
    float rotation;
    float center_x;
    float center_y;
};

struct HandLandmarkResult {
    std::array<float, 63> xyz;
    float score;
    float right_hand;
};

class OnnxTracker {
public:
    OnnxTracker(std::string palm_model_path, std::string landmark_model_path);
    ~OnnxTracker();

    bool initialise();
    const std::string& input_description() const;
    bool run_test();
    std::vector<PalmDetection> detect_palms(
        const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height);
    std::vector<HandLandmarkResult> detect_landmarks(
        const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height,
        const std::vector<PalmDetection>& palms);

private:
    std::string palm_model_path_;
    std::string landmark_model_path_;
    std::string input_description_;
    struct State;
    std::unique_ptr<State> state_;
};