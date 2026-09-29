#include "vision/camera_v4l2.hpp"
#include <fcntl.h>
#include <linux/videodev2.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <utility>
#include <sys/mman.h>
#include <poll.h>
#include <cstring>

namespace
{

struct Target {
    uint32_t width;
    uint32_t height;
    uint32_t requested_fps;
    uint32_t minimum_fps;
    uint32_t maximum_fps;
};

constexpr Target targets[] = {
    {1920, 1080, 60, 24, 60},
    {1280, 720, 24, 24, 60}
};

bool set_format(int fd, const Target& target, CameraSettings& settings) {
    // ask the driver for preferred mpjg dimensions
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = target.width;
    format.fmt.pix.height = target.height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0 || 
    format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG ||
    format.fmt.pix.width != target.width ||
    format.fmt.pix.height != target.height) {
        return false;
    }

    // request target frame rate and validate what the driver accepted.
    v4l2_streamparm stream{};
    stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    stream.parm.capture.timeperframe.numerator = 1;
    stream.parm.capture.timeperframe.denominator = target.requested_fps;

    if (ioctl(fd, VIDIOC_S_PARM, &stream) < 0) {
        return false;
    }

    const auto& interval = stream.parm.capture.timeperframe;

    if (interval.numerator == 0) {
        return false;
    }

    const uint32_t actual_fps = interval.denominator / interval.numerator;

    if (actual_fps < target.minimum_fps || actual_fps > target.maximum_fps) {
        return false;
    }

    settings = {
        format.fmt.pix.width,
        format.fmt.pix.height,
        actual_fps
    };

    return true;
}

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

Camera::Camera(const CameraDevice& device)
    : fd_(open(device.path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC)) {}

Camera::~Camera() {
    release_buffers();

    if (fd_ >= 0) {
        close(fd_);
    }
}

bool Camera::configure() {
    if (fd_ < 0) {
        return false;
    }

    for (const auto& target : targets) {
        if (set_format(fd_, target, settings_)) {
            return prepare_buffers();
        }
    }
    return false;
}

const CameraSettings& Camera::settings() const {
    return settings_;
}

bool Camera::prepare_buffers() {
    v4l2_requestbuffers request{};
    request.count = 2; // number of buffers
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &request) < 0 || request.count == 0) {
        return false;
    }

    buffers_.reserve(request.count);

    for (std::size_t i = 0; i < request.count; ++i) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
            release_buffers();
            return false;
        }

        void* address = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buffer.m.offset);

        if (address == MAP_FAILED) {
            release_buffers();
            return false;
        }

        buffers_.push_back({address, buffer.length});
    }

    return true;
}

void Camera::release_buffers() {
    for (auto& buffer : buffers_) {
        munmap(buffer.address, buffer.length);
    }

    buffers_.clear();
}

bool Camera::capture_frame(std::vector<uint8_t>& frame) {
    for (unsigned int i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (ioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        return false;
    }

    pollfd descriptor(fd_, POLLIN, 0);

    if (poll(&descriptor, 1, 1000) < 0) {
        ioctl(fd_, VIDIOC_STREAMOFF, &type);
        return false;
    }

    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    const bool captured = ioctl(fd_, VIDIOC_DQBUF, &buffer) == 0 && buffer.index < buffers_.size();

    if (captured) {
        const auto* source = static_cast<const uint8_t*>(buffers_[buffer.index].address);

        frame.assign(source, source + buffer.bytesused);
        ioctl(fd_, VIDIOC_QBUF, &buffer);
    }

    ioctl(fd_, VIDIOC_STREAMOFF, &type);
    return captured;
}

std::vector<CameraDevice> discover_cameras() {
    std::vector<CameraDevice> devices;

    // video devices numbered by kernel and might have som gaps
    for (int i = 0; i < 64; ++i) {
        const std::string path = "/dev/video" + std::to_string(i);
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);

        if (fd < 0) {
            continue;
        }

        v4l2_capability cap{};
        const bool queried = ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
        const bool capture =
            queried &&
            (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
            (cap.capabilities & V4L2_CAP_STREAMING);

        if (capture) {
            devices.push_back({path, reinterpret_cast<const char*>(cap.card)});
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