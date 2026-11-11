// One instrument layer: its quantized seed, its mutation state and its fade. Audio task only.
#pragma once
#include <cstdint>

#include "core/beat/rng.hpp"
#include "core/config/beat_config.hpp"

namespace danger {

enum class LayerState : uint8_t { Free, Active, FadingOut };

struct SeedNote {
    uint8_t step;  // 0 .. kLoopSteps-1
    uint8_t note;
    uint8_t vel;
    uint8_t len;   // steps, 1 .. kMaxNoteSteps
};

struct PatternNote {
    enum : uint8_t { kAdded = 1, kMuted = 2 };
    uint32_t birth;  // global step of the commit: fade-in and mutation eligibility count from here
    uint8_t step, note, vel, len;
    int8_t nudge;    // rhythm nudge, steps
    int8_t octave;   // octave shift
    uint8_t flags;

    int eff_step() const { return (step + nudge + cfg::kLoopSteps) % cfg::kLoopSteps; }
    int pitch() const { return note + 12 * octave; }
    bool added() const { return flags & kAdded; }
    bool muted() const { return flags & kMuted; }
    bool drifted() const { return added() || muted() || nudge || octave; }
};

class Layer {
public:
    void reset(uint8_t instrument, uint32_t serial, uint32_t rng_seed);
    // Merges quantized notes into the seed. Returns how many were taken.
    int add_seed(const SeedNote* notes, int count, uint32_t birth);
    // Once per loop, at its start. intensity 0..1.
    void mutate(uint32_t now, float intensity);

    // 0..1 fade-in of the newest material, as shown on screen.
    float fade_in(uint32_t now) const;
    static float note_ramp(const PatternNote& n, uint32_t now);  // 0..1 over kFadeInBars

    int count() const { return count_; }
    const PatternNote& note(int i) const { return notes_[i]; }
    int seed_count() const { return seed_count_; }

    LayerState state = LayerState::Free;
    uint8_t instrument = 0;
    uint32_t serial = 0;       // creation order, for eviction
    uint32_t first_birth = 0;  // mutation intensity counts from here
    float gain = 0, target = 0, rate = 0;  // per frame

private:
    void refresh_stats();
    int find_at(int eff_step, int pitch) const;
    bool occupied(int eff_step) const;
    int pick(bool (*pred)(const PatternNote&), uint32_t now);
    void remove(int i);
    int added_count() const;
    int max_added() const;

    bool op_drop(uint32_t now);
    bool op_restore(uint32_t now);
    bool op_add(uint32_t now);
    bool op_octave(uint32_t now);
    bool op_nudge(uint32_t now);
    bool op_passing(uint32_t now);

    PatternNote notes_[cfg::kMaxPatternNotes];
    int count_ = 0, seed_count_ = 0;
    uint8_t pitches_[16] = {};  // distinct seed pitches, in played order
    int pitch_count_ = 0;
    uint16_t pc_mask_ = 0;      // seed pitch classes
    int low_ = 60, high_ = 60;
    int mean_vel_ = 100, mean_len_ = 2;
    Rng rng_;
};

}  // namespace danger
