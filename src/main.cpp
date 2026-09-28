#include <chrono>
#include <csignal>
#include <thread>

namespace 
{
volatile std::sig_atomic_t running = 0;

void handle_signal(int) {
    running = 0;
}
} // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    while(running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}