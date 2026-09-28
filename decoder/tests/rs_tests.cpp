#include "reed_solomon.h"
#include "ccsds_randomizer.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

template<std::size_t N>
std::vector<std::array<std::uint8_t, N>> vectors(const char* name) {
    std::ifstream input(std::string(RS_REFERENCE_DIR) + "/" + name);
    require(bool(input), "Cannot open reference vectors");
    std::vector<std::array<std::uint8_t, N>> result;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        require(line.size() == N * 2, "Wrong reference vector size");
        std::array<std::uint8_t, N> bytes{};
        for (std::size_t i = 0; i < N; ++i)
            bytes[i] = static_cast<std::uint8_t>(std::stoul(line.substr(i * 2, 2), nullptr, 16));
        result.push_back(bytes);
    }
    return result;
}

// Independent shift-and-reduce arithmetic, used only to check the normative
// dual basis via field traces: z_j = Tr(u * beta^j), beta=alpha^117.
std::uint8_t multiply(unsigned a, unsigned b) {
    unsigned result = 0;
    while (b) {
        if (b & 1U) result ^= a;
        b >>= 1; a <<= 1;
        if (a & 256U) a ^= 0x187U;
    }
    return static_cast<std::uint8_t>(result);
}
std::uint8_t trace(std::uint8_t value) {
    auto result = value;
    for (unsigned i = 1; i < 8; ++i) { value = multiply(value, value); result ^= value; }
    require(result <= 1, "Trace not in GF(2)");
    return result;
}

void damage_lane(std::array<std::uint8_t, 1020>& frame, std::size_t lane, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) frame[4 * (10 + i) + lane] ^= static_cast<std::uint8_t>(i + 1);
}

