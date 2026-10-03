#include "vision/onnx_tracker.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <onnxruntime_cxx_api.h>
#include <utility>
#include <cstdint>
#include <cmath>
#include <vector>

namespace
{

// readable model type names, startup diagnostics
const char* element_type_name(ONNXTensorElementDataType type) {
    switch(type) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: return "float";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8: return "int8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: return "uint8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: return "int32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32: return "uint32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: return "int64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64: return "uint64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE: return "double";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL: return "bool";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED: return "undefined";
        default: return "unknown";
    }
}

template <typename Model>
bool initialise_model(
    Ort::Env& environment,
    Ort::SessionOptions& options,
    const std::string& path,
    Model& model,
    std::string& description) {
    // onnx allocated input name, copy before allocator cleanup
    Ort::AllocatorWithDefaultOptions allocator;
    model.session = std::make_unique<Ort::Session>(environment, path.c_str(), options);

    // tensor input required, other onnx input kinds unsupported
    const auto type_info = model.session->GetInputTypeInfo(0);
    if (type_info.GetONNXType() != ONNX_TYPE_TENSOR) return false;

    auto input_name = model.session->GetInputNameAllocated(0, allocator);
    const auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
    model.input_name = input_name.get();
    model.input_shape = tensor_info.GetShape();
    model.input_type = tensor_info.GetElementType();

    // report model input contract, before inference
    std::ostringstream output;
    output << "onnx input: " << model.input_name << " [";
    for (std::size_t i = 0; i < model.input_shape.size(); ++i) {
        if (i > 0) output << ",";
        output << model.input_shape[i];
    }
    output << "] " << element_type_name(model.input_type);
    description = output.str();
    return true;
}

template <typename Model>
bool run_model_test(Model& model, const char* label) {
    // zero filled tensor, check input acceptance, session execution
    const auto shape = model.input_shape;
    if (shape.size() != 4 || model.input_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        std::cerr << label << " has unsupported input" << std::endl;
        return false;
    }

    std::size_t elements = 1;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        // dynamic batch dimension only, test batch size one
        if (shape[i] <= 0 && i != 0) {
            std::cerr << label << " has unsupported dynamic dimension" << std::endl;
            return false;
        }
        const auto dimension = shape[i] <= 0 ? 1 : shape[i];
        elements *= static_cast<std::size_t>(dimension);
    }

    const auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<float> data(elements, 0.0f);
    std::vector<int64_t> test_shape = shape;
    // one image in test tensor, declared batch size ignored
    test_shape[0] = 1;
    Ort::Value input = Ort::Value::CreateTensor<float>(
        memory_info, data.data(), data.size(), test_shape.data(), test_shape.size());

    Ort::AllocatorWithDefaultOptions allocator;
    std::vector<Ort::AllocatedStringPtr> output_storage;
    std::vector<const char*> output_names;

    // allocated output names stay alive, run call pointer use
    for (std::size_t i = 0; i < model.session->GetOutputCount(); ++i) {
        output_storage.push_back(model.session->GetOutputNameAllocated(i, allocator));
        output_names.push_back(output_storage.back().get());
        const auto output_info = model.session->GetOutputTypeInfo(i);
        const auto output_shape = output_info.GetTensorTypeAndShapeInfo().GetShape();

        std::cout << label << " output " << i << ": " << output_names.back() << " [";
        for (std::size_t j = 0; j < output_shape.size(); ++j) {
            if (j > 0) std::cout << ",";
            std::cout << output_shape[j];
        }
        std::cout << "]" << std::endl;
    }

    const char* input_names[] = {model.input_name.c_str()};
    const auto outputs = model.session->Run(
        Ort::RunOptions{nullptr}, input_names, &input, 1,
        output_names.data(), output_names.size());
    std::cout << label << " outputs: " << outputs.size() << std::endl;
    return !outputs.empty();
}

std::vector<float> make_palm_input(
    const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height) {
    // palm model input, square 192 pixel image, channel first float format
    constexpr uint32_t size = 192;
    // fit full frame in square, preserve aspect ratio
    const uint32_t scaled_width = width * size / std::max(width, height);
    const uint32_t scaled_height = height * size / std::max(width, height);
    const uint32_t offset_x = (size - scaled_width) / 2;
    const uint32_t offset_y = (size - scaled_height) / 2;
    std::vector<float> input(3 * size * size, 0.0f);

    // centre resized image, black border remains
    for (uint32_t y = 0; y < scaled_height; ++y) {
        for (uint32_t x = 0; x < scaled_width; ++x) {
            const uint32_t source_x = x * width / scaled_width;
            const uint32_t source_y = y * height / scaled_height;
            const auto source = (source_y * width + source_x) * 3;
            // rgb components in separate planes, byte values scaled to 0..1
            const auto target = (offset_y + y) * size + offset_x + x;
            for (uint32_t channel = 0; channel < 3; ++channel) {
                input[channel * size * size + target] = rgb[source + channel] / 255.0f;
            }
        }
    }
    return input;
}

