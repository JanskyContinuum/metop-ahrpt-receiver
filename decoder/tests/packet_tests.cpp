#include "packet_reassembler.h"
#include "ccsds_randomizer.h"
#include "mpdu.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using Bytes = std::vector<std::uint8_t>;
using Frame = std::array<std::uint8_t, metop::vcdu_size>;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F action) {
    bool threw = false;
    try { action(); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "Expected invalid_argument");
}
Bytes packet(std::size_t size, std::uint16_t apid = 103) {
    require(size >= 7 && size <= metop::max_space_packet_size, "Bad fixture size");
    Bytes b(size);
    for (std::size_t i = 0; i < size; ++i) b[i] = static_cast<std::uint8_t>((i * 37 + apid) & 255);
    b[0] = static_cast<std::uint8_t>(0x08U | (apid >> 8));
    b[1] = static_cast<std::uint8_t>(apid);
    b[2] = 0xc1; b[3] = 0x23;
    b[4] = static_cast<std::uint8_t>((size - 7) >> 8);
    b[5] = static_cast<std::uint8_t>(size - 7);
    return b;
}
Bytes slice(const Bytes& b, std::size_t offset, std::size_t count) {
    const auto s = std::span(b).subspan(offset, count);
    return {s.begin(), s.end()};
}
Bytes join(Bytes a, const Bytes& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
Frame frame(std::uint32_t counter, std::uint16_t fhp, const Bytes& zone,
            std::uint8_t vcid = 9, std::uint8_t scid = 11, bool replay = false) {
    require(zone.size() == 882, "Fixture packet zone must be 882 bytes");
    Frame f{};
    f[0] = static_cast<std::uint8_t>(0x40U | (scid >> 2));
    f[1] = static_cast<std::uint8_t>(((scid & 3U) << 6) | vcid);
    f[2] = static_cast<std::uint8_t>(counter >> 16);
    f[3] = static_cast<std::uint8_t>(counter >> 8);
    f[4] = static_cast<std::uint8_t>(counter);
    f[5] = replay ? 0x80 : 0;
    f[8] = static_cast<std::uint8_t>(fhp >> 8);
    f[9] = static_cast<std::uint8_t>(fhp);
    std::copy(zone.begin(), zone.end(), f.begin() + 10);
    return f;
}
void equal(const std::vector<metop::ReassembledPacket>& result, const std::vector<Bytes>& expected) {
    require(result.size() == expected.size(), "Unexpected packet count");
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(result[i].bytes == expected[i], "Reassembled bytes differ");
}
std::vector<Frame> three_frames() {
    const auto p = packet(2000);
    return {frame(10, 0, slice(p, 0, 882)),
            frame(11, 0x7ff, slice(p, 882, 882)),
            frame(12, 236, join(slice(p, 1764, 236), packet(646, 104)))};
}
}

