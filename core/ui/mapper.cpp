#include "core/ui/mapper.hpp"

namespace danger {
namespace {

template <size_t N>
Action find(const Binding (&table)[N], Control c, Trigger t, Modifier m) {
    for (const Binding& b : table) {
        if (b.control == c && b.trigger == t && b.modifier == m) return b.action;
    }
    return Action::None;
}

Action lookup(Control c, Trigger t, Modifier m) {
    const Action a = find(kDefaultBindings, c, t, m);
    if (a != Action::None || !kUseProposedMapping) return a;
    return find(kProposedBindings, c, t, m);
}

}  // namespace

MappedAction ControlMapper::map(const hal::ControlEvent& e) {
    if (e.control == kModifierControl) {
        if (e.type == hal::EventType::Press) {
            modifier_held_ = true;
            chord_used_ = false;
            return {Action::None, 0};
        }
        if (e.type == hal::EventType::Release) {
            const bool tap = modifier_held_ && !chord_used_;
            modifier_held_ = false;
            return {tap ? lookup(e.control, Trigger::Tap, Modifier::None) : Action::None, 0};
        }
    }
    if (e.type == hal::EventType::Release) return {Action::None, 0};

    const Modifier m = modifier_held_ ? Modifier::Held : Modifier::None;
    if (modifier_held_) chord_used_ = true;  // any use, bound or not, cancels the tap
    const Trigger t = e.type == hal::EventType::Turn ? Trigger::Turn : Trigger::Press;
    return {lookup(e.control, t, m), e.delta};
}

}  // namespace danger
