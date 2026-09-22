#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace metop {

inline constexpr std::size_t mpdu_size = 884;
inline constexpr std::size_t packet_zone_size = 882;
enum class FirstHeaderKind { packet_start, idle, continuation, invalid };

struct Mpdu {
    std::uint8_t reserved_spare = 0;
    std::uint16_t first_header_pointer = 0;
    FirstHeaderKind kind = FirstHeaderKind::invalid;
    // Borrowed view. No header or sample offsets are inferred within this zone.
    std::span<const std::uint8_t> packet_zone;
};

// Wrong byte lengths throw; out-of-range FHPs are explicitly classified invalid.
Mpdu parse_mpdu(std::span<const std::uint8_t> bytes);
std::string_view first_header_kind_name(FirstHeaderKind kind);

} // namespace metop
