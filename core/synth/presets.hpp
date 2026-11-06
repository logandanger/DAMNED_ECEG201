// Factory patches: overrides on top of the parameter defaults.
#pragma once
#include "core/synth/params.hpp"

namespace danger {

int preset_count();
const char* preset_name(int index);
void preset_load(int index, ParamStore& params);

}  // namespace danger
