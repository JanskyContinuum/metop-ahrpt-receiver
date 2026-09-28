#pragma once

#include "ccsds_randomizer.h"
#include "vcdu.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace metop {

inline constexpr std::size_t rs_symbols = 255;
inline constexpr std::size_t rs_data_symbols = 223;
inline constexpr std::size_t rs_interleave = 4;
using RsCodeword = std::array<std::uint8_t, rs_symbols>;
using RsLanes = std::array<RsCodeword, rs_interleave>;

// CCSDS 131.0-B-5, 4.3.9: conventional polynomial basis <-> transmitted dual basis.
std::uint8_t conventional_to_dual(std::uint8_t value) noexcept;
std::uint8_t dual_to_conventional(std::uint8_t value) noexcept;

// Full-length CCSDS RS(255,223), no erasures/shortening. Returns changed symbols,
// 0 for an already valid word, -1 if uncorrectable. Failure leaves input unchanged.
// More than 16 errors can be undetected or miscorrected, as with any bounded-distance RS decoder.
int correct_ccsds_codeword(std::span<std::uint8_t> dual_word);

RsLanes deinterleave_rs(std::span<const std::uint8_t> cvcdu);
std::array<std::uint8_t, vcdu_size> reconstruct_vcdu(const RsLanes& lanes);

enum class RsStatus { good, corrected, uncorrectable };
std::string_view rs_status_name(RsStatus status) noexcept;

struct RsFrameResult {
    RsStatus status = RsStatus::uncorrectable;
    std::array<int, rs_interleave> corrected_symbols{};
    // No data is exposed unless ALL lanes pass the syndrome checks.
    std::optional<std::array<std::uint8_t, vcdu_size>> vcdu;
};
RsFrameResult decode_rs_frame(std::span<const std::uint8_t> cvcdu);

struct RsStatistics {
    std::uint64_t good_frames = 0;
    std::uint64_t corrected_frames = 0;
    std::uint64_t uncorrectable_frames = 0;
    // Successful lane corrections, including lanes in a rejected frame.
    std::array<std::uint64_t, rs_interleave> corrected_symbols{};
    std::array<std::uint64_t, rs_interleave> uncorrectable_lanes{};
    void observe(const RsFrameResult& result) noexcept;
};

} // namespace metop
