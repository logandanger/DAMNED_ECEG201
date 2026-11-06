// Whole-app tests through the real input path and the real LVGL view, with a stub HAL.
#include <cstdio>
#include <cstring>

#include "core/app.hpp"
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

void tick(int ms = 40) {
    for (int i = 0; i < ms; i += 5) {
        g_now += 5;
        app().ui_tick();
    }
}

void render(int blocks = 4) {
    static int16_t buf[kBlockFrames * 2];
    for (int i = 0; i < blocks; ++i) app().engine().render(buf, kBlockFrames);
}

bool playing(int note) {
    for (int i = 0; i < kVoices; ++i) {
        const Voice& v = app().engine().voice(i);
        if (v.active() && !v.releasing() && v.note() == note) return true;
    }
    return false;
}

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

    // The screen has ink and paper.
    int black = 0, white = 0;
    for (uint16_t p : g_fb) { black += p == 0; white += p == 0xFFFF; }
    std::printf("black %d, white %d\n", black, white);
    CHECK(black > 1000 && white > 10000);

    // Key plays C4 at the fixed velocity.
    press(Control::Key0);
    render();
    CHECK(playing(60));
    release(Control::Key0);
    render();
    CHECK(!playing(60));

    // B + Up: octave up, the cursor stays, B does not count as Back.
    press(Control::Down);
    tick();
    const int cursor = app().menu().cursor();
    CHECK(cursor == 1);
    press(Control::B);
    press(Control::Up);
    release(Control::Up);
    release(Control::B);
    tick();
    CHECK(app().params().get_int(ParamId::GlbOctave) == 1);
    CHECK(app().menu().cursor() == cursor);

    // Note plays an octave higher, and a key held across a shift still releases.
    press(Control::Key0);
    render();
    CHECK(playing(72));
    press(Control::B);
    press(Control::Down);
    release(Control::Down);
    release(Control::B);
    tick();
    CHECK(app().params().get_int(ParamId::GlbOctave) == 0);
    render();
    CHECK(playing(72));
    release(Control::Key0);
    render();
    CHECK(!playing(72));

    // Keys still play while B is held.
    press(Control::B);
    press(Control::Key4);
    render();
    CHECK(playing(64));
    release(Control::Key4);
    release(Control::B);
    render();
    tick();

    // Encoders: Pitch is highlighted. Coarse, fine, push to reset.
    turn(Control::Enc1, 1);
    tick();
    CHECK(app().params().get_int(ParamId::OscSemi) == 12);
    turn(Control::Enc2, -2);
    tick();
    CHECK(app().params().get_int(ParamId::OscSemi) == 10);
    press(Control::Enc2);
    release(Control::Enc2);
    tick();
    CHECK(app().params().get_int(ParamId::OscSemi) == 0);

    // Every screen renders with audio running, including through all group pages.
    press(Control::Key0);
    render();
    for (int i = 0; i < 12; ++i) {
        press(Control::Right);
        release(Control::Right);
        render();
        tick();
    }
    CHECK(app().menu().group() == 2);
    release(Control::Key0);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
