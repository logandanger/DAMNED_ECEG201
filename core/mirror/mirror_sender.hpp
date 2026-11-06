// Dirty-tile tracker and packetizer. Platform-free: firmware and the simulator's --mirror-record share it.
// Holds a shadow of the screen, so a stalled link only coalesces updates and never blocks the UI.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/mirror/mirror_proto.hpp"

namespace danger::mirror {

constexpr int kScreenW = 320, kScreenH = 240;
constexpr int kTile = 16;
constexpr int kTilesX = kScreenW / kTile, kTilesY = kScreenH / kTile;
static_assert(kTilesX <= 31, "dirty row mask is 32 bits");
constexpr int kMaxRunTiles = kMaxRectPixels / (kTile * kTile);  // tiles per packet
constexpr int kBandPixels = kScreenW * kTile;

class Sender {
public:
    // Returns the number of bytes accepted. A short count stops the frame and re-marks what is left.
    using WriteFn = size_t (*)(const uint8_t* data, size_t size, void* user);

    // One buffer of kBandPixels per tile row. Separate bands, because after the engine and LVGL the
    // ESP32-S3 heap has no single free block as large as a whole screen.
    explicit Sender(uint16_t* const* bands) {
        for (int i = 0; i < kTilesY; ++i) bands_[i] = bands[i];
    }

    // UI task: copy a flushed rectangle into the shadow.
    void on_flush(int x, int y, int w, int h, const uint16_t* px);
    void mark_all();

    // Sender task: emit one frame's worth of dirty tiles. Returns false if the link stalled.
    bool pump(WriteFn write, void* user);

private:
    bool send(WriteFn write, void* user, size_t size);

    uint16_t* bands_[kTilesY];
    std::atomic<uint32_t> dirty_[kTilesY] = {};  // one bit per tile column
    std::atomic<bool> need_hello_{true};
    uint8_t packet_[kMaxPacket];
    uint16_t seq_ = 0;
};

}  // namespace danger::mirror
