cmake_minimum_required(VERSION 3.20)

project(gestured
    VERSION 0.1.0
    LANGUAGES CXX
)

add_executable(gestured
    src/main.cpp
)

target_compile_features(gestured PRIVATE cxx_std_20)
set_target_properties(gestured PROPERTIES
    CXX_EXTENSIONS OFF
)