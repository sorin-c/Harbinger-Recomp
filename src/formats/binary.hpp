// Little-endian reader for the engine's serialized resource formats
// (.itf, .mgr, .fnt, .acr, .itm). Throws std::runtime_error on overrun.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace harbinger {

class ByteReader {
public:
    ByteReader(const std::vector<uint8_t>& data, const char* format) : d_(data), format_(format) {}

    uint8_t u8() {
        need(1);
        return d_[pos_++];
    }
    uint16_t u16() {
        need(2);
        const uint16_t v = uint16_t(d_[pos_] | (d_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }
    int16_t i16() { return int16_t(u16()); }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | (uint32_t(u16()) << 16);
    }
    int32_t i32() { return int32_t(u32()); }
    std::vector<uint8_t> bytes(size_t n) {
        need(n);
        std::vector<uint8_t> out(d_.begin() + long(pos_), d_.begin() + long(pos_ + n));
        pos_ += n;
        return out;
    }

    // The engine's string encoding (FUN_0050fe60): u32 length, the characters,
    // then a NUL that the length does not count.
    std::string string() { return readString(u32() + size_t(1)); }
    // The variant used for .mgr paths: the length counts the NUL.
    std::string stringNulCounted() { return readString(u32()); }

    size_t pos() const { return pos_; }
    size_t remaining() const { return d_.size() - pos_; }
    void expectEnd() const {
        if (pos_ != d_.size()) fail("trailing bytes");
    }
    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string(format_) + ": " + what + " at offset " + std::to_string(pos_));
    }

private:
    void need(size_t n) const {
        if (n > d_.size() - pos_) fail("truncated");
    }
    std::string readString(size_t withNul) {
        if (withNul == 0) fail("empty string");
        need(withNul);
        if (d_[pos_ + withNul - 1] != 0) fail("string not NUL-terminated");
        std::string s(reinterpret_cast<const char*>(d_.data() + pos_), withNul - 1);
        pos_ += withNul;
        return s;
    }

    const std::vector<uint8_t>& d_;
    const char* format_;
    size_t pos_ = 0;
};

}  // namespace harbinger
