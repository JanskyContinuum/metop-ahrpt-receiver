#include "sample_unpack.h"
#include <limits>
#include <stdexcept>

namespace metop {
std::vector<std::uint16_t> unpack_10bit_msb(std::span<const std::uint8_t> bytes,
                                         std::size_t bit_offset, std::size_t count) {
    if (bytes.size() > std::numeric_limits<std::size_t>::max() / 8)
        throw std::invalid_argument("Bit range cannot be represented");
    const auto bits = bytes.size() * 8;
    if (bit_offset > bits || count > (bits - bit_offset) / 10)
        throw std::invalid_argument("Truncated ten-bit sample range");
    std::vector<std::uint16_t> output(count);
    for (auto& sample : output) {
        for (unsigned bit = 0; bit < 10; ++bit, ++bit_offset)
            sample = static_cast<std::uint16_t>((sample << 1)
                | ((bytes[bit_offset / 8] >> (7 - bit_offset % 8)) & 1U));
    }
    return output;
}
}
