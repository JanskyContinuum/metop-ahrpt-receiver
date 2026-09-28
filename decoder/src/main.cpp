#include "cadu_reader.h"
#include "ccsds_randomizer.h"
#include "cli.h"
#include "statistics.h"
#include "frame_inspector.h"
#include "packet_reassembler.h"
#include "reed_solomon.h"
#include "avhrr.h"

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
        for (const auto* name : {"stats.txt", "stats.json", "vcdu_log.csv", "packet_log.csv", "rs_log.csv"}) {
            const auto destination = options.output / name;
            if (std::filesystem::exists(destination)
                && std::filesystem::equivalent(options.input, destination)) {
                throw std::runtime_error("Statistics output would overwrite the input file");
            }
        }
        // Check every additional output BEFORE opening/truncating any output file.
        for (const auto* name : {"packet_log.csv", "apid_103_packets.bin", "apid_104_packets.bin"}) {
            if (!options.dump_debug && std::string_view(name) != "packet_log.csv") continue;
            const auto destination = options.output / "debug" / name;
            if (std::filesystem::exists(destination) && std::filesystem::equivalent(options.input, destination))
                throw std::runtime_error("AVHRR debug output would overwrite the input file");
        }
        std::filesystem::create_directories(options.output / "debug");
        std::ofstream avhrr_log(options.output / "debug" / "packet_log.csv", std::ios::binary | std::ios::trunc);
        if (!avhrr_log) throw std::runtime_error("Cannot open AVHRR packet log");
        std::array<std::ofstream, 2> avhrr_dumps;
        std::array<std::ostream*, 2> dump_streams{};
        if (options.dump_debug) {
            for (std::size_t i = 0; i < avhrr_dumps.size(); ++i) {
                avhrr_dumps[i].open(options.output / "debug" / ("apid_" + std::to_string(103 + i) + "_packets.bin"),
                                    std::ios::binary | std::ios::trunc);
                if (!avhrr_dumps[i]) throw std::runtime_error("Cannot open AVHRR binary packet dump");
                dump_streams[i] = &avhrr_dumps[i];
            }
        }
        metop::AvhrrInspector avhrr(avhrr_log, !options.no_rs, options.inspect_packets.value_or(20),
                                    dump_streams, options.dump_debug ? &std::cout : nullptr);
        metop::RunStatistics statistics;
        statistics.input_size = std::filesystem::file_size(options.input);
        std::ofstream frame_log;
        std::ofstream packet_log;
        std::ofstream rs_log(options.output / "rs_log.csv", std::ios::binary | std::ios::trunc);
        if (!rs_log) throw std::runtime_error("Cannot open RS log");
        rs_log << "cadu_index,file_offset,rs_status,lane_0,lane_1,lane_2,lane_3\n";
        if (!options.no_rs) statistics.rs.emplace();
        metop::PacketReassembler reassembler;
        std::uint64_t packet_index = 0;
        std::uint64_t previous_asm_failures = 0;
        std::optional<metop::FrameInspector> inspector;
        {
            frame_log.open(options.output / "vcdu_log.csv", std::ios::binary | std::ios::trunc);
            if (!frame_log) throw std::runtime_error("Cannot open VCDU log");
            inspector.emplace(frame_log);
            packet_log.open(options.output / "packet_log.csv", std::ios::binary | std::ios::trunc);
            if (!packet_log) throw std::runtime_error("Cannot open packet log");
            packet_log << "packet_index,spacecraft_id,vcid,replay,start_counter,end_counter,"
                          "apid,seq_flags,seq_count,secondary_header,total_size,rs_status\n";
            if (options.no_rs)
                std::cerr << "--no-rs: packet reassembly uses uncorrected data; parity is ignored.\n";
        }
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
            if (options.verbose)
                std::cerr << "CADU " << cadu->index << " offset " << cadu->file_offset << '\n';
            if (reader.statistics().asm_failures != previous_asm_failures)
                reassembler.invalidate_all();
            previous_asm_failures = reader.statistics().asm_failures;
            if (inspector) {
                auto vcdu = std::span<const std::uint8_t>(cadu->bytes).subspan<4, metop::vcdu_size>();
                std::optional<metop::RsFrameResult> rs_result;
                std::string_view rs_status = "not_applied";
                rs_log << cadu->index << ',' << cadu->file_offset << ',';
                if (!options.no_rs) {
                    rs_result = metop::decode_rs_frame(std::span(cadu->bytes).subspan<4, metop::cvcdu_size>());
                    statistics.rs->observe(*rs_result);
                    rs_status = metop::rs_status_name(rs_result->status);
                    rs_log << rs_status;
                    for (const auto count : rs_result->corrected_symbols) rs_log << ',' << count;
                    rs_log << '\n';
                    if (!rs_log) throw std::runtime_error("Failed to write RS log");
                    if (!rs_result->vcdu) {
                        // Even the stream identity may be corrupt: clear every partial,
                        // preserving counter history for duplicate detection after recovery.
                        reassembler.invalidate_all();
                        continue;
                    }
                    vcdu = *rs_result->vcdu;
                } else {
                    rs_log << "not_applied,,,,\n";
                    if (!rs_log) throw std::runtime_error("Failed to write RS log");
                }
                inspector->inspect(cadu->index, cadu->file_offset, vcdu, rs_status);
                for (const auto& packet : reassembler.consume(vcdu)) {
                    const auto& h = packet.header;
                    avhrr.consume(packet, packet_index);
                    packet_log << packet_index++ << ',' << unsigned(packet.source.spacecraft_id)
                        << ',' << unsigned(packet.source.vcid) << ',' << packet.source.replay
                        << ',' << packet.source.counter << ',' << packet.end_counter
                        << ',' << h.apid << ',' << unsigned(h.sequence_flags) << ',' << h.sequence_count
                        << ',' << h.secondary_header_flag << ',' << packet.bytes.size() << ','
                        << (options.no_rs ? "not_applied" : "rs_checked") << '\n';
                }
                if (!packet_log) throw std::runtime_error("Failed to write packet log");
            }
        }
        statistics.cadu = reader.statistics();
        statistics.stopped_by_limit = options.max_cadus
            && statistics.cadu.cadus_read == *options.max_cadus;
        if (inspector) {
            statistics.frames = inspector->statistics();
            reassembler.finish();
            statistics.packets = reassembler.statistics();
            packet_log.close();
            if (!packet_log) throw std::runtime_error("Failed to close packet log");
            frame_log.close();
            if (!frame_log) throw std::runtime_error("Failed to close VCDU log");
        }
        rs_log.close();
        if (!rs_log) throw std::runtime_error("Failed to close RS log");
        statistics.avhrr = avhrr.statistics();
        avhrr_log.close();
        if (!avhrr_log) throw std::runtime_error("Failed to close AVHRR packet log");
        if (options.dump_debug) {
            for (auto& dump : avhrr_dumps) {
                dump.close();
                if (!dump) throw std::runtime_error("Failed to close AVHRR binary packet dump");
            }
        }
        metop::save_statistics(options.output, statistics);
        if (options.dump_stats) {
            metop::write_text_statistics(std::cout, statistics);
        } else {
            std::cout << (options.no_rs ? "M6 AVHRR packet inspection (RS not applied)" : "M6 AVHRR packet inspection (RS checked)")
                      << ": " << statistics.cadu.cadus_read << " CADUs, "
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
        if (statistics.rs && statistics.rs->uncorrectable_frames)
            std::cerr << "Uncorrectable RS frames rejected; inspect rs_log.csv and statistics.\n";
        if (statistics.frames) {
            const auto& frames = *statistics.frames;
            if (frames.invalid_versions || frames.invalid_fhp || frames.nonzero_mpdu_spare
                || frames.nonzero_signaling_spare || frames.counter_gaps || frames.counter_duplicates
                || frames.counter_backward_or_reset)
                std::cerr << "Frame anomalies detected; inspect vcdu_log.csv and statistics.\n";
            if (!frames.valid_mpdus) {
                std::cerr << "No structurally valid M-PDUs found.\n";
                return 1;
            }
        }
        if (statistics.packets && (statistics.packets->invalid_headers
            || statistics.packets->boundary_mismatches || statistics.packets->truncated_packets))
            std::cerr << "Packet anomalies detected; inspect packet statistics.\n";
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
