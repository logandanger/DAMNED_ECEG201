#include "core/synth/dsp.hpp"

#include <cmath>

namespace danger {
namespace {

constexpr int kSineSize = 1024;
constexpr float kPi = 3.14159265358979f;
float g_sine[kSineSize + 1];

// Two-sample polynomial band-limited step correction.
inline float poly_blep(float t, float dt, float inv_dt) {
    if (t < dt) {
        t *= inv_dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * inv_dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }

}  // namespace

void dsp_init() {
    for (int i = 0; i <= kSineSize; ++i) g_sine[i] = std::sin(2.0f * kPi * static_cast<float>(i) / kSineSize);
}

float sine_lookup(float phase) {
    const float x = phase * kSineSize;
    const int i = static_cast<int>(x);
    const float f = x - static_cast<float>(i);
    return g_sine[i] + (g_sine[i + 1] - g_sine[i]) * f;
}

EnvCoefs env_coefs(float attack_ms, float decay_ms, float sustain, float release_ms, float steps_per_second) {
    const auto coef = [&](float ms) {
        return std::exp(-6.9078f / (ms * 0.001f * steps_per_second));  // ln(1000)
    };
    EnvCoefs c;
    c.attack_inc = 1.0f / (attack_ms * 0.001f * steps_per_second);
    c.decay_coef = coef(decay_ms);
    c.sustain = sustain;
    c.release_coef = coef(release_ms);
    return c;
}

void Oscillator::fill(float* out, int n, Wave wave, float dt, float pw) {
    const float inv_dt = 1.0f / dt;
    float p = phase_;
    switch (wave) {
        case Wave::Sine:
            for (int i = 0; i < n; ++i) {
                out[i] = sine_lookup(p);
                p = wrap1(p + dt);
            }
            break;
        case Wave::Saw:
            for (int i = 0; i < n; ++i) {
                out[i] = 2.0f * p - 1.0f - poly_blep(p, dt, inv_dt);
                p = wrap1(p + dt);
            }
            break;
        case Wave::Square:
            for (int i = 0; i < n; ++i) {
                const float q = wrap1(p + 1.0f - pw);
                out[i] = (p < pw ? 1.0f : -1.0f) + poly_blep(p, dt, inv_dt) - poly_blep(q, dt, inv_dt);
                p = wrap1(p + dt);
            }
            break;
        case Wave::Triangle:  // naive: harmonics fall at 12 dB/oct, so aliasing stays low
            for (int i = 0; i < n; ++i) {
                out[i] = 1.0f - 4.0f * std::fabs(p - 0.5f);
                p = wrap1(p + dt);
            }
            break;
        case Wave::White:
            for (int i = 0; i < n; ++i) out[i] = white();
            break;
        case Wave::Pink:  // Paul Kellet's economy filter
            for (int i = 0; i < n; ++i) {
                const float w = white();
                b0_ = 0.99765f * b0_ + w * 0.0990460f;
                b1_ = 0.96300f * b1_ + w * 0.2965164f;
                b2_ = 0.57000f * b2_ + w * 1.0526913f;
                out[i] = (b0_ + b1_ + b2_ + w * 0.1848f) * 0.25f;
            }
            break;
    }
    phase_ = p;
}

void Svf::set(float cutoff_hz, float resonance) {
    const float fc = clampf(cutoff_hz, 10.0f, 0.45f * kSampleRate);
    const float g = std::tan(kPi * fc * kInvSampleRate);
    k_ = 2.0f - 1.96f * resonance;  // Q = 1/k, from 0.5 to 25
    a1_ = 1.0f / (1.0f + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
}

void Drive::configure(DriveMode mode, float amount, int bits, int rate_div) {
    mode_ = mode;
    pre_ = 1.0f + amount * amount * 24.0f;
    post_ = 1.0f / (1.0f + amount * 0.6f);
    levels_ = static_cast<float>(1 << (bits - 1));
    inv_levels_ = 1.0f / levels_;
    rate_ = rate_div;
}

void Drive::process(float* x, int n) {
    if (mode_ == DriveMode::Drive) {
        for (int i = 0; i < n; ++i) x[i] = soft_clip(x[i] * pre_) * post_;
    } else if (mode_ == DriveMode::Crush) {
        for (int i = 0; i < n; ++i) {
            if (--count_ <= 0) {
                count_ = rate_;
                const float s = clampf(x[i], -1.0f, 1.0f) * levels_;
                held_ = static_cast<float>(static_cast<int>(s + (s < 0 ? -0.5f : 0.5f))) * inv_levels_;
            }
            x[i] = held_;
        }
    }
}

void ModFx::configure(ModMode mode, float rate_hz, float depth, float mix) {
    mode_ = mode;
    lfo_inc_ = rate_hz * kInvSampleRate;
    depth_ = depth;
    wet_ = mix;
    dry_ = 1.0f - 0.5f * mix;
}

void ModFx::process(const float* in, float* left, float* right, int n) {
    switch (mode_) {
        case ModMode::Off:
            std::memcpy(left, in, sizeof(float) * static_cast<size_t>(n));
            std::memcpy(right, in, sizeof(float) * static_cast<size_t>(n));
            break;
        case ModMode::Chorus: chorus(in, left, right, n); break;
        case ModMode::Phaser: phaser(in, left, right, n); break;
    }
}

void ModFx::chorus(const float* in, float* left, float* right, int n) {
    constexpr float kCenter = 0.011f * kSampleRate;
    const float swing = 0.008f * kSampleRate * depth_;
    const auto tap = [this](float delay) {
        const float pos = static_cast<float>(write_) - delay + kLine;
        const int i = static_cast<int>(pos);
        const float f = pos - static_cast<float>(i);
        const float a = line_[i & (kLine - 1)];
        return a + (line_[(i + 1) & (kLine - 1)] - a) * f;
    };
    for (int i = 0; i < n; ++i) {
        line_[write_] = in[i];
        lfo_ = wrap1(lfo_ + lfo_inc_);
        const float tri_l = std::fabs(2.0f * lfo_ - 1.0f);
        const float tri_r = std::fabs(2.0f * wrap1(lfo_ + 0.25f) - 1.0f);
        left[i] = in[i] * dry_ + tap(kCenter + swing * tri_l) * wet_;
        right[i] = in[i] * dry_ + tap(kCenter + swing * tri_r) * wet_;
        write_ = (write_ + 1) & (kLine - 1);
    }
}

void ModFx::phaser(const float* in, float* left, float* right, int n) {
    lfo_ = wrap1(lfo_ + lfo_inc_ * static_cast<float>(n));
    const float coef[2] = {0.15f + 0.75f * depth_ * std::fabs(2.0f * lfo_ - 1.0f),
                           0.15f + 0.75f * depth_ * std::fabs(2.0f * wrap1(lfo_ + 0.25f) - 1.0f)};
    float* const out[2] = {left, right};
    for (int ch = 0; ch < 2; ++ch) {
        const float a = coef[ch];
        float* z = ap_[ch];
        float fb = fb_[ch];
        for (int i = 0; i < n; ++i) {
            float s = in[i] + fb * 0.5f;
            for (int st = 0; st < 4; ++st) {  // first-order allpass
                const float y = z[st] - a * s;
                z[st] = s + a * y;
                s = y;
            }
            fb = s;
            out[ch][i] = in[i] * dry_ + s * wet_;
        }
        fb_[ch] = fb + 1e-20f;
    }
}

void Delay::configure(float time_ms, float feedback, float mix) {
    target_ = time_ms * 0.001f * kSampleRate;
    feedback_ = feedback;
    mix_ = mix;
}

void Delay::process(float* left, float* right, int n) {
    if (mix_ < 0.001f) {
        active_ = false;
        return;
    }
    if (!active_) {  // no stale tail when the effect comes back
        std::memset(line_, 0, sizeof(line_));
        time_ = target_;
        active_ = true;
    }
    for (int i = 0; i < n; ++i) {
        time_ += (target_ - time_) * 0.0005f;  // tape-style glide: no click on time changes
        float pos = static_cast<float>(write_) - time_;
        if (pos < 0) pos += kLine;
        const int a = static_cast<int>(pos);
        const int b = a + 1 == kLine ? 0 : a + 1;
        const float f = pos - static_cast<float>(a);
        const float s0 = static_cast<float>(line_[a]);
        const float wet = (s0 + (static_cast<float>(line_[b]) - s0) * f) * (1.0f / 32768.0f);
        const float in = 0.5f * (left[i] + right[i]);
        const float rec = clampf(in + wet * feedback_, -1.0f, 1.0f);
        line_[write_] = static_cast<int16_t>(rec * 32767.0f);
        if (++write_ == kLine) write_ = 0;
        left[i] += wet * mix_;
        right[i] += wet * mix_;
    }
}

void Reverb::configure(float size, float damp, float mix) {
    feedback_ = 0.72f + 0.25f * size;
    damp_ = 0.05f + 0.6f * damp;
    mix_ = mix;
}

void Reverb::process(float* left, float* right, int n) {
    if (mix_ < 0.001f) {
        active_ = false;
        return;
    }
    if (!active_) {
        std::memset(comb_, 0, sizeof(comb_));
        std::memset(ap_, 0, sizeof(ap_));
        std::memset(comb_lp_, 0, sizeof(comb_lp_));
        active_ = true;
    }
    const auto allpass = [this](int k, float* line, float x) {
        int& pos = ap_pos_[k];
        const float d = line[pos];
        const float y = d - 0.5f * x;
        line[pos] = x + 0.5f * d;
        if (++pos == kApLen[k]) pos = 0;
        return y;
    };
    float* ap0 = ap_;
    float* ap1 = ap0 + kApLen[0];
    float* ap2 = ap1 + kApLen[1];
    float* ap3 = ap2 + kApLen[2];

    for (int i = 0; i < n; ++i) {
        const float in = (left[i] + right[i]) * 0.06f;
        float sum = 0;
        float* line = comb_;
        for (int c = 0; c < 4; ++c) {
            int& pos = comb_pos_[c];
            const float d = line[pos];
            comb_lp_[c] += (d - comb_lp_[c]) * (1.0f - damp_) + 1e-20f;
            line[pos] = in + comb_lp_[c] * feedback_;
            if (++pos == kCombLen[c]) pos = 0;
            sum += d;
            line += kCombLen[c];
        }
        const float diffused = allpass(1, ap1, allpass(0, ap0, sum));
        left[i] += allpass(2, ap2, diffused) * mix_;
        right[i] += allpass(3, ap3, diffused) * mix_;
    }
}

}  // namespace danger
