#include <memory>
#include <string>

class OnnxTracker {
public:
    explicit OnnxTracker(std::string model_path);
    ~OnnxTracker();

    bool initialise();
    const std::string& input_description() const;

private:
    std::string model_path_;
    std::string input_description_;
    struct State;
    std::unique_ptr<State> state_;

};