// Physical control identifiers and events. Shared by hal/ and core/.
#pragma once
#include <cstdint>

namespace hal {

enum class Control : uint8_t {
    Key0, Key1, Key2, Key3, Key4, Key5, Key6, Key7, Key8, Key9, Key10, Key11,
    Up, Down, Left, Right, A, B,
    Enc1, Enc2,
    Count
};

constexpr int kKeyCount = 12;
constexpr int kControlCount = static_cast<int>(Control::Count);
constexpr uint8_t kFixedKeyVelocity = 100;  // prototype #1 keys are on/off

enum class EventType : uint8_t { Press, Release, Turn };

struct ControlEvent {
    Control control;
    EventType type;
    int16_t delta;     // Turn only: detents, positive = clockwise
    uint8_t velocity;  // keys only, 1..127
};

constexpr bool is_key(Control c) { return c <= Control::Key11; }
constexpr int key_index(Control c) { return static_cast<int>(c); }
constexpr Control key_control(int i) { return static_cast<Control>(i); }

}  // namespace hal
