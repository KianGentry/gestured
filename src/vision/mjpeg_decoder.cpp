#include "vision/mjpeg_decoder.hpp"
#include <csetjmp>
#include <stdio.h> // for jpeglib.h
#include <jpeglib.h>

namespace
{

struct DecoderError {
    jpeg_error_mgr base;
    std::jmp_buf jump;
};

void handle_error(j_common_ptr decoder) {
    // redirect libjpeg failures, return control to the decoder cleanup path
    auto* error = reinterpret_cast<DecoderError*>(decoder->err);
    std::longjmp(error->jump, 1);
}

} // namespace

bool decode_mjpeg(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& rgb, uint32_t& width, uint32_t& height) {
    // empty input cannot contain a jpeg image
    if (compressed.empty()) {
        return false;
    }

    jpeg_decompress_struct decoder{};
    DecoderError error;
    decoder.err = jpeg_std_error(&error.base);
    error.base.error_exit = handle_error;

    // libjpeg uses longjmp for fatal errors, establish recovery before decoder calls
    if (setjmp(error.jump)) {
        jpeg_destroy_decompress(&decoder);
        return false;
    }

    jpeg_create_decompress(&decoder);
    // read jpeg bytes directly from the camera frame buffer
    jpeg_mem_src(&decoder, compressed.data(), compressed.size());
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    // request rgb output, camera input is usually compressed mjpeg
    decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&decoder);

    width = decoder.output_width;
    height = decoder.output_height;

    // each output row contains three bytes per pixel
    const std::size_t row_bytes = static_cast<std::size_t>(width * 3);

    rgb.resize(row_bytes * height);

    // decode one scanline at a time into the output buffer
    while (decoder.output_scanline < decoder.output_height) {
        uint8_t* row = rgb.data() + static_cast<std::size_t>(decoder.output_scanline * row_bytes);
        jpeg_read_scanlines(&decoder, reinterpret_cast<JSAMPARRAY>(&row), 1);
    }

    jpeg_finish_decompress(&decoder);
    // release libjpeg state after a successful decode
    jpeg_destroy_decompress(&decoder);
    return true;
}