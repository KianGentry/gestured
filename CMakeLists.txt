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

function(gestured_map_missing_imported_config target config)
    get_target_property(imported_configs ${target} IMPORTED_CONFIGURATIONS)
    if(imported_configs)
        string(TOUPPER "${config}" config_upper)
        list(FIND imported_configs "${config_upper}" config_index)
        get_target_property(generic_location ${target} IMPORTED_LOCATION)
        if(config_index EQUAL -1 AND NOT generic_location)
            list(GET imported_configs 0 fallback_config)
            set_property(TARGET ${target} PROPERTY
                MAP_IMPORTED_CONFIG_${config_upper} "${fallback_config}"
            )
        endif()
    endif()
endfunction()

# Some distro packages only export optimized library configurations.
gestured_map_missing_imported_config(JPEG::JPEG DEBUG)
gestured_map_missing_imported_config(onnxruntime::onnxruntime DEBUG)
gestured_map_missing_imported_config(onnxruntime::onnxruntime RELEASE)

target_link_libraries(gestured PRIVATE JPEG::JPEG onnxruntime::onnxruntime)

target_compile_definitions(gestured PRIVATE GESTURED_WEBUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/webui")
target_compile_features(gestured PRIVATE cxx_std_20)
set_target_properties(gestured PROPERTIES
    CXX_EXTENSIONS OFF
)