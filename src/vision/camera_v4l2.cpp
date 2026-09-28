#include "vision/camera_v4l2.hpp"
#include <fcntl.h>
#include <linux/videodev2.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <utility>

namespace
{

std::string fourcc_to_string(__u32 value) {
    char name[5] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
        '\0'
    };

    return name;
}

int open_camera(const CameraDevice& camera) {
    return open(camera.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
}

}; // namespace

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
        const bool capture = queried && (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) && (cap.capabilities & V4L2_CAP_STREAMING);

        if (capture) {
            devices.push_back({path, reinterpret_cast<char*>(cap.card)});
        }

        close(fd);
    }

    return devices;
}

std::vector<CameraFormat> discover_camera_formats(const CameraDevice& camera) {
    std::vector<CameraFormat> formats;
    const int fd = open_camera(camera);

    if (fd < 0) {
        return formats;
    }

    for (unsigned int i = 0; ; ++i) {
        v4l2_fmtdesc desc{};
        desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        desc.index = i;

        if (ioctl(fd, VIDIOC_ENUM_FMT, &desc) != 0) {
            break;
        }

        CameraFormat format;
        format.pixel_format = fourcc_to_string(desc.pixelformat);

        for (unsigned int size_index = 0; ; ++size_index) {
            v4l2_frmsizeenum frmsize{};
            frmsize.pixel_format = desc.pixelformat;
            frmsize.index = size_index;

            if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) < 0) {
                break;
            }

            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                format.frame_sizes.push_back({std::to_string(frmsize.discrete.width) + "x" + std::to_string(frmsize.discrete.height)});
            } else {
                format.frame_sizes.push_back("stepwise");
                break;
            }
        }

        formats.push_back(std::move(format));
    }

    close(fd);
    return formats;
}