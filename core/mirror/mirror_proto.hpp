// Framebuffer mirror protocol. Spec: docs/mirror-protocol.md. Used by firmware and by the simulator.
#pragma once
#include <cstddef>
#include <cstdint>

namespace danger::mirror {

constexpr uint8_t kMagic0 = 0xD5, kMagic1 = 0x4D;
constexpr uint8_t kVersion = 1;
constexpr int kHeaderSize = 18;  // including the header CRC
constexpr int kMaxRectPixels = 4096;
constexpr int kMaxPayload = kMaxRectPixels * 2 + kMaxRectPixels / 64 + 64;  // RLE worst case fits
constexpr int kMaxPacket = kHeaderSize + kMaxPayload + 2;

enum class Type : uint8_t { Hello = 1, Rect = 2, FrameEnd = 3 };
constexpr uint8_t kFlagRle = 0x01;

// Host to device, four ASCII bytes. Either one also counts as keepalive.
constexpr char kCmdKeepalive[4] = {'D', 'M', 'K', 'A'};
constexpr char kCmdFullFrame[4] = {'D', 'M', 'F', 'F'};
constexpr uint32_t kSessionTimeoutMs = 3000;

uint16_t crc16(const uint8_t* data, size_t size, uint16_t crc = 0xFFFF);

// PackBits over 16-bit pixels. Returns bytes written, or 0 if the output would not fit.
size_t rle_encode(const uint16_t* px, int count, uint8_t* out, size_t cap);
// Returns false on malformed input or wrong pixel count.
bool rle_decode(const uint8_t* in, size_t size, uint16_t* px, int count);

// Builders return the packet size. `out` must hold kMaxPacket bytes.
size_t build_hello(uint8_t* out, uint16_t seq, int width, int height);
size_t build_frame_end(uint8_t* out, uint16_t seq);
// `src` is the rectangle's top-left pixel, `stride` its row pitch in pixels. (x, y) is its screen position.
// Uses RLE when smaller. w*h <= kMaxRectPixels.
size_t build_rect(uint8_t* out, uint16_t seq, const uint16_t* src, int stride, int x, int y, int w, int h);

// Incremental stream parser. Anything that is not a valid packet, such as log text, is skipped.
class Decoder {
public:
    struct Sink {
        virtual ~Sink() = default;
        virtual void on_hello(int width, int height) = 0;
        virtual void on_rect(int x, int y, int w, int h, const uint16_t* px) = 0;
        virtual void on_frame_end() = 0;
        virtual void on_error() = 0;  // CRC failure or sequence gap: request a full frame
    };

    explicit Decoder(Sink* sink) : sink_(sink) {}
    void feed(const uint8_t* data, size_t size);

    uint32_t packets() const { return packets_; }
    uint32_t errors() const { return errors_; }

private:
    void packet_complete();
    void fail();

    Sink* sink_;
    uint8_t buf_[kMaxPacket];
    uint16_t px_[kMaxRectPixels];
    size_t have_ = 0, need_ = kHeaderSize;
    bool have_seq_ = false;
    uint16_t last_seq_ = 0;
    uint32_t packets_ = 0, errors_ = 0;
};

}  // namespace danger::mirror
