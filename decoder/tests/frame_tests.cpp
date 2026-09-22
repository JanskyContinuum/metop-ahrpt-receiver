#include "frame_inspector.h"
#include "mpdu.h"
#include "vcdu.h"

#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<typename F> void rejects(F action) {
    bool threw = false;
    try { action(); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "Bad length was accepted");
}
}

int main(int argc, char* argv[]) {
    try {
        require(argc == 2, "Expected test name");
        const std::string_view test = argv[1];
        std::array<std::uint8_t, metop::vcdu_size> bytes{};
        if (test == "vcdu_header") {
            bytes[0] = 0x6a; bytes[1] = 0xd5;
            bytes[2] = 0xab; bytes[3] = 0xcd; bytes[4] = 0xef;
            bytes[5] = 0xd5; bytes[6] = 0x12; bytes[7] = 0x34;
            bytes[8] = 0x07; bytes[9] = 0xff; bytes.back() = 0x9a;
            const auto v = metop::parse_vcdu(bytes);
            require(v.header.version == 1 && v.header.spacecraft_id == 171 && v.header.vcid == 21,
                    "Incorrect version/SCID/VCID masks");
            require(v.header.counter == 0xabcdef && v.header.replay && v.header.signaling_spare == 0x55,
                    "Incorrect counter/signaling fields");
            require(v.insert_zone == std::array<std::uint8_t, 2>{0x12, 0x34}, "Insert zone lost");
            require(v.mpdu.size() == 884 && v.mpdu.data() == bytes.data() + 8 && v.mpdu.back() == 0x9a,
                    "Wrong M-PDU geometry");
        } else if (test == "vcdu_lengths") {
            std::array<std::uint8_t, 893> storage{};
            for (const auto n : {0U, 5U, 6U, 7U, 891U, 893U})
                rejects([&] { (void)metop::parse_vcdu(std::span(storage).first(n)); });
        } else if (test == "mpdu_lengths") {
            for (const auto n : {0U, 1U, 2U, 883U, 885U})
                rejects([&] { (void)metop::parse_mpdu(std::span(bytes).first(n)); });
        } else if (test.starts_with("fhp_")) {
            const auto value = static_cast<std::uint16_t>(std::stoul(std::string(test.substr(4))));
            bytes[0] = static_cast<std::uint8_t>(0xf8U | (value >> 8));
            bytes[1] = static_cast<std::uint8_t>(value);
            bytes[2] = 0x12; bytes[883] = 0x34;
            const auto m = metop::parse_mpdu(std::span(bytes).first<884>());
            const auto expected = value < 882 ? metop::FirstHeaderKind::packet_start
                : value == 2046 ? metop::FirstHeaderKind::idle
                : value == 2047 ? metop::FirstHeaderKind::continuation : metop::FirstHeaderKind::invalid;
            require(m.first_header_pointer == value && m.reserved_spare == 31 && m.kind == expected,
                    "Incorrect FHP/spare classification");
            require(m.packet_zone.size() == 882 && m.packet_zone.data() == bytes.data() + 2
                    && m.packet_zone.front() == 0x12 && m.packet_zone.back() == 0x34,
                    "Wrong packet zone bounds");
        } else if (test == "counter_wrap") {
            metop::VcduCounterTracker tracker;
            metop::VcduHeader h{1, 11, 9, 0xffffff, false, 0};
            require(tracker.observe(h).status == metop::Continuity::first, "Missing initial state");
            h.counter = 0;
            const auto obs = tracker.observe(h);
            require(obs.status == metop::Continuity::contiguous && obs.previous == 0xffffff, "24-bit wrap is a gap");
        } else if (test == "counter_events") {
            metop::VcduCounterTracker tracker;
            metop::VcduHeader h{1, 11, 9, 10, false, 0};
            (void)tracker.observe(h);
            h.counter = 12;
            require(tracker.observe(h).status == metop::Continuity::gap, "Gap not detected");
            require(tracker.observe(h).status == metop::Continuity::duplicate, "Duplicate not detected");
            h.counter = 9;
            require(tracker.observe(h).status == metop::Continuity::backward_or_reset, "Backward/reset not detected");
            h.counter = 10;
            require(tracker.observe(h).status == metop::Continuity::contiguous, "Baseline not recovered");
        } else if (test == "counter_streams") {
            metop::VcduCounterTracker tracker;
            metop::VcduHeader h{1, 11, 9, 10, false, 0};
            (void)tracker.observe(h);
            h.vcid = 10; h.counter = 100;
            require(tracker.observe(h).status == metop::Continuity::first, "VCIDs share state");
            h.vcid = 9; h.spacecraft_id = 12;
            require(tracker.observe(h).status == metop::Continuity::first, "Spacecraft share state");
            h.spacecraft_id = 11; h.replay = true;
            require(tracker.observe(h).status == metop::Continuity::first, "Replay shares live state");
            h.replay = false; h.counter = 11;
            require(tracker.observe(h).status == metop::Continuity::contiguous, "Interleaved VC state lost");
        } else if (test == "inspector") {
            std::ostringstream log;
            metop::FrameInspector inspector(log);
            bytes[0] = 0x42; bytes[1] = 0xc9; bytes[4] = 1;
            bytes[6] = 0x12; bytes[7] = 0x34;
            inspector.inspect(0, 0, bytes); // Valid packet boundary.
            bytes[4] = 3; bytes[8] = 0xff; bytes[9] = 0xff;
            inspector.inspect(1, 1024, bytes); // Counter gap; FFFF continuation is valid.
            bytes[4] = 4; bytes[8] = 0x03; bytes[9] = 0xe8;
            inspector.inspect(2, 2048, bytes); // Invalid FHP=1000.
            bytes[1] = 0xff;
            inspector.inspect(3, 3072, bytes); // VCID63 must not parse the invalid FHP.
            bytes[0] = 0x02;
            inspector.inspect(4, 4096, bytes); // Unsupported version, never idle or M-PDU.
            const auto& s = inspector.statistics();
            require(s.headers == 5 && s.invalid_versions == 1 && s.idle_frames == 1
                    && s.valid_mpdus == 2 && s.invalid_fhp == 1 && s.nonzero_mpdu_spare == 1
                    && s.counter_gaps == 1 && s.continuation_zones == 1, "Inspector totals wrong");
            require(log.str().find("1,1024,not_applied,1,11,9,3,0,0,18,52,1,gap,2047,31,continuation,uncorrected")
                    != std::string::npos, "CSV lost offsets, insert bytes, or gap/FHP evidence");
            std::istringstream lines(log.str());
            std::string line;
            while (std::getline(lines, line)) {
                std::size_t commas = 0;
                for (char c : line) if (c == ',') ++commas;
                require(commas == 16, "CSV columns are inconsistent");
            }
        } else throw std::runtime_error("Unknown test");
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
