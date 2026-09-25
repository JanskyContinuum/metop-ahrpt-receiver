#pragma once

#include "cadu_reader.h"
#include "frame_inspector.h"

#include <filesystem>
#include <iosfwd>

namespace metop {

struct RunStatistics {
    std::uintmax_t input_size = 0;
    CaduStatistics cadu;
    // Diagnostic bit extractions only: not RS-validated VCDU headers.
    std::array<std::uint64_t, 64> vcids_before{};
    std::array<std::uint64_t, 64> vcids_after{};
    std::array<std::uint64_t, 4> versions_after{};
    std::array<std::uint64_t, 256> spacecraft_after{};
    std::optional<FrameStatistics> frames;
    bool stopped_by_limit = false;
};

void write_text_statistics(std::ostream& output, const RunStatistics& statistics);
void write_json_statistics(std::ostream& output, const RunStatistics& statistics);
void save_statistics(const std::filesystem::path& directory, const RunStatistics& statistics);

} // namespace metop
