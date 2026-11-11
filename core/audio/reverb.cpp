#include "core/audio/reverb.hpp"

#include <cmath>

#include "core/config/beat_config.hpp"

namespace danger {
namespace {

constexpr float kDamp = 0.35f;
constexpr float kInputGain = 0.06f;

}  // namespace

void Reverb::set_amount(float amount) { target_ = amount < 0 ? 0 : (amount > 1 ? 1 : amount); }

void Reverb::process(const float* in, float* left, float* right, int n) {
    // One-pole glide per block: no zipper noise when the encoder turns.
    const float k = 1.0f - std::exp(-static_cast<float>(n) / (cfg::kReverbGlide * cfg::kSampleRate));
    amount_ += (target_ - amount_) * k;
    const float wet = amount_ * cfg::kReverbMaxWet;
    const float feedback = 0.74f + 0.22f * amount_;

    const auto allpass = [this](int i, float* line, float x) {
        int& pos = ap_pos_[i];
        const float d = line[pos];
        const float y = d - 0.5f * x;
        line[pos] = x + 0.5f * d;
        if (++pos == kApLen[i]) pos = 0;
        return y;
    };
    float* ap0 = ap_;
    float* ap1 = ap0 + kApLen[0];
    float* ap2 = ap1 + kApLen[1];
    float* ap3 = ap2 + kApLen[2];

    for (int i = 0; i < n; ++i) {
        const float x = in[i] * kInputGain;
        float sum = 0;
        float* line = comb_;
        for (int c = 0; c < 4; ++c) {
            int& pos = comb_pos_[c];
            const float d = line[pos];
            comb_lp_[c] += (d - comb_lp_[c]) * (1.0f - kDamp) + 1e-20f;  // offset keeps denormals away
            line[pos] = x + comb_lp_[c] * feedback;
            if (++pos == kCombLen[c]) pos = 0;
            sum += d;
            line += kCombLen[c];
        }
        const float diffused = allpass(1, ap1, allpass(0, ap0, sum));
        left[i] += allpass(2, ap2, diffused) * wet;
        right[i] += allpass(3, ap3, diffused) * wet;
    }
}

}  // namespace danger
