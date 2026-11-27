#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

struct CameraDevice{
    std::string path;
    std::string name;
};

struct CameraFormat {
    std::string pixel_format;
    std::vector<std::string> frame_sizes;
};

struct CameraSettings {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
};

struct CameraControls {
    std::atomic<int> exposure{200};
    std::atomic<int> gain{63};
};

struct CameraControlRange {
    int minimum;
    int maximum;
    int default_value;
};

struct CameraControlRanges {
    CameraControlRange exposure;
    CameraControlRange gain;
};

struct CameraBuffer {
    void* address;
    std::size_t length;
};

class Camera {
public:
    explicit Camera(const CameraDevice& device);
    ~Camera(); // destructor

    Camera(const Camera&) = delete; // copy constructor deleted
    Camera& operator=(const Camera&) = delete; // copy assignment operator deleted

    bool configure();
    const CameraSettings& settings() const;

    bool capture_frame(std::vector<uint8_t>& frame);
    bool start_streaming();
    void stop_streaming();

    bool set_manual_exposure(int exposure_100us);
    bool set_gain(int gain);
    bool get_control_ranges(CameraControlRanges& ranges);
    bool get_controls(int& exposure, int& gain);

private:
    bool streaming_ = false;
    bool prepare_buffers();
    void release_buffers();

    int fd_ = -1;
    CameraSettings settings_{};
    std::vector<CameraBuffer> buffers_;
};

std::vector<CameraDevice> discover_cameras();
std::vector<CameraFormat> discover_camera_formats(const CameraDevice& camera);