// xorshift32. Deterministic, allocation-free, cheap enough for the audio task.
#pragma once
#include <cstdint>

namespace danger {

struct Rng {
    uint32_t s = 0x2545F491u;

    void seed(uint32_t v) { s = v ? v : 0x2545F491u; }
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }  // [0, 1)
    int below(int n) { return n > 0 ? static_cast<int>(next() % static_cast<uint32_t>(n)) : 0; }
    bool chance(float p) { return uniform() < p; }
};

}  // namespace danger
