cmake_minimum_required(VERSION 3.20)

project(gestured
    VERSION 0.1.0
    LANGUAGES CXX
)

add_executable(gestured
    src/main.cpp
    src/vision/camera_v4l2.cpp
    src/vision/mjpeg_decoder.cpp
    src/vision/onnx_tracker.cpp
    src/web/web_server.cpp
)

target_include_directories(gestured PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(JPEG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

target_link_libraries(gestured PRIVATE JPEG::JPEG onnxruntime::onnxruntime)

target_compile_definitions(gestured PRIVATE GESTURED_WEBUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/webui")
target_compile_features(gestured PRIVATE cxx_std_20)
set_target_properties(gestured PROPERTIES
    CXX_EXTENSIONS OFF
)