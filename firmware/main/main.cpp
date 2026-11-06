// Firmware entry. Task layout:
//   core 1  audio   prio MAX-2  I2S, fed by Engine (hal/esp32/hal_audio.cpp)
//   core 0  input   prio 10     buttons, I2C keys and encoders at 1 kHz (hal/esp32/hal_input.cpp)
//   core 0  ui      prio 5      LVGL, menu, LEDs
//   core 0  mirror  prio 2      framebuffer streaming (mirror_task.cpp), optional
#include "core/app.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/esp32/board.hpp"
#include "hal/hal.hpp"
#include "mirror_task.hpp"

namespace {

constexpr int kUiCore = 0;

void ui_task(void*) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // no drawing until the mirror can see every flush
    for (;;) {
        danger::app().ui_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace

extern "C" void app_main() {
    hal::board_init();
    danger::app().init();
    danger::app().start_audio();
    hal::input_start(danger::App::control_sink, &danger::app());
    // The UI stack is allocated before the mirror's buffers: if RAM is short, the mirror is what goes.
    TaskHandle_t ui = nullptr;
    xTaskCreatePinnedToCore(ui_task, "ui", 12288, nullptr, 5, &ui, kUiCore);
    mirror_start();
    xTaskNotifyGive(ui);
}
