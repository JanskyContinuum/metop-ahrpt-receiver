#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace metop {

inline constexpr std::size_t space_packet_header_size = 6;
inline constexpr std::size_t max_space_packet_size = 65542;

struct SpacePacketHeader {
    std::uint8_t version = 0;
    bool type = false;
    bool secondary_header_flag = false;
    std::uint16_t apid = 0;
    std::uint8_t sequence_flags = 0;
    std::uint16_t sequence_count = 0;
    std::uint16_t data_length_field = 0;
    std::size_t packet_data_bytes = 0;
    std::size_t total_packet_bytes = 0;
};

// Requires at least six bytes and CCSDS Space Packet version 0.
// Does not interpret the secondary header or instrument data.
SpacePacketHeader parse_space_packet_header(std::span<const std::uint8_t> bytes);
// Additionally requires exactly the length advertised by the primary header.
SpacePacketHeader validate_space_packet(std::span<const std::uint8_t> bytes);

} // namespace metop
