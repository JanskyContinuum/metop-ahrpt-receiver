#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>

namespace metop {

inline constexpr std::size_t cadu_size = 1024;
inline constexpr std::array<std::uint8_t, 4> attached_sync_marker{0x1a, 0xcf, 0xfc, 0x1d};

struct Cadu {
    std::array<std::uint8_t, cadu_size> bytes{};
    std::uint64_t file_offset = 0;
    std::uint64_t index = 0; // Zero-based ordinal of emitted CADUs, not a VCDU counter.
};

struct CaduStatistics {
    std::uint64_t cadus_read = 0;
    std::uint64_t valid_asm = 0;
    std::uint64_t asm_failures = 0; // Loss-of-alignment episodes, not inferred lost CADUs.
    std::uint64_t resyncs = 0;
    std::uint64_t skipped_bytes = 0;
    std::uint64_t trailing_bytes = 0;
    std::uint64_t rejected_candidates = 0;
    std::uint64_t unconfirmed_candidates = 0;
    std::uint64_t bytes_consumed = 0; // Excludes lookahead still in the buffer.
    bool reached_eof = false; // All input has been classified, including a trailing suffix.
};

// Binary input with default (non-throwing) stream exception mask. Owns no input file.
// Bounded lookahead: at most one CADU plus the next ASM is needed.
class CaduReader {
public:
    explicit CaduReader(std::istream& input) : input_(input) {}
    std::optional<Cadu> next();
    const CaduStatistics& statistics() const noexcept { return statistics_; }

private:
    bool ensure(std::size_t count);
    bool has_marker(std::size_t offset = 0) const;
    void consume(std::size_t count);
    void finish();
    std::uint8_t byte(std::size_t offset) const;

    std::istream& input_;
    std::array<std::uint8_t, cadu_size + attached_sync_marker.size()> buffer_{};
    std::size_t begin_ = 0;
    std::size_t size_ = 0;
    bool input_eof_ = false;
    bool recovering_ = false;
    CaduStatistics statistics_{};
};

} // namespace metop
