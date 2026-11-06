// Simulator-only additions to the HAL.
#pragma once
#include <cstdint>

#include "hal/hal.hpp"

namespace hal::sim {

const uint16_t* framebuffer();  // 320 x 240 RGB565
const uint8_t* leds();          // 32 RGB triples
void emit(const ControlEvent& e);
void disable_audio();

using FlushHook = void (*)(int x, int y, int w, int h, const uint16_t* px, void* user);
void set_flush_hook(FlushHook hook, void* user);

}  // namespace hal::sim
