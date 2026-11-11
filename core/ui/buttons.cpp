#include "core/ui/buttons.hpp"

#include "core/config/beat_config.hpp"

namespace danger {
namespace {

constexpr uint32_t ms(float s) { return static_cast<uint32_t>(s * 1000.0f + 0.5f); }
constexpr uint32_t kSelectMs = ms(cfg::kSelectHold);
constexpr uint32_t kClearLayerMs = ms(cfg::kClearLayerHold);
constexpr uint32_t kClearAllMs = ms(cfg::kClearAllHold);

}  // namespace

ButtonAction ButtonLogic::press(uint32_t now) {
    switch (mode_) {
        case Mode::Idle:
            mode_ = a_ ? Mode::AOnly : Mode::BOnly;
            since_ = now;
            break;
        case Mode::AOnly:
        case Mode::BOnly:
            if (a_ && b_) {  // the second button: a chord, so the pending tap is dropped
                mode_ = Mode::Chord;
                since_ = now;
            }
            break;
        default: break;  // Chord or Locked: ignore
    }
    return ButtonAction::None;
}

ButtonAction ButtonLogic::release(bool is_a, uint32_t now) {
    ButtonAction out = ButtonAction::None;
    switch (mode_) {
        case Mode::AOnly:
            if (is_a) out = ButtonAction::Up;
            break;
        case Mode::BOnly:
            if (!is_a) out = ButtonAction::Down;
            break;
        case Mode::Chord:
            if (now - since_ >= kClearLayerMs) out = ButtonAction::ClearLayer;
            break;
        default: break;
    }
    mode_ = (a_ || b_) ? Mode::Locked : Mode::Idle;
    return out;
}

ButtonAction ButtonLogic::press_a(uint32_t now) {
    if (a_) return ButtonAction::None;
    a_ = true;
    return press(now);
}

ButtonAction ButtonLogic::press_b(uint32_t now) {
    if (b_) return ButtonAction::None;
    b_ = true;
    return press(now);
}

ButtonAction ButtonLogic::release_a(uint32_t now) {
    if (!a_) return ButtonAction::None;
    a_ = false;
    return release(true, now);
}

ButtonAction ButtonLogic::release_b(uint32_t now) {
    if (!b_) return ButtonAction::None;
    b_ = false;
    return release(false, now);
}

ButtonAction ButtonLogic::tick(uint32_t now) {
    if (mode_ == Mode::AOnly && now - since_ >= kSelectMs) {
        mode_ = Mode::Locked;
        return ButtonAction::Select;
    }
    if (mode_ == Mode::Chord && now - since_ >= kClearAllMs) {
        mode_ = Mode::Locked;
        return ButtonAction::ClearAll;
    }
    return ButtonAction::None;
}

float ButtonLogic::progress(uint32_t now) const {
    const auto frac = [&](uint32_t total) {
        const float f = static_cast<float>(now - since_) / static_cast<float>(total);
        return f > 1 ? 1.0f : f;
    };
    if (mode_ == Mode::AOnly) return frac(kSelectMs);
    if (mode_ == Mode::Chord) return frac(kClearAllMs);
    return 0;
}

}  // namespace danger
