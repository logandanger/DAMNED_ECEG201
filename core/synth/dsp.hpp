// DSP blocks. Float32 throughout (the LX7 has a single-precision FPU).
// Inner loops avoid libm and division; coefficients are computed at control rate or on change.
#pragma once
#include <cstdint>
#include <cstring>

#include "core/synth/params.hpp"

namespace danger {

constexpr int kSampleRate = 44100;
constexpr int kBlockFrames = 128;   // 2.90 ms, one audio callback
constexpr int kControlFrames = 32;  // 0.73 ms, modulation update period
constexpr int kVoices = 4;
constexpr float kInvSampleRate = 1.0f / kSampleRate;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// y = x - (4/27) x^3: unity slope at 0, reaches +-1 with zero slope at x = +-1.5.
inline float soft_clip(float x) {
    x = clampf(x, -1.5f, 1.5f);
    return x * (1.0f - 0.148148148f * x * x);
}

void dsp_init();                 // fills the sine table; call once before audio starts
float sine_lookup(float phase);  // phase in [0, 1)

// ---- Envelope -------------------------------------------------------------------------------
struct EnvCoefs {
    float attack_inc;  // linear ramp per step
    float decay_coef;  // exponential: falls 60 dB of the span in the set time
    float sustain;
    float release_coef;
};
EnvCoefs env_coefs(float attack_ms, float decay_ms, float sustain, float release_ms, float steps_per_second);

class Envelope {
public:
    enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };

    void gate_on() { stage_ = Stage::Attack; }  // continues from the current level: no click
    void gate_off() { if (stage_ != Stage::Idle) stage_ = Stage::Release; }
    bool idle() const { return stage_ == Stage::Idle; }
    bool releasing() const { return stage_ == Stage::Release; }
    float level() const { return level_; }

    float next(const EnvCoefs& c) {
        switch (stage_) {
            case Stage::Attack:
                level_ += c.attack_inc;
                if (level_ >= 1.0f) { level_ = 1.0f; stage_ = Stage::Decay; }
                break;
            case Stage::Decay:
                level_ = c.sustain + (level_ - c.sustain) * c.decay_coef;
                if (level_ - c.sustain < 1e-4f) { level_ = c.sustain; stage_ = Stage::Sustain; }
                break;
            case Stage::Sustain:
                level_ = c.sustain;
                if (level_ <= 0.0f) stage_ = Stage::Idle;
                break;
            case Stage::Release:
                level_ *= c.release_coef;
                if (level_ < 1e-4f) { level_ = 0.0f; stage_ = Stage::Idle; }
                break;
            case Stage::Idle: break;
        }
        return level_;
    }

private:
    Stage stage_ = Stage::Idle;
    float level_ = 0.0f;
};

// ---- Oscillator -----------------------------------------------------------------------------
class Oscillator {
public:
    void seed(uint32_t s) { rng_ = s | 1u; }
    void fill(float* out, int n, Wave wave, float dt, float pulse_width);

private:
    float white() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return static_cast<float>(static_cast<int32_t>(rng_)) * (1.0f / 2147483648.0f);
    }
    float phase_ = 0.0f;
    uint32_t rng_ = 0x9E3779B9u;
    float b0_ = 0, b1_ = 0, b2_ = 0;
};

// ---- State-variable filter (trapezoidal, stable under modulation) ---------------------------
class Svf {
public:
    void set(float cutoff_hz, float resonance);
    void reset() { ic1_ = ic2_ = 0; }
    float low(float x) { step(x); return v2_; }
    float band(float x) { step(x); return v1_; }
    float high(float x) { step(x); return x - k_ * v1_ - v2_; }

private:
    void step(float x) {
        const float v3 = x - ic2_;
        v1_ = a1_ * ic1_ + a2_ * v3;
        v2_ = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0f * v1_ - ic1_;
        ic2_ = 2.0f * v2_ - ic2_;
    }
    float a1_ = 1, a2_ = 0, a3_ = 0, k_ = 2;
    float ic1_ = 0, ic2_ = 0, v1_ = 0, v2_ = 0;
};

// ---- Drive / bitcrush -----------------------------------------------------------------------
class Drive {
public:
    void configure(DriveMode mode, float amount, int bits, int rate_div);
    void process(float* x, int n);

private:
    DriveMode mode_ = DriveMode::Off;
    float pre_ = 1, post_ = 1, levels_ = 128, inv_levels_ = 1.0f / 128;
    int rate_ = 1, count_ = 0;
    float held_ = 0;
};

// ---- Chorus / phaser ------------------------------------------------------------------------
class ModFx {
public:
    void configure(ModMode mode, float rate_hz, float depth, float mix);
    void process(const float* in, float* left, float* right, int n);

private:
    void chorus(const float* in, float* left, float* right, int n);
    void phaser(const float* in, float* left, float* right, int n);

    static constexpr int kLine = 1024;  // 23.2 ms, power of two
    float line_[kLine] = {};
    int write_ = 0;
    ModMode mode_ = ModMode::Off;
    float lfo_ = 0, lfo_inc_ = 0, depth_ = 0, wet_ = 0, dry_ = 1;
    float ap_[2][4] = {};
    float fb_[2] = {};
};

// ---- Delay: mono int16 line, 45 kB ----------------------------------------------------------
class Delay {
public:
    void configure(float time_ms, float feedback, float mix);
    void process(float* left, float* right, int n);

private:
    static constexpr int kLine = 22528;  // 0.51 s
    int16_t line_[kLine] = {};
    int write_ = 0;
    float time_ = 13230, target_ = 13230, feedback_ = 0, mix_ = 0;
    bool active_ = false;
};

// ---- Reverb: 4 damped combs, 2 series allpasses, one allpass per output, 26 kB --------------
class Reverb {
public:
    void configure(float size, float damp, float mix);
    void process(float* left, float* right, int n);

private:
    static constexpr int kCombLen[4] = {1116, 1188, 1277, 1356};
    static constexpr int kApLen[4] = {225, 556, 341, 441};
    float comb_[1116 + 1188 + 1277 + 1356] = {};
    float ap_[225 + 556 + 341 + 441] = {};
    int comb_pos_[4] = {}, ap_pos_[4] = {};
    float comb_lp_[4] = {};
    float feedback_ = 0.8f, damp_ = 0.3f, mix_ = 0;
    bool active_ = false;
};

}  // namespace danger
