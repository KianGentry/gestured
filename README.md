# gestured

A daemon for mapping hand gestures to system commands / macros.

The web UI is hosted at `http://127.0.0.1:2026` and streams the MJPEG output of the camera.

## Get the source

```sh
git clone https://github.com/KianGentry/gestured.git
cd gestured
```

## Prerequisites

- Linux with a V4L2-compatible camera
- CMake 3.20 or newer, a C++20 compiler, and Make or Ninja
- JPEG development files and ONNX Runtime development files

On openSUSE, install the build tools and libraries with:

```sh
sudo zypper install cmake gcc-c++ make libjpeg8-devel onnxruntime-devel
```

## Build

From the repository root, configure and compile a Debug build:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
```

For an optimized Release build, use a separate build directory:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
```

## Run

Run from the repository root so the application can find its models:

```sh
./build/debug/gestured
```

Or run the Release build:

```sh
./build/release/gestured
```

Run one build at a time; the application uses the camera exclusively. The camera must be connected and accessible to your user. Once started, open `http://127.0.0.1:2026`. Press Ctrl-C to stop the application. If no camera is available, startup exits with `No cameras found`.

`systmd` integration is coming soon.

---

See LICENSE (BSD 2-Clause) for copyright information.