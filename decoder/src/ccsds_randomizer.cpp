#include "ccsds_randomizer.h"

#include <array>

namespace metop {
namespace {

// CCSDS 131.0-B-5, 10.4.2-10.4.4: legacy 255-bit sequence,
// h(x) = x^8 + x^7 + x^5 + x^3 + 1, transmitted MSB first.
// Bit j of state holds s[n+j]; s[n+8] = s[n+7]^s[n+5]^s[n+3]^s[n].
constexpr auto make_mask() {
    std::array<std::uint8_t, cvcdu_size> mask{};
    unsigned state = 0xff;
    for (auto& byte : mask) {
        unsigned value = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            value = (value << 1) | (state & 1U);
            const auto next = (state ^ (state >> 3) ^ (state >> 5) ^ (state >> 7)) & 1U;
            state = (state >> 1) | (next << 7);
        }
        byte = static_cast<std::uint8_t>(value);
    }
    return mask;
}

constexpr auto mask = make_mask();

} // namespace

void derandomize(std::span<std::uint8_t, cvcdu_size> data) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] ^= mask[i];
    }
}

} // namespace metop
