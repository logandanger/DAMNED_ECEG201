#include "core/app.hpp"

#include "core/config/control_map.hpp"
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
    engine_.init();
    view_init();
}

void App::start_audio() { hal::audio_start(BeatEngine::audio_callback, &engine_); }

void App::on_control(const hal::ControlEvent& e) {
    if (hal::is_key(e.control)) {
        const int i = hal::key_index(e.control);
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if (e.type == hal::EventType::Press) {
            key_mask_.fetch_or(bit, std::memory_order_relaxed);
            engine_.post_key({static_cast<uint8_t>(i), e.velocity ? e.velocity : hal::kFixedKeyVelocity});
        } else if (e.type == hal::EventType::Release) {
            key_mask_.fetch_and(static_cast<uint16_t>(~bit), std::memory_order_relaxed);
            engine_.post_key({static_cast<uint8_t>(i), 0});
        }
        return;
    }
    events_.push(e);
}

void App::act(ButtonAction a) {
    switch (a) {
        case ButtonAction::Up: browse_ = (browse_ + 1) % kInstrumentCount; break;
        case ButtonAction::Down: browse_ = (browse_ + kInstrumentCount - 1) % kInstrumentCount; break;
        case ButtonAction::Select:
            engine_.post_command({CommandType::SelectInstrument, static_cast<uint8_t>(browse_)});
            break;
        case ButtonAction::ClearLayer: engine_.post_command({CommandType::ClearLayer, 0}); break;
        case ButtonAction::ClearAll: engine_.post_command({CommandType::ClearAll, 0}); break;
        case ButtonAction::None: break;
    }
}

void App::handle(const hal::ControlEvent& e, uint32_t now) {
    const bool press = e.type == hal::EventType::Press, release = e.type == hal::EventType::Release;
    if (e.control == kStepUp) {
        if (press) act(buttons_.press_a(now));
        if (release) act(buttons_.release_a(now));
    } else if (e.control == kStepDown) {
        if (press) act(buttons_.press_b(now));
        if (release) act(buttons_.release_b(now));
    } else if (e.type == hal::EventType::Turn) {
        if (e.control == kReverbEncoder)
            engine_.set_reverb(engine_.reverb_target() + static_cast<float>(e.delta) * cfg::kReverbPerDetent);
        if (e.control == kBpmEncoder)
            engine_.set_bpm(engine_.bpm_target() + static_cast<float>(e.delta) * cfg::kBpmPerDetent);
    }
}

void App::ui_tick() {
    const uint32_t now = hal::millis();
    hal::ControlEvent e;
    while (events_.pop(e)) handle(e, now);
    act(buttons_.tick(now));

    if (now - last_view_ms_ >= kViewPeriodMs) {
        last_view_ms_ = now;
        const EngineStatus s = engine_.status();
        ViewModel m{s, browse_, buttons_.mode(), buttons_.progress(now), hal::audio_load()};
        view_update(m);
        uint8_t layers = 0;
        for (int i = 0; i < s.layer_count; ++i) {
            if (s.layers[s.order[i]].state == LayerState::Active) layers = static_cast<uint8_t>(layers << 1 | 1);
        }
        uint8_t rgb[kLedCount * 3];
        led_view_render(rgb, s.peak, layers, key_mask_.load(std::memory_order_relaxed));
        hal::leds_write(rgb, kLedCount);
    }
    lv_timer_handler();
}

}  // namespace danger
