// Generative drums. One bar is generated at each bar start from the seeds' rhythm. Audio task only.
#pragma once
#include <cstdint>

#include "core/beat/rng.hpp"
#include "core/config/beat_config.hpp"
#include "core/samples/samples.hpp"

namespace danger {

class DrumGen {
public:
    void reset(uint32_t seed);
    // bar: 0..kLoopBars-1 within the 4-bar phrase.
    // accent: kStepsPerBar values 0..1, how strongly the seeds play on each step of this bar.
    // activity: seed onsets per step over the whole loop, 0..1.
    void new_bar(int bar, const float* accent, float activity);
    uint8_t hit(int step, Drum d) const { return hits_[step][static_cast<int>(d)]; }
    float density() const { return density_; }

private:
    uint8_t hits_[cfg::kStepsPerBar][kDrumCount] = {};
    float drift_ = 0, density_ = 0;
    bool fill_ = false;
    Rng rng_;
};

}  // namespace danger
