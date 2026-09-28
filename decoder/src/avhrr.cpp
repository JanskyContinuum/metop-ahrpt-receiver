#include "avhrr.h"

#include <algorithm>
#include <ostream>
#include <stdexcept>
#include <string>

namespace metop {
namespace {

std::string hex_bytes(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
template<typename Range>
void map_json(std::ostream& out, const Range& values) {
    out << '{';
    bool first = true;
    for (const auto& [key, count] : values) {
        out << (first ? "" : ", ") << '"' << key << "\": " << count;
        first = false;
    }
    out << '}';
}
template<std::size_t N>
void array_json(std::ostream& out, const std::array<std::uint64_t, N>& values) {
    out << '[';
    for (std::size_t i = 0; i < N; ++i) out << (i ? ", " : "") << values[i];
    out << ']';
}
}

AvhrrInspector::AvhrrInspector(std::ostream& csv, bool rs_checked, std::uint64_t preview_limit,
                               std::array<std::ostream*, 2> binary_dumps, std::ostream* text_preview)
    : csv_(csv), rs_checked_(rs_checked), preview_limit_(preview_limit),
      binary_dumps_(binary_dumps), text_preview_(text_preview) {
    csv_ << "packet_index,avhrr_index,spacecraft_id,vcid,replay,start_counter,end_counter,"
            "apid,packet_type,seq_flags,seq_count,secondary_header,data_length_field,"
            "packet_data_bytes,total_size,rs_status,binary_offset,previewed,"
            "payload_first64_hex,payload_last16_hex\n";
    if (!csv_) throw std::runtime_error("Cannot write AVHRR packet log header");
}

void AvhrrInspector::consume(const ReassembledPacket& packet, std::uint64_t packet_index) {
    if (packet.source.vcid != 9) return;
    const auto h = validate_space_packet(packet.bytes);
    if (h.apid != 103 && h.apid != 104) return;
    const auto apid_index = static_cast<std::size_t>(h.apid - 103);
    auto& stats = statistics_.apids[apid_index];
    const auto payload = std::span(packet.bytes).subspan(space_packet_header_size);
    const bool preview = statistics_.previewed_packets < preview_limit_;
    const auto first = preview ? hex_bytes(payload.first(std::min<std::size_t>(64, payload.size()))) : "";
    const auto last = preview ? hex_bytes(payload.last(std::min<std::size_t>(16, payload.size()))) : "";
    const auto rs_status = rs_checked_ ? "rs_checked" : "not_applied";

    if (auto* dump = binary_dumps_[apid_index]) {
        dump->write(reinterpret_cast<const char*>(packet.bytes.data()),
                    static_cast<std::streamsize>(packet.bytes.size()));
        if (!*dump) throw std::runtime_error("Failed to write AVHRR binary packet dump");
    }
    csv_ << packet_index << ',' << statistics_.selected_packets << ','
         << unsigned(packet.source.spacecraft_id) << ",9," << packet.source.replay << ','
         << packet.source.counter << ',' << packet.end_counter << ',' << h.apid << ','
         << h.type << ',' << unsigned(h.sequence_flags) << ',' << h.sequence_count << ','
         << h.secondary_header_flag << ',' << h.data_length_field << ',' << h.packet_data_bytes << ','
         << h.total_packet_bytes << ',' << rs_status << ',';
    if (binary_dumps_[apid_index]) csv_ << stats.bytes;
    csv_ << ',' << preview << ',' << first << ',' << last << '\n';
    if (!csv_) throw std::runtime_error("Failed to write AVHRR packet log");
    if (preview && text_preview_) {
        *text_preview_ << "AVHRR packet " << statistics_.selected_packets << " VCID=9 APID=" << h.apid
            << " seq_flags=" << unsigned(h.sequence_flags) << " seq_count=" << h.sequence_count
            << " secondary_header=" << h.secondary_header_flag << " total_size=" << h.total_packet_bytes
            << " packet_data_bytes=" << payload.size() << " rs_status=" << rs_status
            << "\n  payload_first64_hex=" << first << "\n  payload_last16_hex=" << last << '\n';
        if (!*text_preview_) throw std::runtime_error("Failed to write AVHRR text preview");
    }
    ++statistics_.selected_packets;
    if (preview) ++statistics_.previewed_packets;
    ++stats.packets;
    stats.bytes += packet.bytes.size();
    ++stats.total_size_histogram[h.total_packet_bytes];
    ++stats.sequence_counts[h.sequence_count];
    ++stats.sequence_flags[h.sequence_flags];
    ++stats.secondary_header_flags[h.secondary_header_flag];
    ++stats.packet_types[h.type];
}

void write_avhrr_text(std::ostream& out, const AvhrrStatistics& s) {
    out << "AVHRR packet inspection (VCID 9, no instrument decoding):\n"
        << "  selected_packets: " << s.selected_packets << "\n  previewed_packets: " << s.previewed_packets << '\n';
    for (std::size_t i = 0; i < s.apids.size(); ++i) {
        const auto& a = s.apids[i];
        out << "  APID " << 103 + i << " count: " << a.packets << "\n    total_size_histogram:";
        for (const auto& [size, count] : a.total_size_histogram) out << ' ' << size << '=' << count;
        out << "\n    sequence_counts:";
        for (const auto& [seq, count] : a.sequence_counts) out << ' ' << seq << '=' << count;
        out << "\n    sequence_flags (0..3): "; array_json(out, a.sequence_flags);
        out << "\n    secondary_header_flags (0..1): "; array_json(out, a.secondary_header_flags);
        out << "\n    packet_types (0..1): "; array_json(out, a.packet_types);
        out << '\n';
    }
}

void write_avhrr_json(std::ostream& out, const AvhrrStatistics& s) {
    out << "{\"selected_packets\": " << s.selected_packets << ", \"previewed_packets\": " << s.previewed_packets
        << ", \"apids\": {";
    for (std::size_t i = 0; i < s.apids.size(); ++i) {
        const auto& a = s.apids[i];
        out << (i ? ", " : "") << '"' << 103 + i << "\": {\"packets\": " << a.packets
            << ", \"bytes\": " << a.bytes << ", \"total_size_histogram\": ";
        map_json(out, a.total_size_histogram);
        out << ", \"sequence_counts\": "; map_json(out, a.sequence_counts);
        out << ", \"sequence_flags\": "; array_json(out, a.sequence_flags);
        out << ", \"secondary_header_flags\": "; array_json(out, a.secondary_header_flags);
        out << ", \"packet_types\": "; array_json(out, a.packet_types);
        out << '}';
    }
    out << "}}";
}

} // namespace metop
