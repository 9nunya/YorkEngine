#ifndef YORK_IMAGE_DECODE_HXX
#define YORK_IMAGE_DECODE_HXX

#include <cstdint>
#include <string>
#include <vector>

#include "types/texture_data.hxx"

namespace york::assets {
    // Decodes encoded image bytes (PNG/JPEG/BMP/TGA/GIF/PSD/HDR/PNM...) into
    // owned RGBA8 pixels. The MIME type is informational only; the decoder
    // sniffs the actual encoding from the bytes so any correct container
    // round-trips. One stb_image implementation lives here so importers can
    // share it without duplicating symbols.
    [[nodiscard]] types::ImageData decodeImage(
        std::string name,
        std::string mimeType,
        const std::vector<uint8_t> &encodedBytes);
}

#endif // YORK_IMAGE_DECODE_HXX
