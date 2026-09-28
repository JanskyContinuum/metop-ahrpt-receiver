#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace metop {
// MSB-first contiguous ten-bit words. Rejects out-of-bounds ranges before reading.
std::vector<std::uint16_t> unpack_10bit_msb(std::span<const std::uint8_t> bytes,
                                         std::size_t bit_offset, std::size_t count);
}
