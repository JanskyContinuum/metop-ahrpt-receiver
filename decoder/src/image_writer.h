#pragma once

#include "avhrr_payload.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <tuple>

namespace metop {
// Bounded-memory raster spool. finish() writes a single P5 image with its actual
// height. No output is produced for zero rows. Never overwrites an existing file.
class RawPgmWriter {
public:
    explicit RawPgmWriter(std::filesystem::path destination);
    ~RawPgmWriter();
    RawPgmWriter(const RawPgmWriter&) = delete;
    RawPgmWriter& operator=(const RawPgmWriter&) = delete;
    void append(std::span<const std::uint16_t> line);
    void finish();
    std::uint64_t height() const noexcept { return height_; }
private:
    std::filesystem::path destination_, temporary_;
    std::ofstream raster_;
    std::uint64_t height_ = 0;
    bool owns_temporary_ = false, owns_incomplete_output_ = false, finished_ = false, failed_ = false;
};

struct ImageStatistics {
    std::uint64_t streams = 0, files = 0, scans = 0;
    std::array<std::uint64_t, 6> channel_rows{}; // 1,2,3A,3B,4,5; totals across streams.
};

// Destination directory must be absent or empty. Each (SCID, VCID, replay)
// identity has its own channel files, row provenance CSV and metadata.json.
// add() receives only scans accepted by AvhrrScanProcessor, in arrival order.
class AvhrrImageWriter {
public:
    AvhrrImageWriter(std::filesystem::path directory, bool rs_applied);
    ~AvhrrImageWriter();
    AvhrrImageWriter(const AvhrrImageWriter&) = delete;
    AvhrrImageWriter& operator=(const AvhrrImageWriter&) = delete;
    void add(const ReassembledPacket& packet, const AvhrrScan& scan);
    void finish();
    const ImageStatistics& statistics() const noexcept { return statistics_; }
private:
    struct Stream;
    using Key = std::tuple<std::uint8_t, std::uint8_t, bool>;
    std::filesystem::path directory_;
    bool rs_applied_, finished_ = false, failed_ = false;
    std::map<Key, std::unique_ptr<Stream>> streams_;
    ImageStatistics statistics_;
};
void write_image_text(std::ostream&, const ImageStatistics&);
void write_image_json(std::ostream&, const ImageStatistics&);
} // namespace metop
