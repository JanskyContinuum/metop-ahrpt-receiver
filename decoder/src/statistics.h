#pragma once

#include "cadu_reader.h"

#include <filesystem>
#include <iosfwd>

namespace metop {

struct RunStatistics {
    std::uintmax_t input_size = 0;
    CaduStatistics cadu;
    bool stopped_by_limit = false;
};

void write_text_statistics(std::ostream& output, const RunStatistics& statistics);
void write_json_statistics(std::ostream& output, const RunStatistics& statistics);
void save_statistics(const std::filesystem::path& directory, const RunStatistics& statistics);

} // namespace metop
