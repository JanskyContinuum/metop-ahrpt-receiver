#include "statistics.h"

#include <fstream>
#include <ostream>
#include <stdexcept>

namespace metop {
namespace {
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
    output << "Stage: M2 derandomization diagnostics (RS not applied)\n"
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
}

void write_json_statistics(std::ostream& output, const RunStatistics& statistics) {
    const auto& c = statistics.cadu;
    output << "{\n  \"schema_version\": 2,\n  \"stage\": \"derandomization_diagnostics\",\n"
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