std::vector<float> make_landmark_input(
    const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height,
    const std::vector<PalmDetection>& palms) {
    // one rotated square crop per palm, landmark input size 224 pixels
    constexpr uint32_t size = 224;
    const float image_size = static_cast<float>(std::max(width, height));
    std::vector<float> input(palms.size() * 3 * size * size, 0.0f);

    for (std::size_t batch = 0; batch < palms.size(); ++batch) {
        // detection centre in frame relative coordinates, size relative to longer frame dimension
        const auto& palm = palms[batch];
        const float center_x = palm.center_x * width;
        const float center_y = palm.center_y * height;
        const float half_size = palm.size * image_size / 2.0f;
        const float sine = std::sin(palm.rotation);
        const float cosine = std::cos(palm.rotation);

        // map crop pixels to original frame, rotate around palm
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const float local_x = (static_cast<float>(x) / (size - 1) - 0.5f) * 2.0f * half_size;
                const float local_y = (static_cast<float>(y) / (size - 1) - 0.5f) * 2.0f * half_size;
                const int source_x = static_cast<int>(center_x + cosine * local_x - sine * local_y);
                const int source_y = static_cast<int>(center_y + sine * local_x + cosine * local_y);
                // out of frame pixels remain zero, padded crop
                if (source_x < 0 || source_y < 0 || source_x >= static_cast<int>(width) ||
                    source_y >= static_cast<int>(height)) continue;

                const auto source = (source_y * width + source_x) * 3;
                const auto target = batch * 3 * size * size + y * size + x;
                // three channel first planes per crop, onnx runtime layout
                for (uint32_t channel = 0; channel < 3; ++channel) {
                    input[batch * 3 * size * size + channel * size * size + y * size + x] =
                        rgb[source + channel] / 255.0f;
                }
            }
        }
    }
    return input;
}

} // namespace

struct OnnxTracker::State {
    // each model, own session, input details for inference
    struct Model {
        std::unique_ptr<Ort::Session> session;
        std::string input_name;
        std::vector<int64_t> input_shape;
        ONNXTensorElementDataType input_type =
            ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    } palm, landmark;

    Ort::Env environment;
    Ort::SessionOptions options;

    // one runtime environment, both model sessions
    State() : environment(ORT_LOGGING_LEVEL_WARNING, "gestured") {}
};

// runtime state private to implementation
OnnxTracker::~OnnxTracker() = default;

// model paths, onnx session creation during initialise
OnnxTracker::OnnxTracker(std::string palm_model_path, std::string landmark_model_path) 
: palm_model_path_(std::move(palm_model_path)), landmark_model_path_(std::move(landmark_model_path)), state_(std::make_unique<State>()) {}

bool OnnxTracker::initialise() {
    try {
        // single thread inference, predictable execution, small inputs
        state_->options.SetIntraOpNumThreads(1);
        state_->options.SetInterOpNumThreads(1);
        state_->options.DisableCpuMemArena();
        state_->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        std::string palm_description;
        std::string landmark_description;
        // both model stages required, tracker ready state
        if (!initialise_model(state_->environment, state_->options, palm_model_path_, state_->palm, palm_description) ||
        !initialise_model(state_->environment, state_->options, landmark_model_path_, state_->landmark, landmark_description)) {
            std::cerr << "onnx input is not a tensor" << std::endl;
            return false;
        }
        input_description_ = palm_description + "; " + landmark_description;

        return true;
        
    } catch (const Ort::Exception& error) {
        std::cerr << "onnx init failed: " << error.what() << std::endl;
        state_->palm.session.reset();
        state_->landmark.session.reset();
        return false;
    }
}

const std::string& OnnxTracker::input_description() const {
    // model input contract, discovered during initialisation, available to callers
    return input_description_;
}

bool OnnxTracker::run_test() {
    try {
        // test both sessions at startup, catch invalid models before camera tracking
        return run_model_test(state_->palm, "palm") && run_model_test(state_->landmark, "landmark");
    } catch (const Ort::Exception& error) {
        std::cerr << "onnx run test failed: " << error.what() << std::endl;
        return false;
    }
}

