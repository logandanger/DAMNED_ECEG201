// Polyphonic engine. All state lives inside the object: no heap, ever.
//   voices (osc -> SVF -> amp) -> drive -> chorus/phaser -> delay -> reverb -> volume -> soft clip
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/synth/dsp.hpp"
#include "core/synth/params.hpp"
#include "core/util/spsc.hpp"

namespace danger {

// The audio task shares these with other tasks and must never wait on a lock.
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<int16_t>::is_always_lock_free);
static_assert(std::atomic<uint8_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

struct NoteEvent {
    uint8_t note;
    uint8_t velocity;  // 0 = note off
};

struct VoiceControl {
    Wave wave;
    FilterType filter_type;
    float pitch_semitones, pulse_width, level;
    float cutoff_hz, resonance, env_octaves, key_track;
    EnvCoefs amp, filter;  // the filter envelope steps once per control period
};

class Voice {
public:
    void seed(uint32_t s) { osc_.seed(s); }
    void note_on(uint8_t note, uint8_t velocity, uint32_t age);
    void note_off() { amp_.gate_off(); fenv_.gate_off(); }
    void render(float* mix, int n, const VoiceControl& c);

    bool active() const { return !amp_.idle(); }
    bool releasing() const { return amp_.releasing(); }
    float level() const { return amp_.level(); }
    uint8_t note() const { return note_; }
    uint32_t age() const { return age_; }

private:
    Oscillator osc_;
    Svf filter_;
    Envelope amp_, fenv_;
    uint8_t note_ = 0;
    uint32_t age_ = 0;
    float velocity_gain_ = 0, dt_ = 0, pitch_key_ = -1000;
};

class Engine {
public:
    static constexpr int kScopeSize = 2048;

    void init(ParamStore* params);

    // Producer side, input task only.
    bool post_note(const NoteEvent& e) { return notes_.push(e); }
    // Any task.
    void panic() { panic_.store(true, std::memory_order_relaxed); }

    // Audio task. `frames` must be a multiple of kControlFrames.
    void render(int16_t* stereo, size_t frames);
    static void audio_callback(int16_t* stereo, size_t frames, void* user) {
        static_cast<Engine*>(user)->render(stereo, frames);
    }

    // Any task. The scope copy may tear; that is harmless for a display.
    void read_scope(int16_t* out, int count) const;
    uint8_t voice_mask() const { return voice_mask_.load(std::memory_order_relaxed); }
    float peak() const { return peak_.load(std::memory_order_relaxed); }

    const Voice& voice(int i) const { return voices_[i]; }

private:
    void update_controls();
    void handle_note(const NoteEvent& e);
    Voice* allocate(uint8_t note);

    ParamStore* params_ = nullptr;
    uint32_t seen_generation_ = 0xFFFFFFFFu;
    VoiceControl control_{};
    Voice voices_[kVoices];
    uint32_t age_ = 0;

    Drive drive_;
    ModFx mod_;
    Delay delay_;
    Reverb reverb_;
    float gain_ = 0, gain_target_ = 0;

    Spsc<NoteEvent, 64> notes_;
    std::atomic<bool> panic_{false};
    std::atomic<int16_t> scope_[kScopeSize];
    std::atomic<uint32_t> scope_write_{0};
    std::atomic<uint8_t> voice_mask_{0};
    std::atomic<float> peak_{0};
};

}  // namespace danger
