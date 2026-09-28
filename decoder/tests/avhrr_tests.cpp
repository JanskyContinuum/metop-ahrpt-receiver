#include "avhrr.h"
#include "cli.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
metop::ReassembledPacket packet(std::uint16_t apid = 103, std::size_t payload_size = 80,
                                std::uint8_t flags = 3, std::uint16_t sequence = 0, bool secondary = true) {
    metop::ReassembledPacket result;
    result.source.spacecraft_id = 11; result.source.vcid = 9;
    result.source.counter = 15; result.end_counter = 16;
    result.bytes.resize(6 + payload_size);
    result.bytes[0] = static_cast<std::uint8_t>((secondary ? 8U : 0U) | (apid >> 8));
    result.bytes[1] = static_cast<std::uint8_t>(apid);
    result.bytes[2] = static_cast<std::uint8_t>((flags << 6) | (sequence >> 8));
    result.bytes[3] = static_cast<std::uint8_t>(sequence);
    result.bytes[4] = static_cast<std::uint8_t>((payload_size - 1) >> 8);
    result.bytes[5] = static_cast<std::uint8_t>(payload_size - 1);
    for (std::size_t i = 0; i < payload_size; ++i) result.bytes[6 + i] = static_cast<std::uint8_t>(i);
    result.header = metop::validate_space_packet(result.bytes);
    return result;
}
std::string bytes(const metop::ReassembledPacket& p) {
    return {reinterpret_cast<const char*>(p.bytes.data()), p.bytes.size()};
}
std::vector<std::string> columns(const std::string& row) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const auto end = row.find(',', start);
        out.push_back(row.substr(start, end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}
std::vector<std::vector<std::string>> rows(const std::ostringstream& csv) {
    std::istringstream input(csv.str());
    std::string line; std::vector<std::vector<std::string>> out;
    std::getline(input, line);
    while (std::getline(input, line)) out.push_back(columns(line));
    return out;
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Missing case");
        const std::string test = argv[1];
        std::ostringstream csv, dump103, dump104, preview;
        metop::AvhrrInspector inspect(csv, true, 2, {&dump103, &dump104}, &preview);
        if (test == "filter") {
            auto p = packet(); p.source.vcid = 10; inspect.consume(p, 0);
            inspect.consume(packet(105), 1);
            inspect.consume(packet(103), 2); inspect.consume(packet(104), 3);
            require(inspect.statistics().selected_packets == 2, "VCID/APID filter failed");
            require(inspect.statistics().apids[0].packets == 1 && inspect.statistics().apids[1].packets == 1,
                    "APID counts not independent");
            require(dump103.str() == bytes(packet(103)) && dump104.str() == bytes(packet(104)), "Wrong dump routing");
            const auto log = rows(csv);
            require(log.size() == 2 && log[0][0] == "2" && log[1][0] == "3", "Global packet indices lost");
        } else if (test == "preview") {
            const auto p = packet();
            inspect.consume(p, 0); inspect.consume(packet(104, 1), 1); inspect.consume(p, 2);
            const auto log = rows(csv);
            require(log.size() == 3 && log[0].size() == 20, "CSV geometry");
            require(log[0][18] == "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F", "Preview includes/skips wrong bytes");
            require(log[0][19] == "404142434445464748494A4B4C4D4E4F", "Wrong tail");
            require(log[1][18] == "00" && log[1][19] == "00", "Short payload padded or out of bounds");
            require(log[2][17] == "0" && log[2][18].empty() && log[2][19].empty(), "Preview limit not global");
            require(inspect.statistics().selected_packets == 3 && inspect.statistics().previewed_packets == 2,
                    "Preview limit changed packet counts");
            require(dump103.str() == bytes(p) + bytes(p) && log[2][16] == "86", "Dump truncated at preview limit");
            require(preview.str().find("AVHRR packet 2") == std::string::npos, "Text preview exceeds limit");
        } else if (test == "lengths") {
            for (const auto size : {1U, 15U, 16U, 63U, 64U, 65U, 65536U}) inspect.consume(packet(103, size), size);
            require(inspect.statistics().apids[0].total_size_histogram.size() == 7, "Lengths filtered by assumed layout");
            require(inspect.statistics().apids[0].total_size_histogram.at(65542) == 1, "Maximum length semantics");
            require(rows(csv).back()[12] == "65535" && rows(csv).back()[13] == "65536", "Data length field semantics");
        } else if (test == "headers") {
            for (std::uint8_t flag = 0; flag < 4; ++flag)
                inspect.consume(packet(103, 2, flag, static_cast<std::uint16_t>(16380 + flag), flag % 2 != 0), flag);
            auto p = packet(104, 2, 3, 0); p.bytes[0] |= 0x10;
            p.header.apid = 103; // Cached metadata must not override validated wire bytes.
            inspect.consume(p, 4);
            const auto& a = inspect.statistics().apids[0];
            require(a.sequence_flags == std::array<std::uint64_t, 4>{1,1,1,1}, "Sequence flags lost");
            require(a.secondary_header_flags == std::array<std::uint64_t, 2>{2,2}, "Secondary flag lost");
            require(a.sequence_counts.at(16383) == 1 && inspect.statistics().apids[1].sequence_counts.at(0) == 1,
                    "14-bit sequence counters changed or APIDs mixed");
            require(inspect.statistics().apids[1].packet_types[1] == 1, "Unusual packet type silently filtered");
        } else if (test == "malformed") {
            for (unsigned kind = 0; kind < 4; ++kind) {
                auto p = packet();
                if (kind == 0) p.bytes.resize(5);
                if (kind == 1) p.bytes.pop_back();
                if (kind == 2) p.bytes.push_back(0);
                if (kind == 3) p.bytes[0] |= 0x20;
                bool threw = false;
                try { inspect.consume(p, 0); } catch (const std::invalid_argument&) { threw = true; }
                require(threw, "Malformed packet accepted");
            }
            require(inspect.statistics().selected_packets == 0 && dump103.str().empty() && rows(csv).empty(),
                    "Malformed packet produced output");
        } else if (test == "no_dumps") {
            std::ostringstream log, text;
            metop::AvhrrInspector limited(log, false, 0, {}, &text);
            limited.consume(packet(), 0);
            const auto row = rows(log)[0];
            require(row[15] == "not_applied" && row[16].empty() && row[17] == "0" && row[18].empty(),
                    "Bypass/disabled-preview metadata incorrect");
            require(text.str().empty() && limited.statistics().apids[0].packets == 1, "Zero preview suppresses count");
        } else if (test == "write_errors") {
            dump103.setstate(std::ios::badbit);
            bool threw = false;
            try { inspect.consume(packet(), 0); } catch (const std::runtime_error&) { threw = true; }
            require(threw, "Binary write error ignored");
            std::ostringstream log;
            metop::AvhrrInspector broken(log, true);
            log.setstate(std::ios::badbit); threw = false;
            try { broken.consume(packet(), 0); } catch (const std::runtime_error&) { threw = true; }
            require(threw, "CSV write error ignored");
            threw = false;
            try { metop::AvhrrInspector invalid(log, true); } catch (const std::runtime_error&) { threw = true; }
            require(threw, "CSV header write error ignored");
        } else if (test == "options") {
            const auto defaults = metop::parse_options(std::array<std::string_view,3>{"a.cadu", "--out", "out"});
            require(!defaults.dump_debug && defaults.inspect_packets.value_or(20) == 20, "Wrong defaults");
            const auto zero = metop::parse_options(std::array<std::string_view,6>{"a.cadu", "--out", "out", "--dump-debug", "--inspect-packets", "0"});
            require(zero.dump_debug && zero.inspect_packets == 0, "Zero preview rejected");
            for (const auto invalid : {"-1", "2x", "18446744073709551616"}) {
                bool threw = false;
                try { (void)metop::parse_options(std::array<std::string_view,5>{"a.cadu","--out","out","--inspect-packets",invalid}); }
                catch (const std::invalid_argument&) { threw = true; }
                require(threw, "Invalid preview limit accepted");
            }
            for (const auto& arguments : std::vector<std::vector<std::string_view>>{
                    {"a.cadu","--out","out","--inspect-packets"},
                    {"a.cadu","--out","out","--inspect-packets","0","--inspect-packets","1"},
                    {"a.cadu","--out","out","--dump-debug","--dump-debug"}}) {
                bool threw = false;
                try { (void)metop::parse_options(arguments); } catch (const std::invalid_argument&) { threw = true; }
                require(threw, "Missing/duplicate option accepted");
            }
        } else throw std::runtime_error("Unknown case");
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
