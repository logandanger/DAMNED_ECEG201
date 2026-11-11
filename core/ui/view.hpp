// LVGL screen. Runs on the UI task only.
#pragma once
#include "core/beat/engine.hpp"
#include "core/ui/buttons.hpp"

namespace danger {

constexpr int kScreenWidth = 320, kScreenHeight = 240;

struct ViewModel {
    EngineStatus s;
    int browse;               // instrument under the A/B cursor
    ButtonLogic::Mode mode;   // A/B state
    float hold;               // 0..1 progress of the A/B hold
    float cpu;                // audio load 0..1
};

void view_init();
void view_update(const ViewModel& m);

}  // namespace danger
