#include "frame_inspector.h"
#include "mpdu.h"

#include <ostream>
#include <stdexcept>

namespace metop {

FrameInspector::FrameInspector(std::ostream& log) : log_(log) {
    log_ << "cadu_index,file_offset,rs_status,version,spacecraft_id,vcid,counter,replay,"
            "signaling_spare,insert0,insert1,previous_counter,continuity,fhp,mpdu_spare,fhp_kind,status\n";
    if (!log_) throw std::runtime_error("Cannot write VCDU log header");
}

void FrameInspector::inspect(std::uint64_t index, std::uint64_t offset,
                             std::span<const std::uint8_t> bytes) {
    const auto vcdu = parse_vcdu(bytes);
    const auto& h = vcdu.header;
    ++statistics_.headers;
    log_ << index << ',' << offset << ",not_applied," << unsigned(h.version) << ','
         << unsigned(h.spacecraft_id) << ',' << unsigned(h.vcid) << ',' << h.counter << ','
         << h.replay << ',' << unsigned(h.signaling_spare) << ','
         << unsigned(vcdu.insert_zone[0]) << ',' << unsigned(vcdu.insert_zone[1]) << ',';
    if (h.version != 1) {
        ++statistics_.invalid_versions;
        log_ << ",not_checked,,,,invalid_version\n";
    } else {
        ++statistics_.vcids[h.vcid];
        if (h.signaling_spare) ++statistics_.nonzero_signaling_spare;
        if (h.vcid == 63) {
            // AOS Only Idle Data frames have no M-PDU packet stream to track.
            ++statistics_.idle_frames;
            log_ << ",not_checked,,,,idle_frame\n";
        } else {
            const auto observation = counters_.observe(h);
            if (observation.previous) log_ << *observation.previous;
            log_ << ',' << continuity_name(observation.status) << ',';
            if (observation.status == Continuity::gap) ++statistics_.counter_gaps;
            if (observation.status == Continuity::duplicate) ++statistics_.counter_duplicates;
            if (observation.status == Continuity::backward_or_reset) ++statistics_.counter_backward_or_reset;
            if (observation.status != Continuity::first && observation.status != Continuity::contiguous)
                ++statistics_.discontinuities_by_vcid[h.vcid];
            const auto mpdu = parse_mpdu(vcdu.mpdu);
            if (mpdu.reserved_spare) ++statistics_.nonzero_mpdu_spare;
            if (mpdu.kind == FirstHeaderKind::invalid) ++statistics_.invalid_fhp;
            if (mpdu.kind == FirstHeaderKind::packet_start) ++statistics_.packet_start_zones;
            if (mpdu.kind == FirstHeaderKind::continuation) ++statistics_.continuation_zones;
            if (mpdu.kind == FirstHeaderKind::idle) ++statistics_.idle_zones;
            // The specification constrains the low 11 FHP bits. The capture uses
            // FF FF for continuation; preserve/report spare bits without treating
            // them as FHP or imposing an unverified mission-specific spare value.
            const bool valid = mpdu.kind != FirstHeaderKind::invalid;
            if (valid) ++statistics_.valid_mpdus;
            log_ << mpdu.first_header_pointer << ',' << unsigned(mpdu.reserved_spare) << ','
                 << first_header_kind_name(mpdu.kind) << ',' << (valid ? "uncorrected" : "invalid_mpdu") << '\n';
        }
    }
    if (!log_) throw std::runtime_error("Failed to write VCDU log");
}

} // namespace metop
