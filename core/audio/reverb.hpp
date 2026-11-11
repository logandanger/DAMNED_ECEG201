// Cheap Schroeder-style reverb: 4 damped combs, 2 series allpasses, one allpass per output. 26 kB.
// About 100 operations per sample; the same code runs on the ESP32-S3 and the desktop.
#pragma once

namespace danger {

class Reverb {
public:
    void set_amount(float amount);  // 0..1; glides with cfg::kReverbGlide
    // Adds the wet signal of `in` into left and right.
    void process(const float* in, float* left, float* right, int n);

private:
    static constexpr int kCombLen[4] = {1116, 1188, 1277, 1356};
    static constexpr int kApLen[4] = {225, 556, 341, 441};
    float comb_[1116 + 1188 + 1277 + 1356] = {};
    float ap_[225 + 556 + 341 + 441] = {};
    int comb_pos_[4] = {}, ap_pos_[4] = {};
    float comb_lp_[4] = {};
    float target_ = 0, amount_ = 0;
};

}  // namespace danger
