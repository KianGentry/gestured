#include "vision/camera_v4l2.hpp"
#include <fcntl.h>
#include <linux/videodev2.h>
#include <unistd.h>
#include <sys/ioctl.h>

std::vector<CameraDevice> discover_cameras() {
    std::vector<CameraDevice> devices;

    for (int i = 0; i < 64; ++i) {
        const std::string path = "/dev/video" + std::to_string(i);
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);

        if (fd < 0) {
            continue;
        }

        v4l2_capability cap{};
        const bool queried = ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
        const bool capture = queried && (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE);

        if (capture) {
            devices.push_back({path, reinterpret_cast<char*>(cap.card)});
        }

        close(fd);
    }
    
    return devices;
}