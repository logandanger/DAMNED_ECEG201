// Light strip content, 2 rows of 16, row-major. Row 1 is the output level.
// Row 2 is voices in cells 0-3, then the 12 keys in cells 4-15, so the 20 LEDs
// on the board (the first 20 cells) still show level and voices.
#pragma once
#include <cstdint>

namespace danger {

constexpr int kLedColumns = 16, kLedRows = 2, kLedCount = kLedColumns * kLedRows;

void led_view_render(uint8_t* rgb, float peak, uint8_t voice_mask, uint16_t key_mask, float brightness);

}  // namespace danger
