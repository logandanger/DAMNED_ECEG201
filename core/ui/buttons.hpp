// A/B chord logic. Pure state machine with explicit time, so it runs in the unit tests.
//
//   A tap            (press and release alone, < kSelectHold)  -> Up
//   A hold           (alone, reaches kSelectHold)              -> Select, fires while held
//   B tap            (press and release alone)                 -> Down
//   A and B together (the second goes down before the first is released):
//     released before kClearLayerHold                          -> nothing
//     released between kClearLayerHold and kClearAllHold       -> ClearLayer, on the first release
//     held to kClearAllHold                                    -> ClearAll, fires while held
//
// Taps fire on release, so an A+B chord never produces Up, Down or Select, however much the two
// presses are skewed. After any action or chord, nothing fires until both buttons are up.
// No software debounce: the MC14490 debounces the on-board buttons in hardware.
#pragma once
#include <cstdint>

namespace danger {

enum class ButtonAction : uint8_t { None, Up, Down, Select, ClearLayer, ClearAll };

class ButtonLogic {
public:
    enum class Mode : uint8_t { Idle, AOnly, BOnly, Chord, Locked };

    ButtonAction press_a(uint32_t now_ms);
    ButtonAction press_b(uint32_t now_ms);
    ButtonAction release_a(uint32_t now_ms);
    ButtonAction release_b(uint32_t now_ms);
    ButtonAction tick(uint32_t now_ms);  // fires hold actions

    Mode mode() const { return mode_; }
    // While A is held alone: 0..1 toward Select. While chording: 0..1 toward ClearAll.
    float progress(uint32_t now_ms) const;

private:
    ButtonAction press(uint32_t now);
    ButtonAction release(bool is_a, uint32_t now);

    Mode mode_ = Mode::Idle;
    bool a_ = false, b_ = false;
    uint32_t since_ = 0;
};

}  // namespace danger
