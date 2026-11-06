// LVGL view of the menu, scope and status. Runs on the UI task only.
#pragma once
#include "core/synth/engine.hpp"
#include "core/ui/menu.hpp"

namespace danger {

constexpr int kScreenWidth = 320, kScreenHeight = 240;

void view_init();
void view_update(const Menu& menu, const ParamStore& params, const Engine& engine, float cpu_load);

}  // namespace danger
