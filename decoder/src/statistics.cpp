#include "statistics.h"

#include <fstream>
#include <ostream>
#include <stdexcept>

namespace metop {

void write_text_statistics(std::ostream& output, const RunStatistics& statistics) {
    const auto& c = statistics.cadu;
    output << "Stage: M1 CADU framing only\n"
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
}

void write_json_statistics(std::ostream& output, const RunStatistics& statistics) {
    const auto& c = statistics.cadu;
    output << "{\n  \"schema_version\": 1,\n  \"stage\": \"cadu_framing\",\n"
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
           << "    \"reached_eof\": " << (c.reached_eof ? "true" : "false") << "\n  }\n}\n";
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
