#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace metop {

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::optional<std::uint64_t> max_cadus;
    bool dump_stats = false;
    bool verbose = false;
    bool help = false;
};

// Arguments are UTF-8; the Windows entry point converts the native wide argv.
Options parse_options(std::span<const std::string_view> arguments);
std::string_view usage();

} // namespace metop
