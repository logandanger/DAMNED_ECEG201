// Polyphonic sample player. Fixed voice pool, no heap. Audio task only.
// Every start ramps in over kVoiceAttack; every stop (release, steal, choke) ramps out, so no clicks.
#pragma once
#include <cstdint>

#include "core/config/beat_config.hpp"
#include "core/samples/samples.hpp"

namespace danger {

// Gain buses: one per layer slot, then generated drums, then live play (always unity).
constexpr int kLayerSlots = cfg::kMaxLayers + 2;  // room for layers that are still fading out
constexpr int kBusDrums = kLayerSlots;
constexpr int kBusLive = kLayerSlots + 1;
constexpr int kBusCount = kLayerSlots + 2;

struct NoteStart {
    const Sample* sample;
    int note;              // MIDI pitch to sound at; the sample plays at sample->root unshifted
    float gain;            // includes velocity
    uint8_t bus;
    uint8_t choke_tag;     // nonzero: a new start with the same tag fades older ones (drums)
    uint32_t gate_frames;  // 0 = held until release(); otherwise released after this many frames
};

class Sampler {
public:
    static constexpr uint32_t kNoVoice = 0xFFFFFFFFu;

    // Returns a handle for release(), or kNoVoice.
    uint32_t start(const NoteStart& n);
    void release(uint32_t handle);
    void kill_bus(uint8_t bus);  // short fade of every voice on the bus

    // Adds n frames into mono. Bus gains ramp linearly from g0 to g1 across the chunk.
    void render(float* mono, int n, const float* g0, const float* g1);

    int active() const;

private:
    enum class Stage : uint8_t { Idle, Attack, Hold, Release, Steal };
    struct Voice {
        const Sample* sample = nullptr;
        uint32_t pos = 0, inc = 0;  // Q20.12, in source samples
        float amp = 0, step = 0, gain = 0;
        uint32_t gate = 0;
        uint32_t serial = 0;
        Stage stage = Stage::Idle;
        uint8_t bus = 0, tag = 0;
        bool has_pending = false;
        NoteStart pending{};
    };

    void begin(Voice& v, const NoteStart& n);
    void fade_out(Voice& v, float seconds, Stage stage);
    Voice* allocate();

    Voice voices_[cfg::kVoices];
    uint32_t serial_ = 0;
};

}  // namespace danger
