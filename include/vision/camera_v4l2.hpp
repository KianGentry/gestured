#include <string>
#include <vector>

struct CameraDevice{
    std::string path;
    std::string name;
};

std::vector<CameraDevice> discover_cameras();