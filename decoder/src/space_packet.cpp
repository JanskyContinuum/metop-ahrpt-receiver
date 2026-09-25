#include "space_packet.h"

#include <stdexcept>

namespace metop {

SpacePacketHeader parse_space_packet_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < space_packet_header_size)
        throw std::invalid_argument("Space Packet header needs six bytes");
    SpacePacketHeader h;
    h.version = static_cast<std::uint8_t>(bytes[0] >> 5);
    if (h.version != 0) throw std::invalid_argument("Unsupported Space Packet version");
    h.type = (bytes[0] & 0x10) != 0;
    h.secondary_header_flag = (bytes[0] & 0x08) != 0;
    h.apid = static_cast<std::uint16_t>(((bytes[0] & 7U) << 8) | bytes[1]);
    h.sequence_flags = static_cast<std::uint8_t>(bytes[2] >> 6);
    h.sequence_count = static_cast<std::uint16_t>(((bytes[2] & 0x3fU) << 8) | bytes[3]);
    h.data_length_field = static_cast<std::uint16_t>((std::uint16_t{bytes[4]} << 8) | bytes[5]);
    // Widen BEFORE adding one: FFFF means 65536 data bytes, not zero.
    h.packet_data_bytes = std::size_t{h.data_length_field} + 1;
    h.total_packet_bytes = space_packet_header_size + h.packet_data_bytes;
    return h;
}

SpacePacketHeader validate_space_packet(std::span<const std::uint8_t> bytes) {
    const auto h = parse_space_packet_header(bytes);
    if (bytes.size() != h.total_packet_bytes)
        throw std::invalid_argument("Space Packet size disagrees with Packet Data Length");
    return h;
}

} // namespace metop
