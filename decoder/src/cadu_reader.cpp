#include "cadu_reader.h"

#include <algorithm>
#include <stdexcept>

namespace metop {

std::uint8_t CaduReader::byte(std::size_t offset) const {
    return buffer_[(begin_ + offset) % buffer_.size()];
}

bool CaduReader::ensure(std::size_t count) {
    if (count > buffer_.size()) {
        throw std::logic_error("CADU lookahead exceeds buffer capacity");
    }
    while (size_ < count && !input_eof_) {
        const auto end = (begin_ + size_) % buffer_.size();
        const auto requested = std::min(count - size_, buffer_.size() - end);
        input_.read(reinterpret_cast<char*>(buffer_.data() + end),
                    static_cast<std::streamsize>(requested));
        const auto received = input_.gcount();
        if (input_.bad() || (input_.fail() && !input_.eof())) {
            throw std::runtime_error("I/O failure while reading CADU input");
        }
        size_ += static_cast<std::size_t>(received);
        input_eof_ = input_.eof();
    }
    return size_ >= count;
}

bool CaduReader::has_marker(std::size_t offset) const {
    if (offset > size_ || size_ - offset < attached_sync_marker.size()) {
        return false;
    }
    for (std::size_t i = 0; i < attached_sync_marker.size(); ++i) {
        if (byte(offset + i) != attached_sync_marker[i]) {
            return false;
        }
    }
    return true;
}

void CaduReader::consume(std::size_t count) {
    begin_ = (begin_ + count) % buffer_.size();
    size_ -= count;
    statistics_.bytes_consumed += count;
}

void CaduReader::finish() {
    statistics_.trailing_bytes += size_;
    consume(size_);
    statistics_.reached_eof = true;
}

std::optional<Cadu> CaduReader::next() {
    if (statistics_.reached_eof) {
        return std::nullopt;
    }
    for (;;) {
        if (!ensure(cadu_size)) {
            finish();
            return std::nullopt;
        }
        if (!has_marker()) {
            if (!recovering_) {
                ++statistics_.asm_failures;
                recovering_ = true;
            }
            ++statistics_.skipped_bytes;
            consume(1);
            continue;
        }
        if (recovering_) {
            // At EOF, an isolated marker cannot establish recovered alignment.
            if (!ensure(cadu_size + attached_sync_marker.size())) {
                ++statistics_.unconfirmed_candidates;
                finish();
                return std::nullopt;
            }
            if (!has_marker(cadu_size)) {
                ++statistics_.rejected_candidates;
                ++statistics_.skipped_bytes;
                consume(1);
                continue;
            }
            recovering_ = false;
            ++statistics_.resyncs;
        }
        Cadu result;
        result.file_offset = statistics_.bytes_consumed;
        result.index = statistics_.cadus_read;
        for (std::size_t i = 0; i < cadu_size; ++i) {
            result.bytes[i] = byte(i);
        }
        consume(cadu_size);
        ++statistics_.cadus_read;
        ++statistics_.valid_asm;
        return result;
    }
}

} // namespace metop
