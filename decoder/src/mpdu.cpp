#include "mpdu.h"

#include <stdexcept>

namespace metop {

Mpdu parse_mpdu(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != mpdu_size) throw std::invalid_argument("M-PDU must contain exactly 884 bytes");
    Mpdu result;
    result.reserved_spare = static_cast<std::uint8_t>(bytes[0] >> 3);
    result.first_header_pointer = static_cast<std::uint16_t>(
        (std::uint16_t{static_cast<std::uint8_t>(bytes[0] & 7)} << 8) | bytes[1]);
    const auto fhp = result.first_header_pointer;
    result.kind = fhp < packet_zone_size ? FirstHeaderKind::packet_start
        : fhp == 0x7fe ? FirstHeaderKind::idle
        : fhp == 0x7ff ? FirstHeaderKind::continuation : FirstHeaderKind::invalid;
    result.packet_zone = bytes.subspan(2, packet_zone_size);
    return result;
}

std::string_view first_header_kind_name(FirstHeaderKind kind) {
    switch (kind) {
    case FirstHeaderKind::packet_start: return "packet_start";
    case FirstHeaderKind::idle: return "idle";
    case FirstHeaderKind::continuation: return "continuation";
    case FirstHeaderKind::invalid: return "invalid";
    }
    throw std::logic_error("Unknown FHP kind");
}

} // namespace metop
