#include "packet_reassembler.h"
#include "mpdu.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace metop {

void PacketReassembler::discard(Partial& p) {
    if (!p.bytes.empty()) {
        ++statistics_.truncated_packets;
        statistics_.discarded_partial_bytes += p.bytes.size();
    }
    p = {};
}

void PacketReassembler::invalidate_all() {
    for (auto& [key, partial] : partials_) {
        (void)key;
        discard(partial);
    }
    partials_.clear();
    // Fragment loss does not erase the last observed counter of each stream.
    // Otherwise a duplicate received after recovery could be delivered again.
}

// Consume only enough bytes to finish this header/packet. Input is always a
// bounded view of one packet zone (or its pre-FHP prefix).
bool PacketReassembler::append(Partial& p, std::span<const std::uint8_t>& bytes) {
    const auto take = [&](std::size_t count) {
        const auto part = bytes.first(std::min(count, bytes.size()));
        p.bytes.insert(p.bytes.end(), part.begin(), part.end());
        bytes = bytes.subspan(part.size());
    };
    if (p.expected_size == 0) {
        take(space_packet_header_size - p.bytes.size());
        if (p.bytes.size() < space_packet_header_size) return true;
        try {
            p.expected_size = parse_space_packet_header(p.bytes).total_packet_bytes;
        } catch (const std::invalid_argument&) {
            ++statistics_.invalid_headers;
            discard(p);
            return false;
        }
    }
    take(p.expected_size - p.bytes.size());
    return true;
}

void PacketReassembler::emit(Partial& p, std::uint32_t end_counter,
                             std::vector<ReassembledPacket>& output) {
    const auto h = validate_space_packet(p.bytes);
    if (h.apid == 0x7ff) {
        ++statistics_.idle_packets;
    } else {
        ++statistics_.reconstructed;
        statistics_.reconstructed_bytes += p.bytes.size();
        ++statistics_.by_vcid[p.source.vcid];
        output.push_back({p.source, end_counter, h, std::move(p.bytes)});
    }
    p = {};
}

std::vector<ReassembledPacket> PacketReassembler::consume(std::span<const std::uint8_t> bytes) {
    std::vector<ReassembledPacket> output;
    if (bytes.size() != vcdu_size) {
        ++statistics_.invalid_frames;
        invalidate_all(); // Cannot trust any stream identity.
        return output;
    }
    const auto v = parse_vcdu(bytes);
    const auto& h = v.header;
    if (h.version != 1) {
        ++statistics_.invalid_frames;
        invalidate_all();
        return output;
    }
    if (h.vcid == 63) return output; // OID frames are outside packet streams.
    const auto key = Key{h.spacecraft_id, h.vcid, h.replay};
    auto& p = partials_[key];
    const auto continuity = counters_.observe(h).status;
    if (continuity != Continuity::first && continuity != Continuity::contiguous)
        discard(p);
    const auto m = parse_mpdu(v.mpdu);
    if (continuity == Continuity::duplicate) {
        // Conservative policy for uncorrected captures: no second delivery,
        // and do not trust either copy as a fragment of an unfinished packet.
        ++statistics_.duplicate_frames;
        statistics_.rejected_zone_bytes += packet_zone_size;
    } else if (m.kind == FirstHeaderKind::invalid) {
        ++statistics_.invalid_frames;
        statistics_.rejected_zone_bytes += packet_zone_size;
        discard(p);
    } else if (m.kind == FirstHeaderKind::idle) {
        // CCSDS 732.0-B-4 4.1.4.2.4.4 note 2 permits idle between fragments.
        ++statistics_.idle_zones;
    } else {
        const auto prefix_size = m.kind == FirstHeaderKind::continuation
            ? packet_zone_size : std::size_t{m.first_header_pointer};
        auto prefix = m.packet_zone.first(prefix_size);
        if (p.bytes.empty()) {
            statistics_.orphan_continuation_bytes += prefix.size();
        } else if (!append(p, prefix)) {
            statistics_.rejected_zone_bytes += prefix.size();
        } else {
            const bool complete = p.expected_size != 0 && p.bytes.size() == p.expected_size;
            // A known next start must exactly agree with the previous length.
            // A continuation-only zone cannot hide another packet after completion.
            if (!prefix.empty() || (m.kind == FirstHeaderKind::packet_start && !complete)) {
                ++statistics_.boundary_mismatches;
                statistics_.rejected_zone_bytes += prefix.size();
                discard(p);
            } else if (complete) {
                emit(p, h.counter, output);
            }
        }
        if (m.kind == FirstHeaderKind::packet_start) {
            auto remaining = m.packet_zone.subspan(m.first_header_pointer);
            while (!remaining.empty()) {
                p.source = h;
                if (!append(p, remaining)) {
                    statistics_.rejected_zone_bytes += remaining.size();
                    break; // Resume only at a later FHP, never byte-scan.
                }
                if (p.expected_size == 0 || p.bytes.size() != p.expected_size) break;
                emit(p, h.counter, output);
            }
        }
    }
    if (p.bytes.empty()) partials_.erase(key);
    return output;
}

} // namespace metop
