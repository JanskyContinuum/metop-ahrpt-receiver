#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>

namespace metop {

inline constexpr std::size_t vcdu_size = 892;
inline constexpr std::uint32_t vcdu_counter_mask = 0xffffff;

struct VcduHeader {
    std::uint8_t version = 0;
    std::uint8_t spacecraft_id = 0;
    std::uint8_t vcid = 0;
    std::uint32_t counter = 0;
    bool replay = false;
    // MetOp profile from the engineering specification: retain all seven low bits.
    // Do not reinterpret these as newer AOS count-cycle fields without a mission ICD.
    std::uint8_t signaling_spare = 0;
};

struct Vcdu {
    VcduHeader header;
    std::array<std::uint8_t, 2> insert_zone{};
    std::span<const std::uint8_t> mpdu; // Borrowed view; source storage must outlive this object.
};

// Reject sizes other than the MetOp profile's 892 bytes. Field extraction itself
// retains every value; the caller decides whether version/profile values are usable.
Vcdu parse_vcdu(std::span<const std::uint8_t> bytes);

enum class Continuity { first, contiguous, gap, duplicate, backward_or_reset };
std::string_view continuity_name(Continuity value);
struct CounterObservation {
    Continuity status = Continuity::first;
    std::optional<std::uint32_t> previous;
};

class VcduCounterTracker {
public:
    CounterObservation observe(const VcduHeader& header);
private:
    // Keep real-time/replay and different spacecraft separate within each VCID.
    std::map<std::tuple<std::uint8_t, std::uint8_t, bool>, std::uint32_t> last_;
};

} // namespace metop
