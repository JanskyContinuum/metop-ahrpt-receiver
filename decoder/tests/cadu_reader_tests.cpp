#include "cadu_reader.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Entirely synthetic fixtures; no satellite data or external implementation used.
std::string frame(char payload) {
    std::string result(metop::cadu_size, payload);
    for (std::size_t i = 0; i < metop::attached_sync_marker.size(); ++i) {
        result[i] = static_cast<char>(metop::attached_sync_marker[i]);
    }
    return result;
}

using ExpectedFrame = std::pair<std::uint64_t, std::string>;

metop::CaduStatistics check(const std::string& data,
                            const std::vector<ExpectedFrame>& expected,
                            std::uint64_t skipped, std::uint64_t trailing,
                            std::uint64_t resyncs = 0, std::uint64_t failures = 0) {
    std::istringstream input(data, std::ios::in | std::ios::binary);
    metop::CaduReader reader(input);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto actual = reader.next();
        require(actual.has_value(), "Missing expected CADU");
        require(actual->index == i, "Wrong CADU index");
        require(actual->file_offset == expected[i].first, "Wrong original file offset");
        require(std::equal(actual->bytes.begin(), actual->bytes.end(), expected[i].second.begin(),
                           [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); }),
                "CADU bytes changed or wrong frame emitted");
    }
    require(!reader.next(), "Unexpected additional CADU");
    require(!reader.next(), "EOF is not stable");
    const auto statistics = reader.statistics();
    require(statistics.cadus_read == expected.size(), "Wrong CADU count");
    require(statistics.valid_asm == expected.size(), "Wrong valid ASM count");
    require(statistics.skipped_bytes == skipped, "Wrong skipped-byte count");
    require(statistics.trailing_bytes == trailing, "Wrong trailing-byte count");
    require(statistics.resyncs == resyncs, "Wrong resync count");
    require(statistics.asm_failures == failures, "Wrong alignment-failure count");
    require(statistics.reached_eof, "EOF not reported");
    require(statistics.bytes_consumed == data.size(), "Input bytes not fully accounted for");
    require(statistics.cadus_read * metop::cadu_size + skipped + trailing == data.size(),
            "Byte conservation failed");
    return statistics;
}

} // namespace

int main(int argc, char* argv[]) {
    const auto a = frame('a');
    const auto b = frame('b');
    const auto c = frame('c');
    const auto d = frame('d');
    const auto e = frame('e');
    const std::map<std::string, std::function<void()>> tests{
        {"empty", [&] { check("", {}, 0, 0); }},
        {"single", [&] { check(a, {{0, a}}, 0, 0); }},
        {"aligned", [&] {
            std::string data;
            std::vector<ExpectedFrame> expected;
            for (std::uint64_t i = 0; i < 200; ++i) {
                const auto f = frame(static_cast<char>(i));
                expected.emplace_back(i * metop::cadu_size, f);
                data += f;
            }
            check(data, expected, 0, 0);
        }},
        {"trailing", [&] { check(a + b + "end", {{0, a}, {1024, b}}, 0, 3); }},
        {"truncated", [&] {
            for (const auto length : {1U, 3U, 4U, 100U, 1023U}) {
                check(a.substr(0, length), {}, 0, length);
            }
        }},
        {"leading_garbage", [&] { check("garbage" + a + b, {{7, a}, {1031, b}}, 7, 0, 1, 1); }},
        {"corrupt_marker", [&] {
            auto corrupt = b;
            corrupt[0] = 0;
            check(a + corrupt + c + d, {{0, a}, {2048, c}, {3072, d}}, 1024, 0, 1, 1);
        }},
        {"inserted_byte", [&] { check(a + "x" + b + c, {{0, a}, {1025, b}, {2049, c}}, 1, 0, 1, 1); }},
        {"deleted_byte", [&] {
            check(a + b.substr(1) + c + d, {{0, a}, {2047, c}, {3071, d}}, 1023, 0, 1, 1);
        }},
        {"false_marker", [&] {
            const auto stats = check("junk" + a + "bad" + b + c,
                                     {{1031, b}, {2055, c}}, 1031, 0, 1, 1);
            require(stats.rejected_candidates == 1, "False marker not rejected");
        }},
        {"unconfirmed", [&] {
            const auto stats = check("junk" + a, {}, 4, 1024, 0, 1);
            require(stats.unconfirmed_candidates == 1, "EOF candidate not reported");
            check(a + "junk" + b, {{0, a}}, 4, 1024, 0, 1);
        }},
        {"large_garbage", [&] { check(std::string(200000, 'x'), {}, 198977, 1023, 0, 1); }},
        {"multiple_resync", [&] {
            check(a + "xx" + b + c + "xx" + d + e,
                  {{0, a}, {1026, b}, {2050, c}, {3076, d}, {4100, e}}, 4, 0, 2, 2);
        }},
        {"truncated_successor", [&] {
            check("x" + a + b.substr(0, 4), {{1, a}}, 1, 4, 1, 1);
        }},
        {"stream_error", [&] {
            std::istringstream input(a);
            input.setstate(std::ios::badbit);
            metop::CaduReader reader(input);
            bool threw = false;
            try {
                (void)reader.next();
            } catch (const std::runtime_error&) {
                threw = true;
            }
            require(threw, "I/O error was silently treated as EOF");
            require(!reader.statistics().reached_eof, "I/O error reported as EOF");
        }}
    };
    try {
        require(argc == 2, "Expected a test case name");
        const auto test = tests.find(argv[1]);
        require(test != tests.end(), "Unknown test case");
        test->second();
        std::cout << "PASS " << argv[1] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