std::vector<PalmDetection> OnnxTracker::detect_palms(
    const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height) {
    std::vector<PalmDetection> detections;
    // reject incomplete frames, palm model not ready
    if (rgb.size() != static_cast<std::size_t>(width) * height * 3 ||
        !state_->palm.session) return detections;

    // prepare frame input, request combined score, box, keypoint output
    const auto input = make_palm_input(rgb, width, height);
    const auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::vector<int64_t> shape = {1, 3, 192, 192};
    Ort::Value tensor = Ort::Value::CreateTensor<float>(
        memory_info, const_cast<float*>(input.data()), input.size(), shape.data(), shape.size());
    const char* input_names[] = {state_->palm.input_name.c_str()};
    const char* output_name = "pdscore_boxx_boxy_boxsize_kp0x_kp0y_kp2x_kp2y";

    try {
        const auto outputs = state_->palm.session->Run(
            Ort::RunOptions{nullptr}, input_names, &tensor, 1, &output_name, 1);
        const auto output_info = outputs[0].GetTensorTypeAndShapeInfo();
        const auto output_shape = output_info.GetShape();
        const float* values = outputs[0].GetTensorData<float>();
        // predictions use padded square coordinates, undo padding mapping
        const float square_size = static_cast<float>(std::max(width, height));
        const float padding = static_cast<float>(std::abs(static_cast<int>(width) - static_cast<int>(height))) / 2.0f;

        for (int64_t i = 0; i < output_shape[0]; ++i) {
            // prediction fields, score, box centre, size, two keypoints
            const float* box = values + i * 8;
            // discard weak predictions, invalid box sizes
            // 0.5f is confidence value for palm detection
            if (box[0] <= 0.5f || box[3] <= 0.0f) continue;
            // two keypoints define hand orientation, image axes
            const float angle = 0.5f * static_cast<float>(M_PI) -
                std::atan2(-(box[7] - box[5]), box[6] - box[4]);
            const float center_x = box[1] + 0.5f * box[3] * std::sin(angle);
            const float center_y = box[2] - 0.5f * box[3] * std::cos(angle);
            detections.push_back({
                // expand wrist box, convert vertical centre to height relative frame coordinates
                2.9f * box[3], angle, center_x,
                (center_y * square_size - padding) / static_cast<float>(height)});
        }
    } catch (const Ort::Exception& error) {
        std::cerr << "palm detection failed: " << error.what() << std::endl;
    }
    return detections;
}

std::vector<HandLandmarkResult> OnnxTracker::detect_landmarks(
const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height,
const std::vector<PalmDetection>& palms) {
    std::vector<HandLandmarkResult> results;
    // skip landmark inference, no palms, invalid frame, unloaded model
    if (palms.empty() || rgb.size() != static_cast<std::size_t>(width) * height * 3 ||
        !state_->landmark.session) return results;

    // batched landmark inference, one rotated crop per palm
    auto input = make_landmark_input(rgb, width, height, palms);
    const auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::vector<int64_t> shape = {static_cast<int64_t>(palms.size()), 3, 224, 224};

    Ort::Value tensor = Ort::Value::CreateTensor<float>(
        memory_info, input.data(), input.size(), shape.data(), shape.size());

    try {
        Ort::AllocatorWithDefaultOptions allocator;
        std::vector<Ort::AllocatedStringPtr> output_storage;
        std::vector<const char*> output_names;

        // output names vary by model, retain allocated names for inference
        for (std::size_t i = 0; i < state_->landmark.session->GetOutputCount(); ++i) {
            output_storage.push_back(state_->landmark.session->GetOutputNameAllocated(i, allocator));
            output_names.push_back(output_storage.back().get());
        }

        const char* input_names[] = {state_->landmark.input_name.c_str()};
        const auto outputs = state_->landmark.session->Run(
        Ort::RunOptions{nullptr}, input_names, &tensor, 1,
        output_names.data(), output_names.size());

        const float* xyz = outputs[0].GetTensorData<float>();
        const float* scores = outputs[1].GetTensorData<float>();
        const float* hands = outputs[2].GetTensorData<float>();

        // model outputs, 21 xyz points, confidence score, right hand score per crop
        for (std::size_t i = 0; i < palms.size(); ++i) {
            // confidence filter, keep low quality crops from caller
            // 0.5f is confidence value for landmark detection
            if (scores[i] <= 0.5f) continue;
            HandLandmarkResult result{};
            // one hand, 21 points, three coordinates each
            std::copy_n(xyz + i * 63, 63, result.xyz.begin());

            const auto& palm = palms[i];
            const float half_size = palm.size * static_cast<float>(std::max(width, height) * 0.5f);
            const float center_x = palm.center_x * width;
            const float center_y = palm.center_y * height;
            const float sine = std::sin(palm.rotation);
            const float cosine = std::cos(palm.rotation);

            for (std::size_t point = 0; point < 21; ++point) {
                const float local_x = (xyz[i * 63 + point * 3] / 224.0f - 0.5f) * 2.0f * half_size;
                const float local_y = (xyz[i * 63 + point * 3 + 1] / 224.0f - 0.5f) * 2.0f * half_size;

                result.frame_xy[point * 2] = (center_x + cosine * local_x - sine * local_y) / width;
                result.frame_xy[point * 2 + 1] = (center_y + sine * local_x + cosine * local_y) / height;
            }

            result.score = scores[i];
            result.right_hand = hands[i];
            results.push_back(result);
        }
    } catch (const Ort::Exception& error) {
        std::cerr << "landmark detection failed: " << error.what() << std::endl;
        results.clear();
    }
    return results;
}