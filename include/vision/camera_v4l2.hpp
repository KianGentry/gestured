#include <string>
#include <vector>

struct CameraDevice{
    std::string path;
    std::string name;
};

struct CameraFormat {
    std::string pixel_format;
    std::vector<std::string> frame_sizes;
};

std::vector<CameraDevice> discover_cameras();
std::vector<CameraFormat> discover_camera_formats(const CameraDevice& camera);