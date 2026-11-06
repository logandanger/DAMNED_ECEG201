// THE control mapping. Every control-to-function assignment lives in this file.
// docs/controls.md explains each choice. Edit, rebuild, done; no other file holds a mapping.
#pragma once
#include <cstdint>

#include "core/ui/actions.hpp"
#include "hal/controls.hpp"

namespace danger {

using hal::Control;

// Set to true once docs/controls.md section 2 is approved.
constexpr bool kUseProposedMapping = false;

// ---- Keys -------------------------------------------------------------------------------------
constexpr uint8_t kBaseNote = 60;  // Key0 at octave 0 = C4
constexpr int8_t kKeySemitone[hal::kKeyCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// ---- Buttons and encoders ---------------------------------------------------------------------
// Press: fires on press.  Turn: fires on encoder rotation.
// Tap: fires on release of kModifierControl, only if no other control was used while it was held.
enum class Trigger : uint8_t { Press, Tap, Turn };
enum class Modifier : uint8_t { None, Held };

// While this control is held, only Modifier::Held bindings fire. Keys always play.
constexpr Control kModifierControl = Control::B;

struct Binding {
    Control control;
    Trigger trigger;
    Modifier modifier;
    Action action;
};

// Default mapping, active now.
constexpr Binding kDefaultBindings[] = {
    {Control::Up,    Trigger::Press, Modifier::None, Action::MenuUp},
    {Control::Down,  Trigger::Press, Modifier::None, Action::MenuDown},
    {Control::Left,  Trigger::Press, Modifier::None, Action::GroupPrev},
    {Control::Right, Trigger::Press, Modifier::None, Action::GroupNext},
    {Control::A,     Trigger::Press, Modifier::None, Action::Select},
    {Control::B,     Trigger::Tap,   Modifier::None, Action::Back},
    {Control::Up,    Trigger::Press, Modifier::Held, Action::OctaveUp},
    {Control::Down,  Trigger::Press, Modifier::Held, Action::OctaveDown},
    {Control::Enc1,  Trigger::Turn,  Modifier::None, Action::AdjustCoarse},
    {Control::Enc2,  Trigger::Turn,  Modifier::None, Action::AdjustFine},
    {Control::Enc1,  Trigger::Press, Modifier::None, Action::ResetValue},
    {Control::Enc2,  Trigger::Press, Modifier::None, Action::ResetValue},
};

// Proposed additions, inactive until kUseProposedMapping is true. See docs/controls.md.
constexpr Binding kProposedBindings[] = {
    {Control::Left,  Trigger::Press, Modifier::Held, Action::PresetPrev},
    {Control::Right, Trigger::Press, Modifier::Held, Action::PresetNext},
    {Control::A,     Trigger::Press, Modifier::Held, Action::WaveNext},
    {Control::Enc1,  Trigger::Turn,  Modifier::Held, Action::AdjustVolume},
    {Control::Enc2,  Trigger::Turn,  Modifier::Held, Action::AdjustCutoff},
    {Control::Enc1,  Trigger::Press, Modifier::Held, Action::Panic},
};

// ---- Computer keyboard (simulator) ------------------------------------------------------------
// `key` is an SDL scancode name. turn != 0 sends encoder detents instead of press/release.
struct PcKey {
    const char* key;
    Control control;
    int8_t turn;
};

constexpr PcKey kPcKeys[] = {
    // Piano row: white keys on A S D F G H J, black keys on W E T Y U.
    {"A", Control::Key0, 0},  {"W", Control::Key1, 0},  {"S", Control::Key2, 0},
    {"E", Control::Key3, 0},  {"D", Control::Key4, 0},  {"F", Control::Key5, 0},
    {"T", Control::Key6, 0},  {"G", Control::Key7, 0},  {"Y", Control::Key8, 0},
    {"H", Control::Key9, 0},  {"U", Control::Key10, 0}, {"J", Control::Key11, 0},
    // D-pad, A, B.
    {"Up", Control::Up, 0},       {"Down", Control::Down, 0},
    {"Left", Control::Left, 0},   {"Right", Control::Right, 0},
    {"X", Control::A, 0},         {"Return", Control::A, 0},
    {"Z", Control::B, 0},         {"Backspace", Control::B, 0},
    // Encoder 1: K L turn, ; push. Encoder 2: , . turn, / push.
    {"K", Control::Enc1, -1},     {"L", Control::Enc1, 1},  {";", Control::Enc1, 0},
    {",", Control::Enc2, -1},     {".", Control::Enc2, 1},  {"/", Control::Enc2, 0},
};

}  // namespace danger
