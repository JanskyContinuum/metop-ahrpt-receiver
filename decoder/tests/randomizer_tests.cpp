#include "cadu_reader.h"
#include "ccsds_randomizer.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char* argv[]) {
    try {
        require(argc == 2, "Expected test name");
        const std::string_view test = argv[1];
        std::array<std::uint8_t, metop::cvcdu_size> data{};
        if (test == "prefix") {
            metop::derandomize(data);
            // First 40 transmitted bits, CCSDS 131.0-B-5 section 10.4.4.
            constexpr std::array<std::uint8_t, 5> expected{0xff, 0x48, 0x0e, 0xc0, 0x9a};
            require(std::equal(expected.begin(), expected.end(), data.begin()), "Incorrect PN prefix");
        } else if (test == "round_trip") {
            for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i);
            const auto original = data;
            metop::derandomize(data);
            require(data != original, "First XOR had no effect");
            metop::derandomize(data);
            require(data == original, "XOR round-trip failed");
        } else if (test == "reset") {
            auto other = data;
            metop::derandomize(data);
            metop::derandomize(other);
            require(data == other && data[0] == 0xff, "Calls do not start at the PN origin");
        } else if (test == "period") {
            metop::derandomize(data);
            const auto bit = [&](std::size_t n) { return (data[n / 8] >> (7 - n % 8)) & 1; };
            for (std::size_t n = 255; n < data.size() * 8; ++n)
                require(bit(n) == bit(n - 255), "PN period is not 255 bits");
        } else if (test == "boundaries") {
            std::array<std::uint8_t, 1028> guarded{};
            std::copy(metop::attached_sync_marker.begin(), metop::attached_sync_marker.end(), guarded.begin());
            std::fill(guarded.end() - 4, guarded.end(), 0xa5);
            metop::derandomize(std::span(guarded).subspan<4, metop::cvcdu_size>());
            require(std::equal(metop::attached_sync_marker.begin(), metop::attached_sync_marker.end(), guarded.begin()),
                    "ASM was modified");
            require(std::all_of(guarded.end() - 4, guarded.end(), [](auto b) { return b == 0xa5; }),
                    "Wrote past CVCDU");
            // The bit-period assertion also verifies bytes in the 128-byte parity region.
            metop::derandomize(data);
            require(std::equal(data.begin(), data.end(), guarded.begin() + 4), "CVCDU not fully XORed");
        } else throw std::runtime_error("Unknown test");
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
