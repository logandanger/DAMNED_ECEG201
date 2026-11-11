// Generative beat engine. Owns transport, seed capture, layers, drums, sampler and reverb.
// Everything runs inside render() on the audio task: fixed arrays, no heap, no locks, no waits.
//   Keys:      input task -> audio, wait-free ring (post_key)
//   Commands:  UI task    -> audio, wait-free ring (post_command)
//   BPM, reverb targets: any task, atomics
//   Status:    audio -> UI, relaxed atomics; fields may tear between each other, which is harmless
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/audio/reverb.hpp"
#include "core/audio/sampler.hpp"
#include "core/beat/drums.hpp"
#include "core/beat/layer.hpp"
#include "core/util/spsc.hpp"
#include "hal/controls.hpp"

namespace danger {

static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<uint8_t>::is_always_lock_free);

struct KeyEvent {
    uint8_t key;       // 0..11
    uint8_t velocity;  // 0 = release
};

enum class CommandType : uint8_t { SelectInstrument, ClearLayer, ClearAll };
struct Command {
    CommandType type;
    uint8_t arg;
};

struct LayerView {
    LayerState state;
    uint8_t instrument;
    uint8_t notes;
    bool current;
    float fade_in;  // 0..1 of the newest material
    float gain;     // 0..1; falls while fading out
};

struct EngineStatus {
    float bpm, reverb;
    float loop_pos;    // 0 .. kLoopSteps, in 16ths
    float drums;       // drum bus gain 0..1
    float peak;
    float commit_in;   // s until the pending seed commits; < 0 = nothing pending
    uint8_t instrument;
    uint8_t seed_notes;
    bool keys_down;
    uint8_t voices;
    LayerView layers[kLayerSlots];
    uint8_t order[kLayerSlots];  // slot indices, oldest first
    uint8_t layer_count;
};

class BeatEngine {
public:
    void init();

    // Input task.
    bool post_key(const KeyEvent& e) { return keys_.push(e); }
    // UI task.
    bool post_command(const Command& c) { return commands_.push(c); }
    // Any task.
    void set_bpm(float bpm);
    void set_reverb(float amount);
    float bpm_target() const { return bpm_target_.load(std::memory_order_relaxed); }
    float reverb_target() const { return reverb_target_.load(std::memory_order_relaxed); }
    EngineStatus status() const;

    // Audio task. `frames` is any count; blocks of cfg::kBlockFrames are typical.
    void render(int16_t* stereo, size_t frames);
    static void audio_callback(int16_t* stereo, size_t frames, void* user) {
        static_cast<BeatEngine*>(user)->render(stereo, frames);
    }

    // Test access. Audio task only.
    const Layer& layer(int slot) const { return layers_[slot]; }
    uint32_t global_step() const { return step_; }
    int loop_step() const { return static_cast<int>((step_ - origin_) & (cfg::kLoopSteps - 1)); }
    int active_voices() const { return sampler_.active(); }

private:
    struct CaptureNote {
        uint32_t step;
        float frac;
        float len;  // steps; < 0 while held
        uint8_t note, vel, key;
    };

    void handle_key(const KeyEvent& e);
    void handle_command(const Command& c);
    void commit_seed();
    int new_layer();
    void fade_layer(int slot, float seconds);
    void fade_drums(float target, float seconds);
    int current_slot() const;
    int active_layers() const;
    void on_step();
    void play_layers(int loop_step);
    void play_drums(int bar_step);
    void new_drum_bar(int bar);
    void publish(float peak);

    float frames_per_step() const;

    Sampler sampler_;
    Reverb reverb_;
    Layer layers_[kLayerSlots];
    DrumGen drums_;
    Rng rng_;

    // Transport. Loop step = (step_ - origin_) mod kLoopSteps. Only the rate changes with BPM,
    // never the phase, so a tempo change keeps every layer and the drums in sync.
    uint32_t step_ = 0;
    float frac_ = 0;
    uint32_t origin_ = 0;
    float bpm_ = cfg::kBpmDefault;
    uint64_t frames_ = 0;

    // Seed capture for the current instrument.
    CaptureNote capture_[cfg::kMaxCaptureNotes];
    int capture_count_ = 0;
    uint16_t keys_down_ = 0;
    uint64_t last_activity_ = 0;
    uint32_t key_voice_[hal::kKeyCount] = {};
    uint8_t key_capture_[hal::kKeyCount] = {};

    uint8_t instrument_ = 0;
    int current_ = -1;  // layer slot that collects seeds for the current instrument
    uint32_t layer_serial_ = 0;

    bool drums_on_ = false;
    float drum_gain_ = 0, drum_target_ = 0, drum_rate_ = 0;

    Spsc<KeyEvent, 64> keys_;
    Spsc<Command, 16> commands_;
    std::atomic<float> bpm_target_{cfg::kBpmDefault};
    std::atomic<float> reverb_target_{cfg::kReverbDefault};

    // Published status.
    std::atomic<float> st_loop_{0}, st_drums_{0}, st_peak_{0}, st_commit_{-1}, st_bpm_{cfg::kBpmDefault};
    std::atomic<uint8_t> st_inst_{0}, st_seed_{0}, st_keys_{0}, st_voices_{0}, st_count_{0};
    std::atomic<uint8_t> st_state_[kLayerSlots] = {}, st_layer_inst_[kLayerSlots] = {}, st_notes_[kLayerSlots] = {},
                         st_order_[kLayerSlots] = {};
    std::atomic<int8_t> st_current_{-1};
    std::atomic<float> st_fade_[kLayerSlots] = {}, st_gain_[kLayerSlots] = {};
};

}  // namespace danger
