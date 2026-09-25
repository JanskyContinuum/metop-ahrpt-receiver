#include "statistics.h"

#include <fstream>
#include <ostream>
#include <stdexcept>

namespace metop {
namespace {
auto packet_counters(const PacketStatistics& p) {
    return std::array<std::pair<std::string_view, std::uint64_t>, 12>{{
        {"reconstructed", p.reconstructed}, {"reconstructed_bytes", p.reconstructed_bytes},
        {"idle_packets", p.idle_packets}, {"invalid_headers", p.invalid_headers},
        {"boundary_mismatches", p.boundary_mismatches}, {"truncated_packets", p.truncated_packets},
        {"discarded_partial_bytes", p.discarded_partial_bytes},
        {"orphan_continuation_bytes", p.orphan_continuation_bytes},
        {"rejected_zone_bytes", p.rejected_zone_bytes}, {"idle_zones", p.idle_zones},
        {"invalid_frames", p.invalid_frames}, {"duplicate_frames", p.duplicate_frames}}};
}
auto frame_counters(const FrameStatistics& f) {
    return std::array<std::pair<std::string_view, std::uint64_t>, 13>{{
        {"headers", f.headers}, {"invalid_versions", f.invalid_versions},
        {"idle_frames", f.idle_frames}, {"nonzero_signaling_spare", f.nonzero_signaling_spare},
        {"valid_mpdus", f.valid_mpdus}, {"invalid_fhp", f.invalid_fhp},
        {"nonzero_mpdu_spare", f.nonzero_mpdu_spare}, {"packet_start_zones", f.packet_start_zones},
        {"continuation_zones", f.continuation_zones}, {"idle_zones", f.idle_zones},
        {"counter_gaps", f.counter_gaps}, {"counter_duplicates", f.counter_duplicates},
        {"counter_backward_or_reset", f.counter_backward_or_reset}}};
}
template<std::size_t N>
void histogram_json(std::ostream& output, const std::array<std::uint64_t, N>& values) {
    output << '{';
    bool first = true;
    for (std::size_t i = 0; i < N; ++i) {
        if (!values[i]) continue;
        if (!first) output << ", ";
        output << '"' << i << "\": " << values[i];
        first = false;
    }
    output << '}';
}
template<std::size_t N>
void histogram_text(std::ostream& output, const char* label, const std::array<std::uint64_t, N>& values) {
    output << label << ':';
    for (std::size_t i = 0; i < N; ++i) if (values[i]) output << ' ' << i << '=' << values[i];
    output << '\n';
}
}

void write_text_statistics(std::ostream& output, const RunStatistics& statistics) {
    const auto& c = statistics.cadu;
    output << "Stage: " << (statistics.frames ? "M4 Space Packet reassembly" : "M2 derandomization diagnostics")
           << " (RS not applied)\n"
           << "Input size: " << statistics.input_size << " bytes\n"
           << "CADUs read: " << c.cadus_read << '\n'
           << "Valid ASM: " << c.valid_asm << '\n'
           << "ASM failures (alignment episodes): " << c.asm_failures << '\n'
           << "Resyncs: " << c.resyncs << '\n'
           << "Skipped bytes: " << c.skipped_bytes << '\n'
           << "Trailing bytes: " << c.trailing_bytes << '\n'
           << "Rejected resync candidates: " << c.rejected_candidates << '\n'
           << "Unconfirmed EOF candidates: " << c.unconfirmed_candidates << '\n'
           << "Bytes consumed: " << c.bytes_consumed << '\n'
           << "Reached EOF: " << (c.reached_eof ? "yes" : "no") << '\n'
           << "Stopped by limit: " << (statistics.stopped_by_limit ? "yes" : "no") << '\n';
    histogram_text(output, "VCID before XOR (diagnostic)", statistics.vcids_before);
    histogram_text(output, "VCID after XOR (uncorrected)", statistics.vcids_after);
    histogram_text(output, "Version after XOR (uncorrected)", statistics.versions_after);
    histogram_text(output, "Spacecraft ID after XOR (uncorrected)", statistics.spacecraft_after);
    if (statistics.frames) {
        output << "VCDU/M-PDU (uncorrected):\n";
        for (const auto& [name, value] : frame_counters(*statistics.frames)) output << "  " << name << ": " << value << '\n';
        histogram_text(output, "Version-1 VCIDs", statistics.frames->vcids);
        histogram_text(output, "Counter discontinuities by VCID", statistics.frames->discontinuities_by_vcid);
    }
    if (statistics.packets) {
        output << "Space Packets (uncorrected):\n";
        for (const auto& [name, value] : packet_counters(*statistics.packets))
            output << "  " << name << ": " << value << '\n';
        histogram_text(output, "Reconstructed packets by VCID", statistics.packets->by_vcid);
    }
}

void write_json_statistics(std::ostream& output, const RunStatistics& statistics) {
    const auto& c = statistics.cadu;
    output << "{\n  \"schema_version\": 4,\n  \"stage\": \""
           << (statistics.frames ? "space_packets_no_rs" : "derandomization_diagnostics") << "\",\n"
           << "  \"rs_applied\": false,\n"
           << "  \"input_size\": " << statistics.input_size << ",\n"
           << "  \"stopped_by_limit\": " << (statistics.stopped_by_limit ? "true" : "false") << ",\n"
           << "  \"cadu\": {\n"
           << "    \"cadus_read\": " << c.cadus_read << ",\n"
           << "    \"valid_asm\": " << c.valid_asm << ",\n"
           << "    \"asm_failures\": " << c.asm_failures << ",\n"
           << "    \"resyncs\": " << c.resyncs << ",\n"
           << "    \"skipped_bytes\": " << c.skipped_bytes << ",\n"
           << "    \"trailing_bytes\": " << c.trailing_bytes << ",\n"
           << "    \"rejected_candidates\": " << c.rejected_candidates << ",\n"
           << "    \"unconfirmed_candidates\": " << c.unconfirmed_candidates << ",\n"
           << "    \"bytes_consumed\": " << c.bytes_consumed << ",\n"
           << "    \"reached_eof\": " << (c.reached_eof ? "true" : "false") << "\n  },\n"
           << "  \"vcids_before\": ";
    histogram_json(output, statistics.vcids_before);
    output << ",\n  \"vcids_after\": ";
    histogram_json(output, statistics.vcids_after);
    output << ",\n  \"versions_after\": ";
    histogram_json(output, statistics.versions_after);
    output << ",\n  \"spacecraft_after\": ";
    histogram_json(output, statistics.spacecraft_after);
    if (statistics.frames) {
        output << ",\n  \"frames\": {\n";
        for (const auto& [name, value] : frame_counters(*statistics.frames)) output << "    \"" << name << "\": " << value << ",\n";
        output << "    \"vcids\": ";
        histogram_json(output, statistics.frames->vcids);
        output << ",\n    \"discontinuities_by_vcid\": ";
        histogram_json(output, statistics.frames->discontinuities_by_vcid);
        output << "\n  }";
    }
    if (statistics.packets) {
        output << ",\n  \"packets\": {\n";
        for (const auto& [name, value] : packet_counters(*statistics.packets))
            output << "    \"" << name << "\": " << value << ",\n";
        output << "    \"by_vcid\": ";
        histogram_json(output, statistics.packets->by_vcid);
        output << "\n  }";
    }
    output << "\n}\n";
}

void save_statistics(const std::filesystem::path& directory, const RunStatistics& statistics) {
    const auto write = [&](const char* name, auto writer) {
        std::ofstream file(directory / name, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error(std::string("Cannot open output ") + name);
        }
        writer(file, statistics);
        file.close();
        if (!file) {
            throw std::runtime_error(std::string("Failed to write output ") + name);
        }
    };
    write("stats.txt", write_text_statistics);
    write("stats.json", write_json_statistics);
}

} // namespace metop
