#include <cstdint>
#include <vector>

bool decode_mjpeg(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& rgb, uint32_t& width, uint32_t& height);