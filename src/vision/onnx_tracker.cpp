#include "vision/onnx_tracker.hpp"
#include <iostream>
#include <sstream>
#include <onnxruntime_cxx_api.h>
#include <utility>
#include <cstdint>
#include <vector>

namespace
{

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

} // namespace

struct OnnxTracker::State {
    Ort::Env environment;
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    std::string input_name;
    std::vector<int64_t> input_shape;
    ONNXTensorElementDataType input_type =
        ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;

    State() : environment(ORT_LOGGING_LEVEL_WARNING, "gestured") {}
};

OnnxTracker::~OnnxTracker() = default;
OnnxTracker::OnnxTracker(std::string model_path) : model_path_(std::move(model_path)), state_(std::make_unique<State>()) {}

bool OnnxTracker::initialise() {
    try {
        state_->options.SetIntraOpNumThreads(1);
        state_->options.SetInterOpNumThreads(1);
        state_->options.DisableCpuMemArena();
        state_->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        state_->session = std::make_unique<Ort::Session>(state_->environment, model_path_.c_str(), state_->options);

        Ort::AllocatorWithDefaultOptions allocator;
        const std::size_t input_count = state_->session->GetInputCount();

        std::cout << "onnx input count: " << input_count << std::endl;

        for (std::size_t i = 0; i < input_count; ++i) {
            auto name = state_->session->GetInputNameAllocated(i, allocator);
            
            const auto type_info = state_->session->GetInputTypeInfo(i);

            std::cout << "input " << i << ": " << name.get() << " type " << static_cast<int>(type_info.GetONNXType()) << std::endl;
        }

        const auto input_type_info = state_->session->GetInputTypeInfo(0);

        if (input_type_info.GetONNXType() != ONNX_TYPE_TENSOR) {
            std::cerr << "onnx input not a tensor" << std::endl;
            return false;
        }

        auto input_name = state_->session->GetInputNameAllocated(0, allocator);

        const auto tensor_info =
            input_type_info.GetTensorTypeAndShapeInfo();

        state_->input_name = input_name.get();

        const auto& shape = tensor_info.GetShape();
        state_->input_shape = shape;
        const auto type = tensor_info.GetElementType();
        state_->input_type = type;

        std::cout << "input rank: " << shape.size() << ", element type: " << static_cast<int>(type) << std::endl;

        std::ostringstream description;

        description << "onnx input: " << input_name.get() << " [";
        for (std::size_t i = 0; i < shape.size(); ++i) {
            if (i > 0) description << ",";
            description << shape[i];
        }
        description << "] " << element_type_name(type);
        input_description_ = description.str();

        return true;
        
    } catch (const Ort::Exception& error) {
        std::cerr << "onnx init failed: " << error.what() << std::endl;
        state_->session.reset();
        return false;
    }
}

const std::string& OnnxTracker::input_description() const {
    return input_description_;
}

bool OnnxTracker::run_test() {
    try {
        Ort::AllocatorWithDefaultOptions allocator;

        const auto& shape = state_->input_shape;

        std::cout << "test input rank: " << shape.size() << std::endl;

        for (const auto dim : shape) std::cout << "dim: " << dim << std::endl;

        if (shape.size() != 4) {
            std::cerr << "unexpected onnx input rank" << std::endl;
            return false;
        }

        std::size_t elements = 1;

        for (const auto dimension : shape) {
            if (dimension <= 0) {
                std::cerr << "dynamic onnx input shape unsupported" << std::endl;
                return false;
            }
            elements *= static_cast<std::size_t>(dimension);
        }

        const auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        if (state_->input_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            std::cerr << "unsupported ONNX input type for test" << std::endl;
            return false;
        }

        std::vector<float> data(elements, 0.0f);
        Ort::Value input = Ort::Value::CreateTensor<float>(memory_info, data.data(), data.size(), shape.data(), shape.size());

        const char* input_names[] = {state_->input_name.c_str()};

        const std::size_t output_count = state_->session->GetOutputCount();
        if (output_count == 0) {
            std::cerr << "no onnx outputs" << std::endl;
            return false;
        }

        std::vector<Ort::AllocatedStringPtr> output_storage;
        std::vector<const char*> output_names;

        output_storage.reserve(output_count);
        output_names.reserve(output_count);

        for (std::size_t i = 0; i < output_count; ++i) {
            output_storage.push_back(state_->session->GetOutputNameAllocated(i, allocator));
            output_names.push_back(output_storage.back().get());
        }

        const auto outputs = state_->session->Run(Ort::RunOptions{nullptr}, input_names, &input, 1, output_names.data(), output_names.size());

        return !outputs.empty();

    } catch (const Ort::Exception& error) {
        std::cerr << "onnx run test failed: " << error.what() << std::endl;
        return false;
    }
}