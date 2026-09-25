#pragma once

#include "space_packet.h"
#include "vcdu.h"

#include <vector>

namespace metop {

struct ReassembledPacket {
    VcduHeader source; // Identity and counter of the VCDU where the packet started.
    std::uint32_t end_counter = 0;
    SpacePacketHeader header;
    std::vector<std::uint8_t> bytes; // Owns the complete primary header and data.
};

struct PacketStatistics {
    std::uint64_t reconstructed = 0; // Non-idle Space Packets delivered.
    std::uint64_t reconstructed_bytes = 0;
    std::uint64_t idle_packets = 0; // APID 2047, delimited but not delivered.
    std::uint64_t invalid_headers = 0;
    std::uint64_t boundary_mismatches = 0;
    std::uint64_t truncated_packets = 0; // Observed partials discarded, not estimated losses.
    std::uint64_t discarded_partial_bytes = 0;
    std::uint64_t orphan_continuation_bytes = 0;
    std::uint64_t rejected_zone_bytes = 0;
    std::uint64_t idle_zones = 0;
    std::uint64_t invalid_frames = 0;
    std::uint64_t duplicate_frames = 0;
    std::array<std::uint64_t, 64> by_vcid{};
};

// Input: full, derandomized 892-byte VCDUs. No RS or instrument decoding.
// Resume only at an advertised FHP; never scan unknown data for plausible headers.
class PacketReassembler {
public:
    std::vector<ReassembledPacket> consume(std::span<const std::uint8_t> vcdu);
    // Call after unknown framing loss, or at EOF / a user-requested processing limit.
    // No incomplete packet is emitted. Repeated calls are harmless.
    void invalidate_all();
    void finish() { invalidate_all(); }
    const PacketStatistics& statistics() const noexcept { return statistics_; }
private:
    struct Partial {
        VcduHeader source;
        std::vector<std::uint8_t> bytes;
        std::size_t expected_size = 0; // Zero while the primary header is split.
    };
    using Key = std::tuple<std::uint8_t, std::uint8_t, bool>;
    std::map<Key, Partial> partials_;
    VcduCounterTracker counters_;
    PacketStatistics statistics_;

    void discard(Partial& partial);
    bool append(Partial& partial, std::span<const std::uint8_t>& bytes);
    void emit(Partial& partial, std::uint32_t end_counter,
              std::vector<ReassembledPacket>& output);
};

} // namespace metop
