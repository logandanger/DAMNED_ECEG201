#include "core/app.hpp"

#include "core/ui/led_view.hpp"
#include "core/ui/view.hpp"
#include "hal/hal.hpp"
#include "lvgl.h"

namespace danger {
namespace {

constexpr uint32_t kViewPeriodMs = 33;

}  // namespace

App& app() {
    static App instance;
    return instance;
}

void App::init() {
    engine_.init(&params_);
    menu_.load_preset(0);
    view_init();
}

void App::start_audio() { hal::audio_start(Engine::audio_callback, &engine_); }

void App::on_key(const hal::ControlEvent& e) {
    const int i = hal::key_index(e.control);
    if (e.type == hal::EventType::Press) {
        const int note = kBaseNote + params_.get_int(ParamId::GlbOctave) * 12 + kKeySemitone[i];
        key_note_[i] = static_cast<uint8_t>(note);
        key_mask_.fetch_or(static_cast<uint16_t>(1u << i), std::memory_order_relaxed);
        engine_.post_note({key_note_[i], e.velocity});
    } else if (e.type == hal::EventType::Release) {
        key_mask_.fetch_and(static_cast<uint16_t>(~(1u << i)), std::memory_order_relaxed);
        engine_.post_note({key_note_[i], 0});
    }
}

void App::on_control(const hal::ControlEvent& e) {
    if (hal::is_key(e.control)) {
        on_key(e);
        return;
    }
    const MappedAction a = mapper_.map(e);
    if (a.action != Action::None) actions_.push(a);
}

void App::ui_tick() {
    MappedAction a;
    while (actions_.pop(a)) {
        if (a.action == Action::Panic) engine_.panic();
        else menu_.handle(a);
    }
    const uint32_t now = hal::millis();
    if (now - last_view_ms_ >= kViewPeriodMs) {
        last_view_ms_ = now;
        view_update(menu_, params_, engine_, hal::audio_load());
        uint8_t rgb[kLedCount * 3];
        led_view_render(rgb, engine_.peak(), engine_.voice_mask(), key_mask_.load(std::memory_order_relaxed),
                        params_.get(ParamId::GlbLed));
        hal::leds_write(rgb, kLedCount);
    }
    lv_timer_handler();
}

}  // namespace danger
