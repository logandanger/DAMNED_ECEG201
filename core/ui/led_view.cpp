#include "core/ui/led_view.hpp"

#include <cmath>
#include <cstring>

namespace danger {
namespace {

void set(uint8_t* rgb, int i, int r, int g, int b, float brightness) {
    rgb[i * 3 + 0] = static_cast<uint8_t>(static_cast<float>(r) * brightness);
    rgb[i * 3 + 1] = static_cast<uint8_t>(static_cast<float>(g) * brightness);
    rgb[i * 3 + 2] = static_cast<uint8_t>(static_cast<float>(b) * brightness);
}

}  // namespace

void led_view_render(uint8_t* rgb, float peak, uint8_t voice_mask, uint16_t key_mask, float brightness) {
    std::memset(rgb, 0, kLedCount * 3);
    // Level over a 48 dB span, one cell per 3 dB.
    const float db = peak > 1e-5f ? 20.0f * std::log10(peak) : -100.0f;
    const int lit = static_cast<int>((db + 48.0f) / 3.0f);
    for (int i = 0; i < kLedColumns && i < lit; ++i) {
        if (i == kLedColumns - 1) set(rgb, i, 255, 0, 15, brightness);
        else set(rgb, i, 255, 255, 255, brightness);
    }
    for (int i = 0; i < 4; ++i) {
        if (voice_mask & (1u << i)) set(rgb, kLedColumns + i, 255, 0, 15, brightness);
    }
    for (int i = 0; i < 12; ++i) {
        if (key_mask & (1u << i)) set(rgb, kLedColumns + 4 + i, 255, 255, 255, brightness);
    }
}

}  // namespace danger
