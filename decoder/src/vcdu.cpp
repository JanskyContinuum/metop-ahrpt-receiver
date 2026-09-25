#include "vcdu.h"

#include <stdexcept>

namespace metop {

Vcdu parse_vcdu(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != vcdu_size) throw std::invalid_argument("VCDU must contain exactly 892 bytes");
    Vcdu result;
    auto& h = result.header;
    h.version = static_cast<std::uint8_t>(bytes[0] >> 6);
    h.spacecraft_id = static_cast<std::uint8_t>(((bytes[0] & 0x3f) << 2) | (bytes[1] >> 6));
    h.vcid = bytes[1] & 0x3f;
    h.counter = (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 8) | bytes[4];
    h.replay = (bytes[5] & 0x80) != 0;
    h.signaling_spare = bytes[5] & 0x7f;
    result.insert_zone = {bytes[6], bytes[7]};
    result.mpdu = bytes.subspan(8, 884);
    return result;
}

CounterObservation VcduCounterTracker::observe(const VcduHeader& header) {
    if (header.counter > vcdu_counter_mask || header.vcid > 63)
        throw std::invalid_argument("Counter or VCID outside wire range");
    const auto key = std::make_tuple(header.spacecraft_id, header.vcid, header.replay);
    const auto [entry, inserted] = last_.try_emplace(key, header.counter);
    if (inserted) return {};
    const auto previous = entry->second;
    const auto delta = (header.counter - previous) & vcdu_counter_mask;
    entry->second = header.counter;
    // Half-range disambiguation is diagnostic only; resets/corruption cannot be
    // distinguished from packet loss in --no-rs mode. Do not estimate lost scans.
    const auto status = delta == 0 ? Continuity::duplicate
        : delta == 1 ? Continuity::contiguous
        : delta < 0x800000 ? Continuity::gap : Continuity::backward_or_reset;
    return {status, previous};
}

std::string_view continuity_name(Continuity value) {
    switch (value) {
    case Continuity::first: return "first";
    case Continuity::contiguous: return "contiguous";
    case Continuity::gap: return "gap";
    case Continuity::duplicate: return "duplicate";
    case Continuity::backward_or_reset: return "backward_or_reset";
    }
    throw std::logic_error("Unknown continuity status");
}

} // namespace metop
