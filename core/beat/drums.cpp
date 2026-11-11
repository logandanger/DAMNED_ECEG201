#include "core/beat/drums.hpp"

#include <cstring>

namespace danger {
namespace {

constexpr int kKick = static_cast<int>(Drum::Kick), kSnare = static_cast<int>(Drum::Snare),
              kHat = static_cast<int>(Drum::Hat), kCrash = static_cast<int>(Drum::Crash);
constexpr int kBeat = cfg::kStepsPerBeat;
constexpr int kBar = cfg::kStepsPerBar;

uint8_t vel(float v) { return static_cast<uint8_t>(v < 1 ? 1 : (v > 127 ? 127 : v)); }

}  // namespace

void DrumGen::reset(uint32_t seed) {
    rng_.seed(seed);
    drift_ = 0;
    fill_ = false;
    std::memset(hits_, 0, sizeof(hits_));
}

void DrumGen::new_bar(int bar, const float* accent, float activity) {
    std::memset(hits_, 0, sizeof(hits_));

    // Density: from how busy the seeds are, plus a slow bounded random walk.
    drift_ += (rng_.uniform() * 2 - 1) * cfg::kDrumDriftPerBar;
    if (drift_ > cfg::kDrumDensityDrift) drift_ = cfg::kDrumDensityDrift;
    if (drift_ < -cfg::kDrumDensityDrift) drift_ = -cfg::kDrumDensityDrift;
    float d = cfg::kDrumDensityMin + activity * cfg::kDrumDensityFromSeed + drift_;
    density_ = d < cfg::kDrumDensityMin ? cfg::kDrumDensityMin : (d > cfg::kDrumDensityMax ? cfg::kDrumDensityMax : d);

    const bool last_bar = bar == cfg::kLoopBars - 1;
    if (bar == 0) fill_ = rng_.chance(cfg::kDrumFillChance * (0.5f + density_));

    // Kick: beat 1, beat 3 unless sparse, and wherever the seeds play strongly.
    hits_[0][kKick] = 110;
    if (density_ > 0.3f) hits_[2 * kBeat][kKick] = 96;
    int extra = 0;
    const int max_extra = 1 + static_cast<int>(density_ * 3);
    for (int s = 1; s < kBar && extra < max_extra; ++s) {
        if (s % (2 * kBeat) == kBeat) continue;           // beats 2 and 4 belong to the snare
        if (s % 2 && density_ < 0.7f) continue;            // 16ths only when busy
        if (accent[s] > cfg::kDrumAccentKick) {
            hits_[s][kKick] = vel(70 + 40 * accent[s]);
            ++extra;
        }
    }

    // Snare: backbeat on 2 and 4, ghost notes when busy.
    hits_[kBeat][kSnare] = 104;
    hits_[3 * kBeat][kSnare] = 108;
    if (density_ > 0.6f) {
        for (int s = 1; s < kBar; s += 2) {
            if (rng_.chance(0.25f * (density_ - 0.5f))) hits_[s][kSnare] = vel(28 + rng_.below(14));
        }
    }

    // Hats: quarters when sparse, 8ths, or 16ths when busy. Accents on the beat.
    const int hat_step = density_ < 0.3f ? kBeat : (density_ > cfg::kDrumHat16th ? 1 : 2);
    for (int s = 0; s < kBar; s += hat_step) {
        const float v = s % kBeat == 0 ? 84 : (s % 2 == 0 ? 62 : 42);
        hits_[s][kHat] = vel(v + static_cast<float>(rng_.below(13) - 6));
    }

    // Crash at the phrase boundary; an open cymbal on the last 8th of some bars.
    if (bar == 0) {
        hits_[0][kCrash] = 96;
        hits_[0][kHat] = 0;
    } else if (rng_.chance(0.35f * density_)) {
        hits_[kBar - 2][kCrash] = 52;
        hits_[kBar - 2][kHat] = 0;
    }

    // Fill: the last beat of the phrase, rising 16th snares.
    if (last_bar && fill_) {
        for (int i = 0; i < kBeat; ++i) {
            const int s = 3 * kBeat + i;
            hits_[s][kSnare] = vel(58 + 16 * i);
            hits_[s][kHat] = 0;
            hits_[s][kKick] = 0;
        }
        hits_[3 * kBeat][kKick] = 92;
    }
}

}  // namespace danger
