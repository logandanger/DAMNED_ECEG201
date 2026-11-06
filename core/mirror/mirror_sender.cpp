#include "core/mirror/mirror_sender.hpp"

#include <cstring>

namespace danger::mirror {

void Sender::on_flush(int x, int y, int w, int h, const uint16_t* px) {
    for (int r = 0; r < h; ++r) {
        const int row = y + r;
        uint16_t* dst = bands_[row / kTile] + (row % kTile) * kScreenW + x;
        std::memcpy(dst, px + r * w, static_cast<size_t>(w) * 2);
    }
    const int c0 = x / kTile, c1 = (x + w - 1) / kTile;
    const uint32_t mask = ((1u << (c1 - c0 + 1)) - 1u) << c0;
    for (int t = y / kTile; t <= (y + h - 1) / kTile; ++t) dirty_[t].fetch_or(mask, std::memory_order_relaxed);
}

void Sender::mark_all() {
    need_hello_.store(true, std::memory_order_relaxed);
    for (auto& row : dirty_) row.store((1u << kTilesX) - 1u, std::memory_order_relaxed);
}

bool Sender::send(WriteFn write, void* user, size_t size) {
    ++seq_;
    return write(packet_, size, user) == size;
}

bool Sender::pump(WriteFn write, void* user) {
    if (need_hello_.exchange(false, std::memory_order_relaxed)) {
        if (!send(write, user, build_hello(packet_, static_cast<uint16_t>(seq_ + 1), kScreenW, kScreenH))) {
            need_hello_.store(true, std::memory_order_relaxed);
            return false;
        }
    }
    bool any = false;
    for (int ty = 0; ty < kTilesY; ++ty) {
        uint32_t row = dirty_[ty].exchange(0, std::memory_order_relaxed);
        while (row) {
            int start = 0;
            while (!(row & (1u << start))) ++start;
            int run = 1;
            while (start + run < kTilesX && run < kMaxRunTiles && (row & (1u << (start + run)))) ++run;
            const size_t size = build_rect(packet_, static_cast<uint16_t>(seq_ + 1), bands_[ty] + start * kTile, kScreenW,
                                           start * kTile, ty * kTile, run * kTile, kTile);
            if (!send(write, user, size)) {
                dirty_[ty].fetch_or(row, std::memory_order_relaxed);
                return false;
            }
            row &= ~(((1u << run) - 1u) << start);
            any = true;
        }
    }
    if (any) return send(write, user, build_frame_end(packet_, static_cast<uint16_t>(seq_ + 1)));
    return true;
}

}  // namespace danger::mirror
