// Hardware abstraction. core/ sees only this header; hal/esp32 and hal/sim implement it.
#pragma once
#include <cstddef>
#include <cstdint>

#include "hal/controls.hpp"

namespace hal {

// Audio. The callback renders exactly `frames` interleaved stereo int16 frames.
// It runs on the audio task and must not allocate, lock, or log.
using AudioCallback = void (*)(int16_t* stereo, size_t frames, void* user);
void audio_start(AudioCallback cb, void* user);
float audio_load();  // fraction of the block period spent rendering, 0..1

// Display. Returns when `pixels` (RGB565, native endian, tightly packed) may be reused.
// The implementation may modify `pixels`.
void display_flush(int x, int y, int w, int h, uint16_t* pixels);

// LEDs. `rgb` holds `count` RGB triples in strip order; LEDs past the physical count are ignored.
void leds_write(const uint8_t* rgb, int count);

// Input. The sink is called from the input task, never from the audio task.
using ControlSink = void (*)(const ControlEvent& event, void* user);
void input_start(ControlSink sink, void* user);

uint32_t millis();

}  // namespace hal
