// Translates button and encoder events to actions using core/config/control_map.hpp.
// Keys do not pass through here: they always play.
#pragma once
#include "core/config/control_map.hpp"

namespace danger {

class ControlMapper {
public:
    MappedAction map(const hal::ControlEvent& e);
    bool modifier_held() const { return modifier_held_; }

private:
    bool modifier_held_ = false;
    bool chord_used_ = false;
};

}  // namespace danger
