// Whole-app tests through the real input path and the real LVGL view, with a stub HAL.
#include <cstdio>
#include <cstring>

#include "core/app.hpp"
#include "core/config/beat_config.hpp"
#include "core/ui/view.hpp"
#include "hal/hal.hpp"

using namespace danger;
using hal::Control;
using hal::EventType;

namespace {

uint32_t g_now = 0;
uint16_t g_fb[320 * 240];
uint32_t g_flushed_pixels = 0;
int g_led_calls = 0;

int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

void press(Control c) { app().on_control({c, EventType::Press, 0, hal::is_key(c) ? hal::kFixedKeyVelocity : uint8_t(0)}); }
void release(Control c) { app().on_control({c, EventType::Release, 0, 0}); }
void turn(Control c, int d) { app().on_control({c, EventType::Turn, int16_t(d), 0}); }

// Advances UI time and audio together, as both run concurrently on the device.
void tick(int ms = 40) {
    static int16_t buf[cfg::kBlockFrames * 2];
    static double audio_ms = 0;
    for (int i = 0; i < ms; i += 5) {
        g_now += 5;
        app().ui_tick();
        while (audio_ms < g_now) {
            app().engine().render(buf, cfg::kBlockFrames);
            audio_ms += 1000.0 * cfg::kBlockFrames / cfg::kSampleRate;
        }
    }
}

int count_pixels(uint16_t color) {
    int n = 0;
    for (uint16_t p : g_fb) n += p == color;
    return n;
}

constexpr uint16_t kRed565 = ((0xE3 >> 3) << 11) | ((0x00 >> 2) << 5) | (0x0F >> 3);

}  // namespace

namespace hal {
void audio_start(AudioCallback, void*) {}
float audio_load() { return 0.1f; }
void display_flush(int x, int y, int w, int h, uint16_t* px) {
    for (int r = 0; r < h; ++r) std::memcpy(&g_fb[(y + r) * 320 + x], px + r * w, size_t(w) * 2);
    g_flushed_pixels += uint32_t(w * h);
}
void leds_write(const uint8_t*, int count) { CHECK(count == 32); ++g_led_calls; }
void input_start(ControlSink, void*) {}
uint32_t millis() { return g_now; }
}  // namespace hal

int main() {
    app().init();
    tick(100);
    CHECK(g_flushed_pixels >= 320 * 240);  // first frame covers the screen
    CHECK(g_led_calls > 0);
    int black = count_pixels(0), white = count_pixels(0xFFFF);
    std::printf("black %d, white %d\n", black, white);
    CHECK(black > 1000 && white > 30000);

    // A tap: cursor moves, nothing selected yet; the name turns red (browsing).
    const int before_red = count_pixels(kRed565);
    press(Control::A);
    tick(100);
    release(Control::A);
    tick(100);
    CHECK(app().browse() == 1);
    CHECK(app().engine().status().instrument == 0);
    CHECK(count_pixels(kRed565) > before_red + 50);

    // B tap: back.
    press(Control::B);
    tick(100);
    release(Control::B);
    tick(100);
    CHECK(app().browse() == 0);

    // A tap to 1, then hold A to select it.
    press(Control::A);
    release(Control::A);
    tick(50);
    press(Control::A);
    tick(int(cfg::kSelectHold * 1000) + 50);
    release(Control::A);
    tick(50);
    CHECK(app().engine().status().instrument == 1);

    // A+B for 0.5 s: no step, no select, no clear.
    press(Control::A);
    tick(60);
    press(Control::B);
    tick(440);
    release(Control::A);
    release(Control::B);
    tick(100);
    CHECK(app().browse() == 1);
    CHECK(app().engine().status().instrument == 1);

    // Play and commit a seed through the key path; the layer shows up.
    press(Control::Key0);
    tick(100);
    release(Control::Key0);
    tick(200);
    press(Control::Key4);
    tick(100);
    release(Control::Key4);
    tick(100);
    CHECK(app().engine().status().seed_notes == 2);
    tick(int(cfg::kSeedIdleCommit * 1000) + 200);
    EngineStatus s = app().engine().status();
    CHECK(s.layer_count == 1 && s.layers[s.order[0]].state == LayerState::Active);
    CHECK(s.drums > 0);

    // Chord progress shows on screen while held; 1.5 s then release clears the layer.
    press(Control::B);
    press(Control::A);
    tick(800);
    const int red_mid_hold = count_pixels(kRed565);
    tick(700);
    CHECK(count_pixels(kRed565) > red_mid_hold);  // progress bar turns signal red past 1 s
    release(Control::A);
    release(Control::B);
    tick(100);
    s = app().engine().status();
    CHECK(s.layers[s.order[0]].state == LayerState::FadingOut);
    tick(int(cfg::kClearLayerFade * 1000) + 200);
    CHECK(app().engine().status().layer_count == 0);

    // Encoders: left = reverb, right = BPM.
    const float rev = app().engine().reverb_target(), bpm = app().engine().bpm_target();
    turn(Control::Enc1, 5);
    turn(Control::Enc2, -10);
    tick();
    CHECK(app().engine().reverb_target() > rev);
    CHECK(app().engine().bpm_target() == bpm - 10 * cfg::kBpmPerDetent);
    turn(Control::Enc2, 1000);
    tick();
    CHECK(app().engine().bpm_target() == cfg::kBpmMax);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