void fixture(const char* path, const std::string& kind) {
    auto frames = vectors<1020>("cvcdus.hex");
    if (kind == "corrected") {
        // Includes version, SCID/VCID, counter and FHP: parser must see corrected bytes.
        for (std::size_t f = 0; f < frames.size(); ++f)
            for (std::size_t i = 0; i < 64; ++i) frames[f][i] ^= static_cast<std::uint8_t>(i + 1);
    } else if (kind == "recovery" || kind == "duplicate_recovery") {
        damage_lane(frames[1], 0, 17);
        damage_lane(frames[1], 1, 1);
        if (kind == "duplicate_recovery") frames.insert(frames.begin() + 2, frames[0]);
    } else if (kind == "rejected") {
        for (auto& frame : frames) damage_lane(frame, 0, 17);
    }
    std::ofstream out(path, std::ios::binary);
    require(bool(out), "Cannot create fixture");
    constexpr std::array<std::uint8_t, 4> asm_bytes{0x1a, 0xcf, 0xfc, 0x1d};
    for (auto& frame : frames) {
        metop::derandomize(frame);
        out.write(reinterpret_cast<const char*>(asm_bytes.data()), 4);
        out.write(reinterpret_cast<const char*>(frame.data()), 1020);
    }
    out.close();
    require(bool(out), "Cannot write fixture");
}
}

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Missing case");
        const std::string test = argv[1];
        if (test == "fixture") { require(argc == 4, "fixture path kind"); fixture(argv[2], argv[3]); return 0; }
        const auto words = vectors<255>("codewords.hex");
        const auto frames = vectors<1020>("cvcdus.hex");
        require(words.size() == 2 && frames.size() == 4, "Incomplete reference vectors");
        if (test == "basis") {
            std::uint8_t beta = 1;
            for (unsigned i = 0; i < 117; ++i) beta = multiply(beta, 2);
            require(beta == 0x4d, "beta must equal alpha^117");
            for (unsigned u = 0; u < 256; ++u) {
                std::uint8_t dual = 0, beta_power = 1;
                for (unsigned j = 0; j < 8; ++j) {
                    dual |= static_cast<std::uint8_t>(trace(multiply(u, beta_power)) << (7 - j));
                    beta_power = multiply(beta_power, beta);
                }
                require(metop::conventional_to_dual(static_cast<std::uint8_t>(u)) == dual, "Dual basis trace mismatch");
                require(metop::dual_to_conventional(dual) == u, "Basis conversion inverse mismatch");
            }
            require(metop::conventional_to_dual(1) == 0x7b, "CCSDS table F-1 known pair");
        } else if (test == "vectors") {
            for (auto word : words) {
                const auto original = word;
                require(metop::correct_ccsds_codeword(word) == 0 && word == original, "External CCSDS vector rejected");
            }
            metop::RsCodeword zero{};
            require(metop::correct_ccsds_codeword(zero) == 0, "Zero word rejected");
            auto wrong_basis = words[0];
            std::transform(wrong_basis.begin(), wrong_basis.end(), wrong_basis.begin(), metop::dual_to_conventional);
            const auto before = wrong_basis;
            require(metop::correct_ccsds_codeword(wrong_basis) == -1 && wrong_basis == before, "Conventional representation accepted as dual");
        } else if (test == "all_positions") {
            for (const auto& original : words)
                for (std::size_t p = 0; p < 255; ++p) {
                    auto word = original;
                    word[p] ^= static_cast<std::uint8_t>(1 + p);
                    require(metop::correct_ccsds_codeword(word) == 1 && word == original, "Single-symbol correction mismatch");
                }
        } else if (test == "error_counts") {
            std::mt19937 rng(0x18711211);
            for (unsigned count = 1; count <= 16; ++count)
                for (unsigned trial = 0; trial < 24; ++trial) {
                    const auto& original = words[trial % words.size()];
                    auto word = original;
                    std::array<bool, 255> used{};
                    for (unsigned n = 0; n < count; ++n) {
                        auto p = rng() % 255;
                        while (used[p]) p = rng() % 255;
                        used[p] = true;
                        word[p] ^= static_cast<std::uint8_t>(1 + rng() % 255);
                    }
                    require(metop::correct_ccsds_codeword(word) == static_cast<int>(count) && word == original,
                            "Multi-symbol correction mismatch");
                }
        } else if (test == "bounds") {
            for (const auto size : {0U, 1U, 254U, 256U, 1020U}) {
                std::vector<std::uint8_t> word(size);
                bool threw = false;
                try { (void)metop::correct_ccsds_codeword(word); } catch (const std::invalid_argument&) { threw = true; }
                require(threw, "Invalid codeword size accepted");
            }
            for (const auto size : {0U, 891U, 1019U, 1021U}) {
                bool threw = false;
                try { (void)metop::decode_rs_frame(std::vector<std::uint8_t>(size)); }
                catch (const std::invalid_argument&) { threw = true; }
                require(threw, "Invalid CVCDU size accepted");
            }
        } else if (test == "interleave") {
            for (const auto& frame : frames) {
                const auto lanes = metop::deinterleave_rs(frame);
                for (std::size_t lane = 0; lane < 4; ++lane)
                    for (std::size_t i = 0; i < 255; ++i)
                        require(lanes[lane][i] == frame[4 * i + lane], "Lane stride/order mismatch");
                const auto result = metop::decode_rs_frame(frame);
                require(result.status == metop::RsStatus::good && result.vcdu.has_value(), "External frame rejected");
                require(std::equal(result.vcdu->begin(), result.vcdu->end(), frame.begin()), "Reconstruction mismatch");
                require(*result.vcdu == metop::reconstruct_vcdu(lanes), "Data/parity separation mismatch");
            }
        } else if (test == "burst") {
            // Every possible contiguous 64-byte burst: exactly 16 symbols in each lane.
            for (std::size_t start = 0; start + 64 <= 1020; ++start) {
                auto frame = frames[start % frames.size()];
                const auto original = frame;
                for (std::size_t i = 0; i < 64; ++i) frame[start + i] ^= static_cast<std::uint8_t>(i + 1);
                const auto result = metop::decode_rs_frame(frame);
                require(result.status == metop::RsStatus::corrected && result.vcdu.has_value(), "Burst rejected");
                require(result.corrected_symbols == std::array<int, 4>{16, 16, 16, 16}, "Burst counts");
                require(std::equal(result.vcdu->begin(), result.vcdu->end(), original.begin()), "Burst reconstruction");
            }
        } else if (test == "uncorrectable") {
            auto frame = frames[1];
            damage_lane(frame, 0, 17); damage_lane(frame, 1, 1);
            auto word = metop::deinterleave_rs(frame)[0];
            const auto original = word;
            require(metop::correct_ccsds_codeword(word) == -1 && word == original, "Rejected word mutated");
            const auto result = metop::decode_rs_frame(frame);
            require(result.status == metop::RsStatus::uncorrectable && !result.vcdu, "Rejected frame exposed");
            require(result.corrected_symbols == std::array<int, 4>{-1, 1, 0, 0}, "Mixed lane results");
            auto parity = frames[0];
            for (std::size_t i = 223; i < 239; ++i) parity[4 * i + 3] ^= 0x57;
            const auto corrected = metop::decode_rs_frame(parity);
            require(corrected.corrected_symbols == std::array<int, 4>{0, 0, 0, 16}, "Parity corrections not counted");
            require(corrected.vcdu && std::equal(corrected.vcdu->begin(), corrected.vcdu->end(), frames[0].begin()), "Parity reconstruction");
        } else if (test == "statistics") {
            metop::RsStatistics stats;
            stats.observe(metop::decode_rs_frame(frames[0]));
            auto frame = frames[0];
            damage_lane(frame, 2, 16); stats.observe(metop::decode_rs_frame(frame));
            damage_lane(frame, 0, 17); stats.observe(metop::decode_rs_frame(frame));
            require(stats.good_frames == 1 && stats.corrected_frames == 1 && stats.uncorrectable_frames == 1, "Frame statistics");
            require(stats.corrected_symbols == std::array<std::uint64_t, 4>{0, 0, 32, 0}, "Lane correction totals");
            require(stats.uncorrectable_lanes == std::array<std::uint64_t, 4>{1, 0, 0, 0}, "Lane failure totals");
        } else throw std::runtime_error("Unknown case");
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
