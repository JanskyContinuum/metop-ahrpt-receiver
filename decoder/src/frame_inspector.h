#pragma once

#include "vcdu.h"

#include <iosfwd>

namespace metop {

struct FrameStatistics {
    std::uint64_t headers = 0;
    std::uint64_t invalid_versions = 0;
    std::uint64_t idle_frames = 0;
    std::uint64_t nonzero_signaling_spare = 0;
    std::uint64_t valid_mpdus = 0;
    std::uint64_t invalid_fhp = 0;
    std::uint64_t nonzero_mpdu_spare = 0;
    std::uint64_t packet_start_zones = 0;
    std::uint64_t continuation_zones = 0;
    std::uint64_t idle_zones = 0;
    std::uint64_t counter_gaps = 0;
    std::uint64_t counter_duplicates = 0;
    std::uint64_t counter_backward_or_reset = 0;
    std::array<std::uint64_t, 64> vcids{};
    std::array<std::uint64_t, 64> discontinuities_by_vcid{};
};

// M3 inspection only. Does not reconstruct packets or validate RS parity.
class FrameInspector {
public:
    explicit FrameInspector(std::ostream& log);
    void inspect(std::uint64_t cadu_index, std::uint64_t file_offset,
                 std::span<const std::uint8_t> vcdu);
    const FrameStatistics& statistics() const noexcept { return statistics_; }
private:
    std::ostream& log_;
    FrameStatistics statistics_;
    VcduCounterTracker counters_;
};

} // namespace metop
