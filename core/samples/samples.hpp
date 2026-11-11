// Preloaded samples. Data is generated into sample_data.cpp by tools/pack_samples.py; const, so it
// stays in flash on the ESP32-S3 and is read through the cache.
#pragma once
#include <cstdint>

namespace danger {

constexpr int kSampleRateHz = 22050;  // every stored sample

struct Sample {
    const char* name;
    const int16_t* data;
    uint32_t length;
    uint32_t loop_start, loop_end;  // sustain loop while the note is held; equal = one-shot
    uint8_t root;                   // MIDI note the sample sounds at when played unshifted
    float release_s;                // fade after note off
};

enum class Drum : uint8_t { Kick, Snare, Hat, Crash };
constexpr int kDrumCount = 4;

extern const Sample kDrumSamples[kDrumCount];
extern const Sample kInstrumentSamples[];
extern const int kInstrumentCount;

}  // namespace danger