int main(int argc, char* argv[]) {
    try {
        require(argc >= 2, "Expected case");
        const std::string_view test = argv[1];
        metop::PacketReassembler r;
        if (test == "header") {
            const Bytes b{0x1b, 0x45, 0x92, 0x34, 0x12, 0x34};
            const auto h = metop::parse_space_packet_header(b);
            require(h.version == 0 && h.type && h.secondary_header_flag && h.apid == 0x345
                && h.sequence_flags == 2 && h.sequence_count == 0x1234
                && h.data_length_field == 0x1234 && h.packet_data_bytes == 4661
                && h.total_packet_bytes == 4667, "Header masks/length wrong");
        } else if (test == "lengths") {
            for (const auto size : {7U, 8U, 255U, 882U, 2000U, 65542U}) {
                auto b = packet(size);
                require(metop::validate_space_packet(b).total_packet_bytes == size, "Length semantics wrong");
                b.pop_back();
                rejects([&] { (void)metop::validate_space_packet(b); });
                b.push_back(0); b.push_back(0);
                rejects([&] { (void)metop::validate_space_packet(b); });
            }
            for (std::size_t n = 0; n < 6; ++n)
                rejects([&] { (void)metop::parse_space_packet_header(Bytes(n)); });
            auto bad = packet(7); bad[0] |= 0x20;
            rejects([&] { (void)metop::parse_space_packet_header(bad); });
        } else if (test == "three_mpdus") {
            const auto frames = three_frames();
            equal(r.consume(frames[0]), {});
            equal(r.consume(frames[1]), {});
            const auto result = r.consume(frames[2]);
            equal(result, {packet(2000), packet(646, 104)});
            require(result[0].source.counter == 10 && result[0].end_counter == 12, "Provenance lost");
            r.finish();
            require(r.statistics().truncated_packets == 0, "Complete packet reported truncated");
        } else if (test == "boundaries") {
            equal(r.consume(frame(0, 0, join(packet(7), packet(875, 104)))), {packet(7), packet(875, 104)});
            equal(r.consume(frame(1, 0, packet(882))), {packet(882)});
            Bytes many;
            for (int i = 0; i < 126; ++i) many = join(std::move(many), packet(7));
            require(r.consume(frame(2, 0, many)).size() == 126, "Multiple minimum packets lost");
        } else if (test == "split_headers") {
            for (std::size_t n = 1; n <= 5; ++n) {
                metop::PacketReassembler split;
                const auto p = packet(882 + n);
                equal(split.consume(frame(0, static_cast<std::uint16_t>(882 - n),
                    join(Bytes(882 - n, 0xff), slice(p, 0, n)))), {});
                equal(split.consume(frame(1, 0x7ff, slice(p, n, 882))), {p});
                require(split.statistics().orphan_continuation_bytes == 882 - n, "Prefix parsed as header");
            }
        } else if (test == "split_prefix") {
            for (std::size_t n = 1; n <= 5; ++n) {
                metop::PacketReassembler split;
                const auto p = packet(10);
                equal(split.consume(frame(0, static_cast<std::uint16_t>(882 - n),
                    join(Bytes(882 - n, 0xff), slice(p, 0, n)))), {});
                equal(split.consume(frame(1, static_cast<std::uint16_t>(10 - n),
                    join(slice(p, n, 10 - n), packet(872 + n)))), {p, packet(872 + n)});
            }
        } else if (test == "segmented_stream") {
            std::vector<Bytes> expected;
            std::vector<std::size_t> starts;
            Bytes stream;
            for (std::size_t i = 0; i < 100; ++i) {
                starts.push_back(stream.size());
                expected.push_back(packet(7 + ((i * 173) % 4096), static_cast<std::uint16_t>(100 + i)));
                stream = join(std::move(stream), expected.back());
            }
            starts.push_back(stream.size());
            auto padding = 882 - stream.size() % 882;
            if (padding < 7) padding += 882;
            stream = join(std::move(stream), packet(padding, 2047));
            std::vector<metop::ReassembledPacket> actual;
            for (std::size_t offset = 0; offset < stream.size(); offset += 882) {
                auto fhp = std::uint16_t{0x7ff};
                for (const auto start : starts)
                    if (start >= offset && start < offset + 882) {
                        fhp = static_cast<std::uint16_t>(start - offset);
                        break;
                    }
                auto batch = r.consume(frame(static_cast<std::uint32_t>(offset / 882),
                    fhp, slice(stream, offset, 882)));
                for (auto& p : batch) actual.push_back(std::move(p));
            }
            equal(actual, expected);
            r.finish();
            require(r.statistics().invalid_headers == 0 && r.statistics().boundary_mismatches == 0
                && r.statistics().truncated_packets == 0 && r.statistics().idle_packets == 1,
                "Valid mixed-size stream reported corrupt");
        } else if (test == "orphan") {
            equal(r.consume(frame(0, 0x7ff, packet(882))), {});
            equal(r.consume(frame(1, 100, join(Bytes(100, 0), packet(782)))), {packet(782)});
            require(r.statistics().orphan_continuation_bytes == 982, "Orphan bytes not counted");
        } else if (test == "idle_between") {
            const auto p = packet(1764);
            equal(r.consume(frame(0, 0, slice(p, 0, 882))), {});
            equal(r.consume(frame(1, 0x7fe, packet(882))), {});
            equal(r.consume(frame(17, 0, packet(882), 63)), {});
            equal(r.consume(frame(2, 0x7ff, slice(p, 882, 882))), {p});
            require(r.statistics().idle_zones == 1, "Idle zone not counted");
        } else if (test == "idle_packet") {
            equal(r.consume(frame(0, 0, join(packet(100, 2047), packet(782)))), {packet(782)});
            require(r.statistics().idle_packets == 1 && r.statistics().reconstructed == 1, "Idle packet delivered");
        } else if (test == "gap" || test == "gap_idle") {
            const auto p = packet(2000);
            equal(r.consume(frame(0, 0, slice(p, 0, 882))), {});
            equal(r.consume(frame(2, test == "gap_idle" ? 0x7fe : 0x7ff, slice(p, 882, 882))), {});
            equal(r.consume(frame(3, 236, join(slice(p, 1764, 236), packet(646)))), {packet(646)});
            require(r.statistics().truncated_packets == 1, "Gap did not invalidate partial");
        } else if (test == "gap_boundary") {
            equal(r.consume(frame(0, 0, slice(packet(2000), 0, 882))), {});
            equal(r.consume(frame(2, 0, packet(882))), {packet(882)});
            require(r.statistics().truncated_packets == 1, "Gap boundary recovery failed");
        } else if (test == "counter_wrap") {
            const auto p = packet(1764);
            equal(r.consume(frame(0xffffff, 0, slice(p, 0, 882))), {});
            equal(r.consume(frame(0, 0x7ff, slice(p, 882, 882))), {p});
        } else if (test == "streams") {
            const auto p = packet(1764);
            equal(r.consume(frame(0, 0, slice(p, 0, 882), 9)), {});
            equal(r.consume(frame(20, 0, slice(p, 0, 882), 10)), {});
            equal(r.consume(frame(40, 0, slice(p, 0, 882), 9, 12)), {});
            equal(r.consume(frame(60, 0, slice(p, 0, 882), 9, 11, true)), {});
            equal(r.consume(frame(22, 0x7ff, slice(p, 882, 882), 10)), {});
            equal(r.consume(frame(1, 0x7ff, slice(p, 882, 882), 9)), {p});
            equal(r.consume(frame(41, 0x7ff, slice(p, 882, 882), 9, 12)), {p});
            equal(r.consume(frame(61, 0x7ff, slice(p, 882, 882), 9, 11, true)), {p});
        } else if (test == "duplicate") {
            equal(r.consume(frame(0, 0, packet(882))), {packet(882)});
            equal(r.consume(frame(0, 0, packet(882))), {});
            const auto p = packet(1764);
            equal(r.consume(frame(1, 0, slice(p, 0, 882))), {});
            equal(r.consume(frame(1, 0, slice(p, 0, 882))), {});
            equal(r.consume(frame(2, 0x7ff, slice(p, 882, 882))), {});
            equal(r.consume(frame(3, 0, packet(882))), {packet(882)});
            require(r.statistics().duplicate_frames == 2 && r.statistics().truncated_packets == 1,
                "Duplicate policy failed");
        } else if (test.starts_with("duplicate_after_")) {
            const auto frames = three_frames();
            equal(r.consume(frames[0]), {});
            equal(r.consume(frames[1]), {});
            equal(r.consume(frames[2]), {packet(2000), packet(646, 104)});
            // Another VC's partial must still be discarded, while counter
            // history for already delivered packets survives invalidation.
            equal(r.consume(frame(30, 0, slice(packet(1764), 0, 882), 10)), {});
            if (test == "duplicate_after_invalid_version") {
                auto bad = frames[2]; bad[0] &= 0x3f;
                equal(r.consume(bad), {});
            } else if (test == "duplicate_after_invalid_size") {
                equal(r.consume(Bytes(891)), {});
            } else if (test == "duplicate_after_alignment_loss") {
                r.invalidate_all();
            } else throw std::runtime_error("Unknown invalidation case");
            equal(r.consume(frames[2]), {});
            require(r.statistics().reconstructed == 2 && r.statistics().duplicate_frames == 1,
                "Invalidation forgot an already delivered frame");
            equal(r.consume(frame(31, 0x7ff, slice(packet(1764), 882, 882), 10)), {});
            equal(r.consume(frame(13, 0, packet(882, 105))), {packet(882, 105)});
            require(r.statistics().truncated_packets == 1 && r.statistics().reconstructed == 3,
                "Invalidation retained a partial or blocked subsequent recovery");
        } else if (test == "backward") {
            equal(r.consume(frame(10, 0, slice(packet(2000), 0, 882))), {});
            equal(r.consume(frame(9, 0, packet(882))), {packet(882)});
            require(r.statistics().truncated_packets == 1, "Backward counter kept partial");
        } else if (test == "mismatch") {
            for (const auto total : {900U, 1000U}) {
                metop::PacketReassembler mismatch;
                const auto p = packet(total);
                equal(mismatch.consume(frame(0, 0, slice(p, 0, 882))), {});
                // Advertised next start is 100: expected remainder is either 18 or 118.
                equal(mismatch.consume(frame(1, 100, join(Bytes(100, 0x55), packet(782)))), {packet(782)});
                require(mismatch.statistics().boundary_mismatches == 1
                    && mismatch.statistics().truncated_packets == 1, "Conflicting length accepted");
            }
            equal(r.consume(frame(0, 0, slice(packet(1764), 0, 882))), {});
            equal(r.consume(frame(1, 0, packet(882))), {packet(882)});
            require(r.statistics().boundary_mismatches == 1, "FHP zero did not reject partial");
        } else if (test == "continuation_overrun") {
            equal(r.consume(frame(0, 0, slice(packet(900), 0, 882))), {});
            equal(r.consume(frame(1, 0x7ff, packet(882))), {});
            require(r.statistics().boundary_mismatches == 1 && r.statistics().reconstructed == 0,
                "Continuation tail silently ignored");
        } else if (test == "invalid_header") {
            auto bad = packet(100); bad[0] = 0xe0;
            equal(r.consume(frame(0, 0, join(bad, packet(782)))), {});
            equal(r.consume(frame(1, 0x7ff, packet(882))), {});
            equal(r.consume(frame(2, 0, packet(882))), {packet(882)});
            require(r.statistics().invalid_headers == 1, "Invalid version not counted");
        } else if (test == "invalid_frame") {
            const auto p = packet(1764);
            equal(r.consume(frame(0, 0, slice(p, 0, 882))), {});
            equal(r.consume(frame(1, 882, slice(p, 882, 882))), {});
            equal(r.consume(frame(2, 0x7ff, slice(p, 882, 882))), {});
            equal(r.consume(frame(3, 0, slice(p, 0, 882))), {});
            auto bad = frame(4, 0x7ff, slice(p, 882, 882)); bad[0] &= 0x3f;
            equal(r.consume(bad), {});
            equal(r.consume(frame(5, 0x7ff, slice(p, 882, 882))), {});
            equal(r.consume(frame(6, 0, packet(882))), {packet(882)});
            equal(r.consume(Bytes(891)), {});
            require(r.statistics().invalid_frames == 3 && r.statistics().truncated_packets == 2,
                "Invalid frame state not cleared");
        } else if (test == "finish") {
            equal(r.consume(frame(0, 0, slice(packet(2000), 0, 882))), {});
            equal(r.consume(frame(0, 881, Bytes(882, 0), 10)), {});
            r.finish(); r.finish();
            require(r.statistics().truncated_packets == 2 && r.statistics().discarded_partial_bytes == 883,
                "EOF partial accounting wrong");
            equal(r.consume(frame(1, 0x7ff, packet(882))), {});
        } else if (test == "maximum") {
            const auto p = packet(65542);
            std::size_t offset = 0;
            std::uint32_t counter = 0;
            while (p.size() - offset >= 882) {
                equal(r.consume(frame(counter++, offset == 0 ? 0 : 0x7ff, slice(p, offset, 882))), {});
                offset += 882;
            }
            const auto tail = p.size() - offset;
            equal(r.consume(frame(counter, static_cast<std::uint16_t>(tail),
                join(slice(p, offset, tail), packet(882 - tail, 2047)))), {p});
            require(r.statistics().idle_packets == 1 && r.statistics().invalid_headers == 0,
                "FFFF length rejected or overflowed");
        } else if (test.starts_with("fixture")) {
            require(argc == 3, "Fixture output path required");
            std::ofstream file(std::filesystem::path(argv[2]), std::ios::binary);
            auto frames = three_frames();
            if (test == "fixture_gap") { frames[1][4] = 12; frames[2][4] = 13; }
            if (test == "fixture_partial") frames.pop_back();
            if (test == "fixture_duplicate_invalid" || test == "fixture_duplicate_resync") {
                const auto last = frames.back();
                if (test == "fixture_duplicate_invalid") {
                    auto bad = last; bad[0] &= 0x3f;
                    frames.push_back(bad);
                }
                frames.push_back(last);
                frames.push_back(frame(13, 0, packet(882, 105)));
            }
            std::size_t index = 0;
            for (const auto& f : frames) {
                if ((test == "fixture_resync" && index == 1)
                    || (test == "fixture_duplicate_resync" && index == 3)) file.put('x');
                ++index;
                std::array<std::uint8_t, 1024> cadu{0x1a, 0xcf, 0xfc, 0x1d};
                std::copy(f.begin(), f.end(), cadu.begin() + 4);
                metop::derandomize(std::span(cadu).subspan<4, 1020>());
                file.write(reinterpret_cast<const char*>(cadu.data()), static_cast<std::streamsize>(cadu.size()));
            }
            file.close();
            require(bool(file), "Fixture write failed");
        } else throw std::runtime_error("Unknown case");
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
