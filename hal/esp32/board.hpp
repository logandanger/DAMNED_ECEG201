// DAMNED board wiring, from hardware/netlist/esp32_pinmap.csv and README sections 5 to 10.
#pragma once
#include <cstdint>

namespace board {

// I2S (J3). No MCLK is routed.
constexpr int kI2sBclk = 7, kI2sWs = 5, kI2sDout = 6;

// Display (J6), shared SPI bus.
constexpr int kSpiMosi = 35, kSpiSck = 36, kSpiMiso = 37;
constexpr int kLcdCs = 42, kLcdDc = 40, kLcdRst = 41, kLcdBacklight = 2;
constexpr uint32_t kLcdClockHz = 40000000;

// Debounced buttons: active-low, driven by the MC14490.
constexpr int kBtnUp = 12, kBtnDown = 11, kBtnLeft = 14, kBtnRight = 13, kBtnA = 21, kBtnB = 47;

// I2C. On-board pull-ups, plus those on each breakout.
constexpr int kI2cSda = 3, kI2cScl = 4;
constexpr uint32_t kI2cHz = 400000;
constexpr uint8_t kAw9523Addr = 0x58;
constexpr uint8_t kEncoderAddr[2] = {0x36, 0x37};

// AW9523 pin (0-7 = P0, 8-15 = P1) for each key. [verify] against the actual key wiring.
constexpr uint8_t kKeyPin[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// Sign of a seesaw delta for a clockwise turn. [verify] on the bench.
constexpr int kEncoderSign = 1;

// LED chain.
constexpr int kLedClk = 17, kLedData = 18;
constexpr int kLedCount = 20;
constexpr uint8_t kLedGlobalBrightness = 8;  // of 31: hard cap for the USB budget, README risk R2

}  // namespace board

namespace hal {
void board_init();  // display, LEDs and GPIO; call before danger::App::init()
}

// Mirror hook, implemented in firmware/main/mirror_task.cpp when DANGER_MIRROR=1.
void mirror_on_flush(int x, int y, int w, int h, const uint16_t* px);
