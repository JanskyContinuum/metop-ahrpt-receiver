#include "avhrr_payload.h"
#include "sample_unpack.h"

#include <algorithm>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace metop {
namespace {
// Derived from MO-IF-MMT-SY0001 issue 07C, sections 5.2.1.2 and 5.2.2.3.1.
// See docs/avhrr-payload-layout.md for source provenance and bit accounting.
constexpr std::size_t application_offset = 6 + 8 + 6;
constexpr std::size_t application_bytes = 12944;
constexpr std::size_t space_words = 50, ramp_words = 5;
constexpr std::size_t earth_start = space_words + ramp_words;
constexpr std::size_t temperature_start = earth_start + avhrr_active_channels * avhrr_earth_samples;
constexpr std::size_t back_start = temperature_start + 5 + 5;
constexpr std::size_t sample_words = back_start + 50;
static_assert(sample_words * 10 + 2 == application_bytes * 8);
static_assert(application_offset + application_bytes + 2 == avhrr_packet_size);

std::uint16_t be16(std::span<const std::uint8_t> p, std::size_t i) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(p[i]) << 8) | p[i + 1]);
}
std::uint32_t be32(std::span<const std::uint8_t> p, std::size_t i) {
    return (static_cast<std::uint32_t>(be16(p, i)) << 16) | be16(p, i + 2);
}
}
std::string_view avhrr_channel_name(AvhrrChannel channel) noexcept {
    switch (channel) {
    case AvhrrChannel::ch1: return "1";
    case AvhrrChannel::ch2: return "2";
    case AvhrrChannel::ch3a: return "3A";
    case AvhrrChannel::ch3b: return "3B";
    case AvhrrChannel::ch4: return "4";
    case AvhrrChannel::ch5: return "5";
    }
    return "unknown";
}
std::string_view avhrr_error_name(AvhrrPayloadError error) noexcept {
    switch (error) {
    case AvhrrPayloadError::none: return "accepted";
    case AvhrrPayloadError::packet_length: return "packet_length";
    case AvhrrPayloadError::packet_header: return "packet_header";
    case AvhrrPayloadError::checksum: return "vpc";
    case AvhrrPayloadError::time: return "time";
    case AvhrrPayloadError::sbt_reserved: return "sbt_reserved";
    case AvhrrPayloadError::filler: return "filler";
    }
    return "unknown";
}
AvhrrDecodeResult decode_avhrr_packet(std::span<const std::uint8_t> packet) {
    const auto fail = [](AvhrrPayloadError error) { return AvhrrDecodeResult{error, std::nullopt}; };
    if (packet.size() != avhrr_packet_size) return fail(AvhrrPayloadError::packet_length);
    SpacePacketHeader header;
    try { header = validate_space_packet(packet); }
    catch (const std::invalid_argument&) { return fail(AvhrrPayloadError::packet_header); }
    if (header.type || !header.secondary_header_flag || header.sequence_flags != 3
        || (header.apid != 103 && header.apid != 104))
        return fail(AvhrrPayloadError::packet_header);
    // VPC is the XOR of all preceding 16-bit pairs, including the primary header.
    std::uint16_t parity = 0;
    for (std::size_t i = 0; i < packet.size(); i += 2) parity ^= be16(packet, i);
    if (parity != 0) return fail(AvhrrPayloadError::checksum);
    const auto milliseconds = be32(packet, 8);
    const auto microseconds = be16(packet, 12);
    // Allow the positive UTC leap second; a leap-second table is outside this raw parser.
    if (milliseconds >= 86401000 || microseconds >= 1000) return fail(AvhrrPayloadError::time);
    if (packet[14] != 0) return fail(AvhrrPayloadError::sbt_reserved);
    if ((packet[application_offset + application_bytes - 1] & 3U) != 0)
        return fail(AvhrrPayloadError::filler);

    AvhrrDecodeResult result;
    auto& scan = result.scan.emplace();
    scan.header = header;
    scan.utc_days = be16(packet, 6);
    scan.utc_milliseconds = milliseconds;
    scan.utc_microseconds = microseconds;
    scan.sbt_seconds = (static_cast<std::uint32_t>(packet[15]) << 16) | be16(packet, 16);
    scan.sbt_fraction = be16(packet, 18);
    scan.channels = {AvhrrChannel::ch1, AvhrrChannel::ch2,
        header.apid == 103 ? AvhrrChannel::ch3a : AvhrrChannel::ch3b,
        AvhrrChannel::ch4, AvhrrChannel::ch5};
    const auto words = unpack_10bit_msb(packet.subspan(application_offset, application_bytes), 0, sample_words);
    // Instrument word order corroborated with the pinned SatDump MetOp reader.
    for (std::size_t c = 0; c < avhrr_active_channels; ++c) {
        for (std::size_t x = 0; x < avhrr_earth_samples; ++x)
            scan.earth[c][x] = words[earth_start + x * avhrr_active_channels + c];
        for (std::size_t x = 0; x < 10; ++x) {
            scan.space[c][x] = words[x * avhrr_active_channels + c];
            scan.back_scan[c][x] = words[back_start + x * avhrr_active_channels + c];
        }
    }
    std::copy_n(words.begin() + space_words, 5, scan.ramp.begin());
    std::copy_n(words.begin() + temperature_start, 5, scan.ir_target_temperature.begin());
    std::copy_n(words.begin() + temperature_start + 5, 5, scan.patch_temperature.begin());
    return result;
}
AvhrrScanProcessor::AvhrrScanProcessor(std::ostream& log, bool rs_checked, Sink sink)
    : log_(log), rs_checked_(rs_checked), sink_(std::move(sink)) {
    log_ << "packet_index,spacecraft_id,vcid,replay,start_counter,end_counter,apid,sequence_count,"
            "rs_status,payload_status,channel_3,utc_days,utc_milliseconds,utc_microseconds,"
            "sbt_seconds,sbt_fraction,earth_samples_per_channel\n";
    if (!log_) throw std::runtime_error("Cannot write AVHRR scan log header");
}
void AvhrrScanProcessor::consume(const ReassembledPacket& packet, std::uint64_t packet_index) {
    if (packet.source.vcid != 9) return;
    // Read routing from the wire, not potentially stale cached header metadata.
    const auto header = validate_space_packet(packet.bytes);
    if (header.apid != 103 && header.apid != 104) return;
    ++statistics_.candidates;
    const auto decoded = decode_avhrr_packet(packet.bytes);
    log_ << packet_index << ',' << unsigned(packet.source.spacecraft_id) << ','
         << unsigned(packet.source.vcid) << ',' << packet.source.replay << ','
         << packet.source.counter << ',' << packet.end_counter << ',' << header.apid << ','
         << header.sequence_count << ',' << (rs_checked_ ? "rs_checked" : "not_applied")
         << ',' << avhrr_error_name(decoded.error);
    if (decoded.scan) {
        const auto& scan = *decoded.scan;
        ++statistics_.accepted;
        if (header.apid == 103) ++statistics_.channel_3a; else ++statistics_.channel_3b;
        log_ << ',' << avhrr_channel_name(scan.channels[2]) << ',' << scan.utc_days << ','
             << scan.utc_milliseconds << ',' << scan.utc_microseconds << ','
             << scan.sbt_seconds << ',' << scan.sbt_fraction << ',' << avhrr_earth_samples << '\n';
        if (!log_) throw std::runtime_error("Cannot write AVHRR scan log");
        if (sink_) sink_(packet, scan);
    } else {
        ++statistics_.rejected;
        ++statistics_.errors[static_cast<std::size_t>(decoded.error)];
        log_ << ",,,,,,,\n";
        if (!log_) throw std::runtime_error("Cannot write AVHRR scan log");
    }
}
void write_avhrr_scan_text(std::ostream& out, const AvhrrScanStatistics& s) {
    out << "AVHRR scans (VPC checked, raw 10-bit counts):\n"
        << "  candidates: " << s.candidates << "\n  accepted: " << s.accepted
        << "\n  rejected: " << s.rejected << "\n  channel_3a: " << s.channel_3a
        << "\n  channel_3b: " << s.channel_3b << "\n  Earth samples per active channel: 2048\n";
    for (std::size_t i = 1; i < s.errors.size(); ++i)
        out << "  rejected_" << avhrr_error_name(static_cast<AvhrrPayloadError>(i)) << ": " << s.errors[i] << '\n';
}
void write_avhrr_scan_json(std::ostream& out, const AvhrrScanStatistics& s) {
    out << "{\"candidates\": " << s.candidates << ", \"accepted\": " << s.accepted
        << ", \"rejected\": " << s.rejected << ", \"channel_3a\": " << s.channel_3a
        << ", \"channel_3b\": " << s.channel_3b << ", \"earth_samples_per_channel\": 2048, \"errors\": {";
    for (std::size_t i = 1; i < s.errors.size(); ++i)
        out << (i == 1 ? "" : ", ") << '"' << avhrr_error_name(static_cast<AvhrrPayloadError>(i)) << "\": " << s.errors[i];
    out << "}}";
}
} // namespace metop
