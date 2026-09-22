#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace metop {

inline constexpr std::size_t cvcdu_size = 1020;

// XOR exactly the 1020 bytes after the ASM, including the RS parity symbols.
// Each call starts at the all-ones initial state. The ASM must not be included.
void derandomize(std::span<std::uint8_t, cvcdu_size> data);

} // namespace metop
