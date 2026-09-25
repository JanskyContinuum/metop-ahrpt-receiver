#include "cli.h"

#include <charconv>
#include <stdexcept>
#include <string>

namespace metop {

std::string_view usage() {
    return "Usage: metop_decoder <input.cadu> --out <directory> [options]\n"
           "M2: CADU derandomization and header diagnostics; RS not applied.\n"
           "  --no-rs        Enable M4 packet reassembly, bypassing RS explicitly\n"
           "  --max-cadus N  Stop after N accepted CADUs (positive integer)\n"
           "  --dump-stats   Print full statistics; stats.txt/json are always saved\n"
           "  --verbose      Log each accepted CADU index and file offset\n"
           "  --help         Show this help\n";
}

Options parse_options(std::span<const std::string_view> arguments) {
    Options options;
    const auto path = [](std::string_view text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    };
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto argument = arguments[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 == arguments.size() || arguments[i + 1].empty()
                || arguments[i + 1].starts_with("--")) {
                throw std::invalid_argument("Missing value for " + std::string(argument));
            }
            return arguments[++i];
        };
        auto duplicate = [&](bool present) {
            if (present) {
                throw std::invalid_argument("Duplicate option: " + std::string(argument));
            }
        };
        if (argument == "--help") {
            duplicate(options.help);
            options.help = true;
        } else if (argument == "--out") {
            duplicate(!options.output.empty());
            options.output = path(value());
        } else if (argument == "--max-cadus") {
            duplicate(options.max_cadus.has_value());
            const auto text = value();
            std::uint64_t count = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
            if (error != std::errc{} || end != text.data() + text.size() || count == 0) {
                throw std::invalid_argument("--max-cadus requires a positive 64-bit integer");
            }
            options.max_cadus = count;
        } else if (argument == "--no-rs") {
            duplicate(options.no_rs);
            options.no_rs = true;
        } else if (argument == "--dump-stats") {
            duplicate(options.dump_stats);
            options.dump_stats = true;
        } else if (argument == "--verbose") {
            duplicate(options.verbose);
            options.verbose = true;
        } else if (argument.empty() || argument.starts_with('-')) {
            throw std::invalid_argument("Unknown option: " + std::string(argument));
        } else if (!options.input.empty()) {
            throw std::invalid_argument("Expected exactly one input file");
        } else {
            options.input = path(argument);
        }
    }
    if (!options.help && (options.input.empty() || options.output.empty())) {
        throw std::invalid_argument("An input file and --out directory are required");
    }
    return options;
}

} // namespace metop
