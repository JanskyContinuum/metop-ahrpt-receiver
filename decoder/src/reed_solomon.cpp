#include "reed_solomon.h"

#include <algorithm>
#include <stdexcept>

namespace metop {
namespace {

// Normative matrices, CCSDS 131.0-B-5 section 4.3.9. Rows correspond to input
// bits MSB first; transmitted z0 is MSB. These are mathematical constants.
constexpr std::array<std::uint8_t, 8> to_dual{0x8d, 0xef, 0xec, 0x86, 0xfa, 0x99, 0xaf, 0x7b};
constexpr std::array<std::uint8_t, 8> to_conventional{0xc5, 0x42, 0x2e, 0xfd, 0xf0, 0x79, 0xac, 0xcc};

std::uint8_t transform(std::uint8_t value, const std::array<std::uint8_t, 8>& rows) noexcept {
    std::uint8_t result = 0;
    for (std::size_t bit = 0; bit < 8; ++bit)
        if (value & (0x80U >> bit)) result ^= rows[bit];
    return result;
}

struct Field {
    std::array<std::uint8_t, 255> exp{};
    std::array<unsigned, 256> log{};
    constexpr Field() {
        unsigned value = 1;
        for (unsigned i = 0; i < 255; ++i) {
            exp[i] = static_cast<std::uint8_t>(value);
            log[value] = i;
            value <<= 1;
            if (value & 0x100U) value ^= 0x187U; // x^8+x^7+x^2+x+1
        }
    }
    std::uint8_t alpha(unsigned power) const noexcept { return exp[power % 255]; }
    std::uint8_t mul(std::uint8_t a, std::uint8_t b) const noexcept {
        return a && b ? alpha(log[a] + log[b]) : 0;
    }
    std::uint8_t inv(std::uint8_t a) const {
        if (!a) throw std::logic_error("GF inverse of zero");
        return alpha(255 - log[a]);
    }
    std::uint8_t pow(std::uint8_t a, unsigned power) const noexcept {
        return power == 0 ? 1 : (a ? alpha(log[a] * power) : 0);
    }
};
constexpr Field gf;
using Syndromes = std::array<std::uint8_t, 32>;

Syndromes syndromes(const RsCodeword& word) {
    Syndromes result{};
    for (unsigned j = 0; j < result.size(); ++j) {
        // CCSDS roots alpha^(11*j), j=112..143, not generic QR/DVB defaults.
        const auto root = gf.alpha(11 * (112 + j));
        for (const auto symbol : word) result[j] = gf.mul(result[j], root) ^ symbol;
    }
    return result;
}
bool zero(const Syndromes& values) {
    return std::all_of(values.begin(), values.end(), [](auto value) { return value == 0; });
}

} // namespace

std::uint8_t conventional_to_dual(std::uint8_t value) noexcept { return transform(value, to_dual); }
std::uint8_t dual_to_conventional(std::uint8_t value) noexcept { return transform(value, to_conventional); }

int correct_ccsds_codeword(std::span<std::uint8_t> dual_word) {
    if (dual_word.size() != rs_symbols) throw std::invalid_argument("RS codeword must be 255 bytes");
    RsCodeword candidate{};
    std::transform(dual_word.begin(), dual_word.end(), candidate.begin(), dual_to_conventional);
    const auto s = syndromes(candidate);
    if (zero(s)) return 0;

    // Berlekamp-Massey: locator coefficients in ascending powers.
    std::array<std::uint8_t, 33> locator{}, previous{};
    locator[0] = previous[0] = 1;
    std::size_t degree = 0, shift = 1;
    std::uint8_t previous_discrepancy = 1;
    for (std::size_t n = 0; n < s.size(); ++n) {
        auto discrepancy = s[n];
        for (std::size_t i = 1; i <= degree; ++i)
            discrepancy ^= gf.mul(locator[i], s[n - i]);
        if (!discrepancy) {
            ++shift;
            continue;
        }
        const auto saved = locator;
        const auto scale = gf.mul(discrepancy, gf.inv(previous_discrepancy));
        for (std::size_t i = 0; i + shift < locator.size(); ++i)
            locator[i + shift] ^= gf.mul(scale, previous[i]);
        if (2 * degree <= n) {
            degree = n + 1 - degree;
            previous = saved;
            previous_discrepancy = discrepancy;
            shift = 1;
        } else {
            ++shift;
        }
    }
    if (degree == 0 || degree > 16) return -1;

    // Chien search. Byte p is coefficient x^(254-p), so X=alpha^(11*(254-p)).
    std::array<std::size_t, 16> positions{};
    std::array<std::uint8_t, 16> locations{};
    std::size_t found = 0;
    for (std::size_t p = 0; p < rs_symbols; ++p) {
        const auto x = gf.alpha(11 * static_cast<unsigned>(254 - p));
        const auto inverse = gf.inv(x);
        auto value = locator[degree];
        for (std::size_t i = degree; i > 0; --i) value = gf.mul(value, inverse) ^ locator[i - 1];
        if (value == 0) {
            if (found == degree) return -1;
            positions[found] = p;
            locations[found++] = x;
        }
    }
    if (found != degree) return -1;

    // Solve S_j = sum(y_k * X_k^j), y_k = error_k * X_k^112.
    // A small GF Vandermonde solve avoids convention-sensitive Forney formulas.
    std::array<std::array<std::uint8_t, 17>, 16> matrix{};
    for (std::size_t row = 0; row < degree; ++row) {
        for (std::size_t col = 0; col < degree; ++col)
            matrix[row][col] = gf.pow(locations[col], static_cast<unsigned>(row));
        matrix[row][degree] = s[row];
    }
    for (std::size_t col = 0; col < degree; ++col) {
        std::size_t pivot = col;
        while (pivot < degree && !matrix[pivot][col]) ++pivot;
        if (pivot == degree) return -1;
        std::swap(matrix[pivot], matrix[col]);
        const auto scale = gf.inv(matrix[col][col]);
        for (std::size_t j = col; j <= degree; ++j) matrix[col][j] = gf.mul(matrix[col][j], scale);
        for (std::size_t row = 0; row < degree; ++row) {
            if (row == col) continue;
            const auto factor = matrix[row][col];
            for (std::size_t j = col; j <= degree; ++j)
                matrix[row][j] ^= gf.mul(factor, matrix[col][j]);
        }
    }
    for (std::size_t k = 0; k < degree; ++k) {
        const auto error = gf.mul(matrix[k][degree], gf.inv(gf.pow(locations[k], 112)));
        if (!error) return -1;
        candidate[positions[k]] ^= error;
    }
    if (!zero(syndromes(candidate))) return -1;
    std::transform(candidate.begin(), candidate.end(), dual_word.begin(), conventional_to_dual);
    return static_cast<int>(degree);
}

RsLanes deinterleave_rs(std::span<const std::uint8_t> cvcdu) {
    if (cvcdu.size() != cvcdu_size) throw std::invalid_argument("CVCDU must be 1020 bytes");
    RsLanes lanes{};
    for (std::size_t symbol = 0; symbol < rs_symbols; ++symbol)
        for (std::size_t lane = 0; lane < rs_interleave; ++lane)
            lanes[lane][symbol] = cvcdu[rs_interleave * symbol + lane];
    return lanes;
}

std::array<std::uint8_t, vcdu_size> reconstruct_vcdu(const RsLanes& lanes) {
    static_assert(vcdu_size == rs_data_symbols * rs_interleave);
    static_assert(cvcdu_size == rs_symbols * rs_interleave);
    std::array<std::uint8_t, vcdu_size> result{};
    for (std::size_t symbol = 0; symbol < rs_data_symbols; ++symbol)
        for (std::size_t lane = 0; lane < rs_interleave; ++lane)
            result[rs_interleave * symbol + lane] = lanes[lane][symbol];
    return result;
}

RsFrameResult decode_rs_frame(std::span<const std::uint8_t> cvcdu) {
    auto lanes = deinterleave_rs(cvcdu);
    RsFrameResult result;
    result.status = RsStatus::good;
    for (std::size_t lane = 0; lane < rs_interleave; ++lane) {
        const auto count = correct_ccsds_codeword(lanes[lane]);
        result.corrected_symbols[lane] = count;
        if (count < 0) result.status = RsStatus::uncorrectable;
        else if (count > 0 && result.status == RsStatus::good) result.status = RsStatus::corrected;
    }
    if (result.status != RsStatus::uncorrectable) result.vcdu = reconstruct_vcdu(lanes);
    return result;
}

std::string_view rs_status_name(RsStatus status) noexcept {
    switch (status) {
    case RsStatus::good: return "good";
    case RsStatus::corrected: return "corrected";
    case RsStatus::uncorrectable: return "uncorrectable";
    }
    return "unknown";
}

void RsStatistics::observe(const RsFrameResult& result) noexcept {
    switch (result.status) {
    case RsStatus::good: ++good_frames; break;
    case RsStatus::corrected: ++corrected_frames; break;
    case RsStatus::uncorrectable: ++uncorrectable_frames; break;
    }
    for (std::size_t lane = 0; lane < rs_interleave; ++lane) {
        if (result.corrected_symbols[lane] < 0) ++uncorrectable_lanes[lane];
        else corrected_symbols[lane] += static_cast<unsigned>(result.corrected_symbols[lane]);
    }
}

} // namespace metop
