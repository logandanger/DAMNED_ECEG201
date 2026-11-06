#include "core/mirror/mirror_proto.hpp"

#include <cstring>

namespace danger::mirror {
namespace {

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// Header: magic(2) type(1) flags(1) seq(2) x(2) y(2) w(2) h(2) len(2) crc(2), little-endian.
void write_header(uint8_t* out, Type type, uint8_t flags, uint16_t seq, int x, int y, int w, int h, size_t len) {
    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = static_cast<uint8_t>(type);
    out[3] = flags;
    put16(out + 4, seq);
    put16(out + 6, static_cast<uint16_t>(x));
    put16(out + 8, static_cast<uint16_t>(y));
    put16(out + 10, static_cast<uint16_t>(w));
    put16(out + 12, static_cast<uint16_t>(h));
    put16(out + 14, static_cast<uint16_t>(len));
    put16(out + 16, crc16(out, 16));
}

size_t finish(uint8_t* out, size_t len) {
    put16(out + kHeaderSize + len, crc16(out + kHeaderSize, len));
    return kHeaderSize + len + 2;
}

}  // namespace

uint16_t crc16(const uint8_t* data, size_t size, uint16_t crc) {  // CCITT-FALSE
    for (size_t i = 0; i < size; ++i) {
        crc = static_cast<uint16_t>(crc ^ (data[i] << 8));
        for (int b = 0; b < 8; ++b) {
            crc = static_cast<uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

// Control byte c: c < 128 copies c+1 literal pixels; c >= 128 repeats the next pixel c-125 times (3..130).
size_t rle_encode(const uint16_t* px, int count, uint8_t* out, size_t cap) {
    size_t o = 0;
    int i = 0;
    while (i < count) {
        int run = 1;
        while (i + run < count && px[i + run] == px[i] && run < 130) ++run;
        if (run >= 3) {
            if (o + 3 > cap) return 0;
            out[o++] = static_cast<uint8_t>(run + 125);
            put16(out + o, px[i]);
            o += 2;
            i += run;
            continue;
        }
        int lit = 0;
        while (i + lit < count && lit < 128) {
            const int rest = count - (i + lit);
            if (rest >= 3 && px[i + lit] == px[i + lit + 1] && px[i + lit] == px[i + lit + 2]) break;
            ++lit;
        }
        if (o + 1 + static_cast<size_t>(lit) * 2 > cap) return 0;
        out[o++] = static_cast<uint8_t>(lit - 1);
        for (int k = 0; k < lit; ++k, o += 2) put16(out + o, px[i + k]);
        i += lit;
    }
    return o;
}

bool rle_decode(const uint8_t* in, size_t size, uint16_t* px, int count) {
    size_t i = 0;
    int o = 0;
    while (i < size) {
        const uint8_t c = in[i++];
        if (c < 128) {
            const int n = c + 1;
            if (o + n > count || i + static_cast<size_t>(n) * 2 > size) return false;
            for (int k = 0; k < n; ++k, i += 2) px[o++] = get16(in + i);
        } else {
            const int n = c - 125;
            if (o + n > count || i + 2 > size) return false;
            const uint16_t v = get16(in + i);
            i += 2;
            for (int k = 0; k < n; ++k) px[o++] = v;
        }
    }
    return o == count;
}

size_t build_hello(uint8_t* out, uint16_t seq, int width, int height) {
    write_header(out, Type::Hello, kVersion, seq, 0, 0, width, height, 0);
    return finish(out, 0);
}

size_t build_frame_end(uint8_t* out, uint16_t seq) {
    write_header(out, Type::FrameEnd, 0, seq, 0, 0, 0, 0, 0);
    return finish(out, 0);
}

size_t build_rect(uint8_t* out, uint16_t seq, const uint16_t* src, int stride, int x, int y, int w, int h) {
    uint16_t px[kMaxRectPixels];
    for (int r = 0; r < h; ++r) std::memcpy(px + r * w, src + r * stride, static_cast<size_t>(w) * 2);
    const int count = w * h;
    uint8_t* payload = out + kHeaderSize;
    size_t len = rle_encode(px, count, payload, static_cast<size_t>(count) * 2 - 1);
    uint8_t flags = kFlagRle;
    if (len == 0) {
        flags = 0;
        len = static_cast<size_t>(count) * 2;
        for (int i = 0; i < count; ++i) put16(payload + i * 2, px[i]);
    }
    write_header(out, Type::Rect, flags, seq, x, y, w, h, len);
    return finish(out, len);
}

void Decoder::fail() {
    ++errors_;
    have_seq_ = false;
    sink_->on_error();
}

void Decoder::packet_complete() {
    const uint8_t* b = buf_;
    const Type type = static_cast<Type>(b[2]);
    const uint16_t seq = get16(b + 4);
    const int x = get16(b + 6), y = get16(b + 8), w = get16(b + 10), h = get16(b + 12);
    const size_t len = get16(b + 14);

    if (have_seq_ && seq != static_cast<uint16_t>(last_seq_ + 1)) fail();
    have_seq_ = true;
    last_seq_ = seq;
    ++packets_;

    switch (type) {
        case Type::Hello: sink_->on_hello(w, h); break;
        case Type::FrameEnd: sink_->on_frame_end(); break;
        case Type::Rect: {
            const int count = w * h;
            if (count <= 0 || count > kMaxRectPixels) { fail(); break; }
            const uint8_t* payload = b + kHeaderSize;
            if (b[3] & kFlagRle) {
                if (!rle_decode(payload, len, px_, count)) { fail(); break; }
            } else {
                if (len != static_cast<size_t>(count) * 2) { fail(); break; }
                for (int i = 0; i < count; ++i) px_[i] = get16(payload + i * 2);
            }
            sink_->on_rect(x, y, w, h, px_);
            break;
        }
        default: break;  // unknown types from newer firmware are skipped
    }
}

void Decoder::feed(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        const uint8_t byte = data[i];
        if (have_ == 0 && byte != kMagic0) continue;
        if (have_ == 1 && byte != kMagic1) {
            have_ = byte == kMagic0 ? 1 : 0;
            continue;
        }
        buf_[have_++] = byte;
        if (have_ < need_) continue;

        if (need_ == kHeaderSize) {
            const size_t len = get16(buf_ + 14);
            if (crc16(buf_, 16) != get16(buf_ + 16) || len > static_cast<size_t>(kMaxPayload)) {
                // Not a header: rescan these bytes after the first one for a magic.
                size_t keep = 0;
                for (size_t k = 1; k < have_; ++k) {
                    if (buf_[k] == kMagic0) { keep = have_ - k; std::memmove(buf_, buf_ + k, keep); break; }
                }
                have_ = 0;
                if (keep) {
                    uint8_t tail[kHeaderSize];
                    std::memcpy(tail, buf_, keep);
                    feed(tail, keep);
                }
                continue;
            }
            need_ = kHeaderSize + len + 2;
            continue;
        }

        const size_t len = need_ - kHeaderSize - 2;
        if (crc16(buf_ + kHeaderSize, len) == get16(buf_ + kHeaderSize + len)) packet_complete();
        else fail();
        have_ = 0;
        need_ = kHeaderSize;
    }
}

}  // namespace danger::mirror
