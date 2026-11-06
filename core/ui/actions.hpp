// Functions a control can be mapped to.
#pragma once
#include <cstdint>

namespace danger {

enum class Action : uint8_t {
    None,
    MenuUp, MenuDown,        // move the highlight
    Select,                  // enter a page, load a preset, cycle an enum
    Back,                    // leave a page
    GroupPrev, GroupNext,    // Oscillator, Filter, Envelope, Effects, Global
    AdjustCoarse, AdjustFine,
    ResetValue,
    OctaveUp, OctaveDown,
    // Used only by the proposed mapping in control_map.hpp.
    PresetPrev, PresetNext,
    WaveNext,
    AdjustVolume, AdjustCutoff,
    Panic,
};

struct MappedAction {
    Action action;
    int16_t arg;  // encoder detents for Adjust*
};

}  // namespace danger
