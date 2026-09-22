#include "cadu_reader.h"
#include "ccsds_randomizer.h"
#include "cli.h"
#include "statistics.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int run(std::span<const std::string_view> arguments) {
    metop::Options options;
    try {
        options = metop::parse_options(arguments);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Argument error: " << error.what() << '\n' << metop::usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Cannot read command-line paths: " << error.what() << '\n';
        return 1;
    }
    if (options.help) {
        std::cout << metop::usage();
        return 0;
    }
    try {
        if (!std::filesystem::is_regular_file(options.input)) {
            throw std::runtime_error("Input must be an existing regular file");
        }
        std::ifstream input(options.input, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Cannot open input file");
        }
        // Prevent a user-selected input (including a hard link) being overwritten by stats.
        for (const auto* name : {"stats.txt", "stats.json"}) {
            const auto destination = options.output / name;
            if (std::filesystem::exists(destination)
                && std::filesystem::equivalent(options.input, destination)) {
                throw std::runtime_error("Statistics output would overwrite the input file");
            }
        }
        std::filesystem::create_directories(options.output);
        metop::RunStatistics statistics;
        statistics.input_size = std::filesystem::file_size(options.input);
        metop::CaduReader reader(input);
        while (!options.max_cadus || reader.statistics().cadus_read < *options.max_cadus) {
            auto cadu = reader.next();
            if (!cadu) {
                break;
            }
            ++statistics.vcids_before[cadu->bytes[5] & 0x3f];
            metop::derandomize(std::span(cadu->bytes).subspan<4, metop::cvcdu_size>());
            ++statistics.vcids_after[cadu->bytes[5] & 0x3f];
            ++statistics.versions_after[cadu->bytes[4] >> 6];
            const auto spacecraft = ((cadu->bytes[4] & 0x3f) << 2) | (cadu->bytes[5] >> 6);
            ++statistics.spacecraft_after[static_cast<std::size_t>(spacecraft)];
            if (options.verbose) {
                std::cerr << "CADU " << cadu->index << " offset " << cadu->file_offset << '\n';
            }
        }
        statistics.cadu = reader.statistics();
        statistics.stopped_by_limit = options.max_cadus
            && statistics.cadu.cadus_read == *options.max_cadus;
        metop::save_statistics(options.output, statistics);
        if (options.dump_stats) {
            metop::write_text_statistics(std::cout, statistics);
        } else {
            std::cout << "M2 diagnostics (RS not applied): " << statistics.cadu.cadus_read << " CADUs, "
                      << statistics.cadu.resyncs << " resyncs, "
                      << statistics.cadu.skipped_bytes << " skipped bytes, "
                      << statistics.cadu.trailing_bytes << " trailing bytes"
                      << (statistics.stopped_by_limit ? " (CADU limit reached)" : "") << '\n';
        }
        if (statistics.cadu.asm_failures || statistics.cadu.trailing_bytes) {
            std::cerr << "Framing anomalies detected; inspect stats.txt or stats.json.\n";
        }
        if (statistics.cadu.cadus_read && (!statistics.versions_after[1] || !statistics.vcids_after[9])) {
            std::cerr << "Header sanity check incomplete: inspect version/VCID histograms before packet work.\n";
        }
        if (statistics.cadu.cadus_read == 0) {
            std::cerr << "No complete validated CADUs found.\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    try {
        std::vector<std::string> utf8_arguments;
        for (int i = 1; i < argc; ++i) {
            const auto utf8 = std::filesystem::path(argv[i]).u8string();
            utf8_arguments.emplace_back(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        }
        const std::vector<std::string_view> views(utf8_arguments.begin(), utf8_arguments.end());
        return run(views);
    } catch (const std::exception& error) {
        std::cerr << "Cannot read Windows command line: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    return run(arguments);
}
#endif
