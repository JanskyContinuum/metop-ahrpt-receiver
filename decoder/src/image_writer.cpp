#include "image_writer.h"

#include <algorithm>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>
#include <utility>

namespace metop {
namespace {
constexpr std::size_t row_bytes = avhrr_earth_samples * 2;
constexpr std::array<const char*, 6> names{
    "ch1_raw.pgm", "ch2_raw.pgm", "ch3a_raw.pgm", "ch3b_raw.pgm", "ch4_raw.pgm", "ch5_raw.pgm"};
void validate_line(std::span<const std::uint16_t> line) {
    if (line.size() != avhrr_earth_samples)
        throw std::invalid_argument("AVHRR image line must contain exactly 2048 Earth samples");
    if (std::any_of(line.begin(), line.end(), [](auto v) { return v > 1023; }))
        throw std::invalid_argument("AVHRR image sample exceeds raw ten-bit range");
}
}
RawPgmWriter::RawPgmWriter(std::filesystem::path destination)
    : destination_(std::move(destination)), temporary_(destination_) {
    temporary_ += ".raster.tmp";
    if (std::filesystem::exists(destination_) || std::filesystem::exists(temporary_))
        throw std::runtime_error("Image destination or raster spool already exists");
}
RawPgmWriter::~RawPgmWriter() {
    raster_.close();
    std::error_code ignored;
    if (owns_temporary_) std::filesystem::remove(temporary_, ignored);
    if (owns_incomplete_output_) std::filesystem::remove(destination_, ignored);
}
void RawPgmWriter::append(std::span<const std::uint16_t> line) {
    if (finished_ || failed_) throw std::logic_error("Cannot append to closed/failed image writer");
    validate_line(line); // Validate before creating files or changing height.
    if (height_ >= std::numeric_limits<std::uint64_t>::max() / row_bytes)
        throw std::overflow_error("PGM raster size overflow");
    if (!raster_.is_open()) {
        raster_.open(temporary_, std::ios::binary | std::ios::trunc);
        if (!raster_) { failed_ = true; throw std::runtime_error("Cannot open image raster spool"); }
        owns_temporary_ = true;
    }
    std::array<char, row_bytes> bytes{};
    for (std::size_t i = 0; i < line.size(); ++i) {
        bytes[2*i] = static_cast<char>(line[i] >> 8);
        bytes[2*i+1] = static_cast<char>(line[i] & 255U);
    }
    raster_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!raster_) { failed_ = true; throw std::runtime_error("Cannot write image raster"); }
    ++height_;
}
void RawPgmWriter::finish() {
    if (failed_) throw std::logic_error("Cannot finish failed image writer");
    if (finished_) return;
    if (!height_) { finished_ = true; return; }
    try {
        raster_.close();
        if (!raster_) throw std::runtime_error("Cannot close image raster");
        std::ifstream input(temporary_, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot reopen image raster");
        if (std::filesystem::exists(destination_)) throw std::runtime_error("Image output already exists");
        std::ofstream output(destination_, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot open PGM output");
        owns_incomplete_output_ = true;
        output.imbue(std::locale::classic());
        output << "P5\n2048 " << height_ << "\n1023\n";
        auto remaining = height_ * row_bytes;
        std::array<char, 65536> buffer{};
        while (remaining) {
            const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
            input.read(buffer.data(), count);
            if (input.gcount() != count) throw std::runtime_error("Truncated image raster");
            output.write(buffer.data(), count);
            if (!output) throw std::runtime_error("Cannot write PGM output");
            remaining -= static_cast<std::uint64_t>(count);
        }
        if (input.peek() != std::char_traits<char>::eof() || input.bad())
            throw std::runtime_error("Unexpected image raster size or read failure");
        input.close();
        output.close();
        if (!output) throw std::runtime_error("Cannot close PGM output");
        std::filesystem::remove(temporary_);
        owns_temporary_ = false;
        owns_incomplete_output_ = false;
        finished_ = true;
    } catch (...) { failed_ = true; throw; }
}
struct AvhrrImageWriter::Stream {
    std::filesystem::path directory;
    std::ofstream rows;
    std::array<std::unique_ptr<RawPgmWriter>, 6> channels;
    std::uint64_t scans = 0;
};
AvhrrImageWriter::AvhrrImageWriter(std::filesystem::path directory, bool rs_applied)
    : directory_(std::move(directory)), rs_applied_(rs_applied) {
    if (std::filesystem::exists(directory_) &&
        (!std::filesystem::is_directory(directory_) || !std::filesystem::is_empty(directory_)))
        throw std::runtime_error("AVHRR image directory must be absent or empty; choose a new --out directory");
    std::filesystem::create_directories(directory_);
}
AvhrrImageWriter::~AvhrrImageWriter() = default;
void AvhrrImageWriter::add(const ReassembledPacket& packet, const AvhrrScan& scan) {
    if (finished_ || failed_) throw std::logic_error("Cannot add to closed/failed image collection");
    if (packet.source.vcid != 9 || (scan.header.apid != 103 && scan.header.apid != 104))
        throw std::invalid_argument("Unexpected AVHRR scan source");
    const std::array expected{AvhrrChannel::ch1, AvhrrChannel::ch2,
        scan.header.apid == 103 ? AvhrrChannel::ch3a : AvhrrChannel::ch3b,
        AvhrrChannel::ch4, AvhrrChannel::ch5};
    if (scan.channels != expected) throw std::invalid_argument("Invalid semantic AVHRR channel order");
    for (const auto& line : scan.earth) validate_line(line); // All five before any output.
    try {
        const Key key{packet.source.spacecraft_id, packet.source.vcid, packet.source.replay};
        auto& pointer = streams_[key];
        if (!pointer) {
            pointer = std::make_unique<Stream>();
            pointer->directory = directory_ / ("scid_" + std::to_string(packet.source.spacecraft_id)
                + "_vcid_" + std::to_string(packet.source.vcid) + "_replay_" + (packet.source.replay ? "1" : "0"));
            // A new stream directory must not replace any pre-existing content.
            if (!std::filesystem::create_directory(pointer->directory))
                throw std::runtime_error("Image stream directory already exists");
            pointer->rows.open(pointer->directory / "scan_rows.csv", std::ios::binary | std::ios::trunc);
            pointer->rows.imbue(std::locale::classic());
            pointer->rows << "scan_index,apid,sequence_count,start_counter,end_counter,utc_days,"
                             "utc_milliseconds,utc_microseconds,ch1_row,ch2_row,ch3a_row,ch3b_row,ch4_row,ch5_row\n";
            if (!pointer->rows) throw std::runtime_error("Cannot open image row log");
            ++statistics_.streams;
        }
        auto& stream = *pointer;
        std::array<std::optional<std::uint64_t>, 6> row_indices{};
        for (std::size_t c = 0; c < scan.channels.size(); ++c) {
            const auto semantic = static_cast<std::size_t>(scan.channels[c]);
            auto& writer = stream.channels[semantic];
            if (!writer) writer = std::make_unique<RawPgmWriter>(stream.directory / names[semantic]);
            row_indices[semantic] = writer->height();
            writer->append(scan.earth[c]);
            ++statistics_.channel_rows[semantic];
        }
        stream.rows << stream.scans << ',' << scan.header.apid << ',' << scan.header.sequence_count
            << ',' << packet.source.counter << ',' << packet.end_counter << ',' << scan.utc_days
            << ',' << scan.utc_milliseconds << ',' << scan.utc_microseconds;
        for (const auto row : row_indices) {
            stream.rows << ',';
            if (row) stream.rows << *row;
        }
        stream.rows << '\n';
        if (!stream.rows) throw std::runtime_error("Cannot write image row log");
        ++stream.scans;
        ++statistics_.scans;
    } catch (...) { failed_ = true; throw; }
}
void AvhrrImageWriter::finish() {
    if (failed_) throw std::logic_error("Cannot finish failed image collection");
    if (finished_) return;
    try {
        for (auto& [key, pointer] : streams_) {
            auto& stream = *pointer;
            stream.rows.close();
            if (!stream.rows) throw std::runtime_error("Cannot close image row log");
            for (auto& writer : stream.channels) if (writer) { writer->finish(); ++statistics_.files; }
            std::ofstream meta(stream.directory / "metadata.json", std::ios::binary | std::ios::trunc);
            meta.imbue(std::locale::classic());
            if (!meta) throw std::runtime_error("Cannot open image metadata");
            const auto [scid, vcid, replay] = key;
            meta << "{\n  \"spacecraft_id\": " << unsigned(scid) << ",\n  \"vcid\": " << unsigned(vcid)
                << ",\n  \"replay\": " << (replay ? "true" : "false")
                << ",\n  \"rs_applied\": " << (rs_applied_ ? "true" : "false")
                << ",\n  \"vpc_checked\": true,\n  \"width\": 2048,\n  \"maxval\": 1023,"
                   "\n  \"sample_type\": \"raw_uint16_big_endian\",\n  \"scan_count\": " << stream.scans
                << ",\n  \"row_order\": \"arrival\",\n  \"gap_padding\": false,\n  \"channels\": {";
            bool first = true;
            for (std::size_t c = 0; c < stream.channels.size(); ++c) if (stream.channels[c]) {
                meta << (first ? "\n" : ",\n") << "    \""
                    << avhrr_channel_name(static_cast<AvhrrChannel>(c)) << "\": {\"file\": \"" << names[c]
                    << "\", \"height\": " << stream.channels[c]->height() << '}';
                first = false;
            }
            meta << "\n  }\n}\n";
            meta.close();
            if (!meta) throw std::runtime_error("Cannot write/close image metadata");
        }
        finished_ = true;
    } catch (...) { failed_ = true; throw; }
}
void write_image_text(std::ostream& out, const ImageStatistics& s) {
    out << "Raw AVHRR PGM output: files=" << s.files << ", streams=" << s.streams
        << ", accepted_scans=" << s.scans << '\n';
}
void write_image_json(std::ostream& out, const ImageStatistics& s) {
    out << "{\"files\": " << s.files << ", \"streams\": " << s.streams << ", \"scans\": " << s.scans
        << ", \"width\": 2048, \"maxval\": 1023, \"channel_rows\": {";
    for (std::size_t c = 0; c < s.channel_rows.size(); ++c)
        out << (c ? ", " : "") << '"' << avhrr_channel_name(static_cast<AvhrrChannel>(c))
            << "\": " << s.channel_rows[c];
    out << "}}";
}
} // namespace metop
