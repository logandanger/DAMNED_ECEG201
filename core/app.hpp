// Application root. Connects hal/ input, the beat engine and the view.
//   on_control()  input task: keys go straight to the audio task, the rest to the UI task
//   ui_tick()     UI task: A/B logic, encoders, view, LEDs
//   BeatEngine    audio task, through BeatEngine::audio_callback
#pragma once
#include <atomic>

#include "core/beat/engine.hpp"
#include "core/ui/buttons.hpp"
#include "core/util/spsc.hpp"

namespace danger {

class App {
public:
    void init();  // engine, LVGL
    void start_audio();
    void on_control(const hal::ControlEvent& e);
    void ui_tick();

    static void control_sink(const hal::ControlEvent& e, void* user) { static_cast<App*>(user)->on_control(e); }

    BeatEngine& engine() { return engine_; }
    int browse() const { return browse_; }
    const ButtonLogic& buttons() const { return buttons_; }

private:
    void handle(const hal::ControlEvent& e, uint32_t now);
    void act(ButtonAction a);

    BeatEngine engine_;
    ButtonLogic buttons_;
    Spsc<hal::ControlEvent, 64> events_;
    std::atomic<uint16_t> key_mask_{0};
    int browse_ = 0;  // instrument under the cursor; becomes current on Select
    uint32_t last_view_ms_ = 0;
};

App& app();

}  // namespace danger
