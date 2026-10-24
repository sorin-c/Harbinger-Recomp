#include "formats/gim.hpp"

#include <stdexcept>

namespace harbinger::gim {
namespace {

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& d) : d_(d) {}
    void need(size_t n) const {
        if (pos_ + n > d_.size()) throw std::runtime_error("gim: truncated");
    }
    uint8_t u8() {
        need(1);
        return d_[pos_++];
    }
    uint16_t u16() {
        need(2);
        uint16_t v = uint16_t(d_[pos_] | (d_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        uint32_t lo = u16();
        return lo | (uint32_t(u16()) << 16);
    }
    size_t pos() const { return pos_; }
    const uint8_t* at() const { return d_.data() + pos_; }
    void skip(size_t n) {
        need(n);
        pos_ += n;
    }

private:
    const std::vector<uint8_t>& d_;
    size_t pos_ = 0;
};

// One RLE row: u16 byteLength (including itself), u8 segmentCount, then per
// segment u8 transparentRun, u8 literalCount, literal palette indices.
// A literal count byte of 0xFF stands for 254 pixels.
void decodeRleFrame(const uint8_t* d, size_t size, size_t& pos, const Image& img, Frame& f) {
    f.index.assign(size_t(img.width) * img.height, 0);
    f.opaque.assign(f.index.size(), 0);
    for (size_t y = 0; y < img.height; ++y) {
        if (pos + 3 > size) throw std::runtime_error("gim: truncated row");
        const size_t rowEnd = pos + (d[pos] | (d[pos + 1] << 8));
        const uint8_t segments = d[pos + 2];
        size_t q = pos + 3, x = 0;
        if (rowEnd > size) throw std::runtime_error("gim: row overruns data");
        for (uint8_t s = 0; s < segments; ++s) {
            if (q + 2 > rowEnd) throw std::runtime_error("gim: truncated segment");
            x += d[q];
            const size_t literal = d[q + 1] == 0xFF ? 254 : d[q + 1];
            q += 2;
            if (q + literal > rowEnd || x + literal > img.width) throw std::runtime_error("gim: segment overruns row");
            for (size_t i = 0; i < literal; ++i, ++x) {
                f.index[y * img.width + x] = d[q + i];
                f.opaque[y * img.width + x] = 1;
            }
            q += literal;
        }
        if (q != rowEnd || x != img.width) throw std::runtime_error("gim: row length mismatch");
        pos = rowEnd;
    }
}

}  // namespace

Image decode(const std::vector<uint8_t>& data) {
    Reader r(data);
    Image img;
    img.width = r.u16();
    img.height = r.u16();
    img.pitch = r.u16();
    img.kind = r.u8();
    img.flags = r.u8();
    img.originX = int16_t(r.u16());
    img.originY = int16_t(r.u16());

    if (img.kind == 1 && (img.flags & 0x20)) {
        const uint16_t count = r.u16();
        img.palette.resize(count);
        for (auto& c : img.palette) {
            c.b = r.u8();
            c.g = r.u8();
            c.r = r.u8();
            c.a = r.u8();
        }
    }
    if (img.encoding() & 3) {
        r.need(8);
        for (auto& b : img.extra) b = r.u8();
    }
    if (img.flags & 0x10) {
        const uint32_t size = r.u32();
        r.need(size);
        const uint8_t* d = r.at();
        if (img.isRle()) {
            // Sprite sheets ("fsp") hold several frames back to back.
            size_t pos = 0;
            while (pos < size) {
                img.frames.emplace_back();
                decodeRleFrame(d, size, pos, img, img.frames.back());
            }
        } else {
            if (size_t(img.pitch) * img.height != size || img.pitch < img.width)
                throw std::runtime_error("gim: raw size mismatch");
            Frame f;
            f.index.reserve(size_t(img.width) * img.height);
            for (size_t y = 0; y < img.height; ++y)
                f.index.insert(f.index.end(), d + y * img.pitch, d + y * img.pitch + img.width);
            f.opaque.assign(f.index.size(), 1);
            img.frames.push_back(std::move(f));
        }
        r.skip(size);
    }
    if (r.pos() != data.size()) throw std::runtime_error("gim: trailing bytes");
    return img;
}

}  // namespace harbinger::gim
