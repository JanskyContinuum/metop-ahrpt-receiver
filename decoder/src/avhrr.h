#pragma once

#include "packet_reassembler.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>

namespace metop {

struct AvhrrApidStatistics {
    std::uint64_t packets = 0;
    std::uint64_t bytes = 0; // Whole Space Packets, including the primary header.
    std::map<std::size_t, std::uint64_t> total_size_histogram;
    std::map<std::uint16_t, std::uint64_t> sequence_counts;
    std::array<std::uint64_t, 4> sequence_flags{};
    std::array<std::uint64_t, 2> secondary_header_flags{};
    std::array<std::uint64_t, 2> packet_types{};
};
struct AvhrrStatistics {
    std::uint64_t selected_packets = 0;
    std::uint64_t previewed_packets = 0;
    std::array<AvhrrApidStatistics, 2> apids{}; // 103, 104
};

// M6 inspection only. "Payload" means the entire CCSDS Packet Data Field,
// including any secondary header. No instrument offsets or sample unpacking.
class AvhrrInspector {
public:
    AvhrrInspector(std::ostream& csv, bool rs_checked, std::uint64_t preview_limit = 20,
                   std::array<std::ostream*, 2> binary_dumps = {},
                   std::ostream* text_preview = nullptr);
    // Source identity comes from the reassembler; header fields are revalidated
    // from bytes before any output. Unselected VCIDs/APIDs are not dumped.
    void consume(const ReassembledPacket& packet, std::uint64_t packet_index);
    const AvhrrStatistics& statistics() const noexcept { return statistics_; }
private:
    std::ostream& csv_;
    bool rs_checked_;
    std::uint64_t preview_limit_;
    std::array<std::ostream*, 2> binary_dumps_;
    std::ostream* text_preview_;
    AvhrrStatistics statistics_;
};

void write_avhrr_text(std::ostream& output, const AvhrrStatistics& statistics);
void write_avhrr_json(std::ostream& output, const AvhrrStatistics& statistics);

} // namespace metop
