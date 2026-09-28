#pragma once

#include "packet_reassembler.h"
#include <array>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace metop {
inline constexpr std::size_t avhrr_earth_samples = 2048;
inline constexpr std::size_t avhrr_active_channels = 5;
inline constexpr std::size_t avhrr_packet_size = 12966;

enum class AvhrrChannel { ch1, ch2, ch3a, ch3b, ch4, ch5 };
std::string_view avhrr_channel_name(AvhrrChannel channel) noexcept;
enum class AvhrrPayloadError { none, packet_length, packet_header, checksum, time, sbt_reserved, filler };
std::string_view avhrr_error_name(AvhrrPayloadError error) noexcept;

struct AvhrrScan {
    SpacePacketHeader header;
    // Raw CDS time, epoch 2000-01-01; no inferred acquisition-time correction.
    std::uint16_t utc_days = 0;
    std::uint32_t utc_milliseconds = 0;
    std::uint16_t utc_microseconds = 0;
    std::uint32_t sbt_seconds = 0; // 24-bit coarse time; epoch not inferred.
    std::uint16_t sbt_fraction = 0; // units of 2^-16 seconds.
    std::array<AvhrrChannel, avhrr_active_channels> channels{};
    std::array<std::array<std::uint16_t, avhrr_earth_samples>, avhrr_active_channels> earth{};
    std::array<std::array<std::uint16_t, 10>, avhrr_active_channels> space{};
    std::array<std::array<std::uint16_t, 10>, avhrr_active_channels> back_scan{};
    // Preserve wire-order calibration/telemetry words without radiometric interpretation.
    std::array<std::uint16_t, 5> ramp{};
    std::array<std::uint16_t, 5> ir_target_temperature{};
    std::array<std::uint16_t, 5> patch_temperature{};
};
struct AvhrrDecodeResult {
    AvhrrPayloadError error = AvhrrPayloadError::none;
    std::optional<AvhrrScan> scan;
};
// Input is the COMPLETE Space Packet (including the six-byte primary header).
// Only the documented HR profile is accepted. No partial/invalid scan is returned.
AvhrrDecodeResult decode_avhrr_packet(std::span<const std::uint8_t> packet);

struct AvhrrScanStatistics {
    std::uint64_t candidates = 0, accepted = 0, rejected = 0;
    std::uint64_t channel_3a = 0, channel_3b = 0;
    std::array<std::uint64_t, 7> errors{};
};

// Streams one scan per valid VCID-9/APID-103/104 packet. Does not fill sequence gaps,
// combine spacecraft streams, sort scans, or synthesize an inactive channel.
// Callback is synchronous; copy the scan if retaining it beyond the call.
class AvhrrScanProcessor {
public:
    using Sink = std::function<void(const ReassembledPacket&, const AvhrrScan&)>;
    AvhrrScanProcessor(std::ostream& log, bool rs_checked, Sink sink = {});
    void consume(const ReassembledPacket& packet, std::uint64_t packet_index);
    const AvhrrScanStatistics& statistics() const noexcept { return statistics_; }
private:
    std::ostream& log_;
    bool rs_checked_;
    Sink sink_;
    AvhrrScanStatistics statistics_;
};
void write_avhrr_scan_text(std::ostream&, const AvhrrScanStatistics&);
void write_avhrr_scan_json(std::ostream&, const AvhrrScanStatistics&);
}
