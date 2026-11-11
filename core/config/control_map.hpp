// THE control mapping. Every control-to-function assignment lives in this file.
// docs/controls.md explains each choice. Timing constants are in core/config/beat_config.hpp.
#pragma once
#include <cstdint>

#include "hal/controls.hpp"

namespace danger {

using hal::Control;

// ---- Keys -------------------------------------------------------------------------------------
// Piano layout: Key0 = C ... Key11 = B. The first four black keys (top left) are drum pads.
constexpr uint8_t kBaseNote = 60;  // Key0 = C4, shifted to each instrument's register
constexpr int8_t kKeySemitone[hal::kKeyCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
// Drum index (core/samples/samples.hpp Drum) per key, or -1 for a melodic key.
//                                       C   C#  D   D#  E   F   F#  G   G#  A   A#  B
constexpr int8_t kKeyDrum[hal::kKeyCount] = {-1, 0, -1, 1, -1, -1, 2, -1, 3, -1, -1, -1};
//                                           kick   snare      hat      crash

// ---- Buttons and encoders ---------------------------------------------------------------------
constexpr Control kStepUp = Control::A;      // tap: next instrument; hold: select it
constexpr Control kStepDown = Control::B;    // tap: previous instrument
constexpr Control kReverbEncoder = Control::Enc1;  // left
constexpr Control kBpmEncoder = Control::Enc2;     // right
// Up, Down, Left, Right and the encoder pushes are unassigned.

// ---- Computer keyboard (simulator) ------------------------------------------------------------
// `key` is an SDL scancode name. turn != 0 sends encoder detents instead of press/release.
struct PcKey {
    const char* key;
    Control control;
    int8_t turn;
};

constexpr PcKey kPcKeys[] = {
    // Piano row: white keys on A S D F G H J, black keys on W E T Y U (W E T Y = drum pads).
    {"A", Control::Key0, 0},  {"W", Control::Key1, 0},  {"S", Control::Key2, 0},
    {"E", Control::Key3, 0},  {"D", Control::Key4, 0},  {"F", Control::Key5, 0},
    {"T", Control::Key6, 0},  {"G", Control::Key7, 0},  {"Y", Control::Key8, 0},
    {"H", Control::Key9, 0},  {"U", Control::Key10, 0}, {"J", Control::Key11, 0},
    // D-pad, A, B.
    {"Up", Control::Up, 0},       {"Down", Control::Down, 0},
    {"Left", Control::Left, 0},   {"Right", Control::Right, 0},
    {"X", Control::A, 0},         {"Return", Control::A, 0},
    {"Z", Control::B, 0},         {"Backspace", Control::B, 0},
    // Left encoder (reverb): K L turn, ; push. Right encoder (BPM): , . turn, / push.
    {"K", Control::Enc1, -1},     {"L", Control::Enc1, 1},  {";", Control::Enc1, 0},
    {",", Control::Enc2, -1},     {".", Control::Enc2, 1},  {"/", Control::Enc2, 0},
};

}  // namespace danger
