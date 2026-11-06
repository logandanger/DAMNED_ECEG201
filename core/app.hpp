// Application root. Owns the parameters, engine and menu and connects them to hal/.
//   on_control()  input task
//   ui_tick()     UI task
//   Engine        audio task, through Engine::audio_callback
#pragma once
#include <atomic>

#include "core/synth/engine.hpp"
#include "core/ui/mapper.hpp"
#include "core/ui/menu.hpp"
#include "core/util/spsc.hpp"

namespace danger {

class App {
public:
    App() : menu_(&params_) {}

    void init();         // engine, first preset, LVGL
    void start_audio();
    void on_control(const hal::ControlEvent& e);
    void ui_tick();

    static void control_sink(const hal::ControlEvent& e, void* user) { static_cast<App*>(user)->on_control(e); }

    Engine& engine() { return engine_; }
    ParamStore& params() { return params_; }
    const Menu& menu() const { return menu_; }

private:
    void on_key(const hal::ControlEvent& e);

    ParamStore params_;
    Engine engine_;
    Menu menu_;
    ControlMapper mapper_;
    Spsc<MappedAction, 64> actions_;
    uint8_t key_note_[hal::kKeyCount] = {};  // a release must match its press across octave shifts
    std::atomic<uint16_t> key_mask_{0};
    uint32_t last_view_ms_ = 0;
};

App& app();

}  // namespace danger
