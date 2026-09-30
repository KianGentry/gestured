#include "vision/onnx_tracker.hpp"
#include <iostream>
#include <sstream>
#include <onnxruntime_cxx_api.h>
#include <utility>

struct OnnxTracker::State {
    Ort::Env environment;
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;

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
        auto input_name = state_->session->GetInputNameAllocated(0, allocator);

        const auto shape = state_->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();

        std::ostringstream description;

        description << "onnx input: " << input_name.get() << " [";
        for (std::size_t i = 0; i < shape.size(); ++i) {
            if (i > 0) description << ", ";
            description << shape[i];
        }
        description << "]";

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