#include <string>
#include <vector>
#include <cstdint>

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

class Camera {
public:
    explicit Camera(const CameraDevice& device);
    ~Camera(); // destructor

    Camera(const Camera&) = delete; // copy constructor deleted
    Camera& operator=(const Camera&) = delete; // copy assignment operator deleted

    bool configure();
    const CameraSettings& settings() const;

private:
    int fd_ = -1;
    CameraSettings settings_{};
};

std::vector<CameraDevice> discover_cameras();
std::vector<CameraFormat> discover_camera_formats(const CameraDevice& camera);