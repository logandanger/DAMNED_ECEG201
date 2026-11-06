// SDL2 implementation of the HAL. SDL's audio thread plays the role of the audio task.
#include "hal/sim/hal_sim.hpp"

#include <SDL.h>
#include <xmmintrin.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace hal {
namespace {

constexpr int kWidth = 320, kHeight = 240, kLeds = 32;
constexpr int kSampleRate = 44100, kBlock = 128;

uint16_t g_fb[kWidth * kHeight];
uint8_t g_leds[kLeds * 3];
ControlSink g_sink = nullptr;
void* g_sink_user = nullptr;
sim::FlushHook g_hook = nullptr;
void* g_hook_user = nullptr;
bool g_audio_disabled = false;

AudioCallback g_audio_cb = nullptr;
void* g_audio_user = nullptr;
std::atomic<float> g_load{0.0f};

// SDL may ask for any byte count; the engine only renders fixed blocks.
int16_t g_stash[kBlock * 2];
int g_stash_left = 0;

void render_block() {
    const uint64_t t0 = SDL_GetPerformanceCounter();
    g_audio_cb(g_stash, kBlock, g_audio_user);
    const double seconds = double(SDL_GetPerformanceCounter() - t0) / double(SDL_GetPerformanceFrequency());
    const float load = float(seconds * kSampleRate / kBlock);
    g_load.store(g_load.load(std::memory_order_relaxed) * 0.95f + load * 0.05f, std::memory_order_relaxed);
    g_stash_left = kBlock * 2;
}

void SDLCALL sdl_audio(void*, Uint8* stream, int len) {
    _mm_setcsr(_mm_getcsr() | 0x8040);  // flush denormals to zero on x86
    auto* out = reinterpret_cast<int16_t*>(stream);
    int want = len / 2;
    while (want > 0) {
        if (g_stash_left == 0) render_block();
        const int n = std::min(want, g_stash_left);
        std::memcpy(out, g_stash + (kBlock * 2 - g_stash_left), size_t(n) * 2);
        out += n;
        want -= n;
        g_stash_left -= n;
    }
}

}  // namespace

void audio_start(AudioCallback cb, void* user) {
    if (g_audio_disabled) return;
    g_audio_cb = cb;
    g_audio_user = user;
    SDL_AudioSpec want{};
    want.freq = kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = sdl_audio;
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (dev == 0) {
        std::fprintf(stderr, "audio: %s (continuing without sound)\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(dev, 0);
}

float audio_load() { return g_load.load(std::memory_order_relaxed); }

void display_flush(int x, int y, int w, int h, uint16_t* px) {
    for (int r = 0; r < h; ++r) std::memcpy(&g_fb[(y + r) * kWidth + x], px + r * w, size_t(w) * 2);
    if (g_hook) g_hook(x, y, w, h, px, g_hook_user);
}

void leds_write(const uint8_t* rgb, int count) {
    std::memcpy(g_leds, rgb, size_t(std::min(count, kLeds)) * 3);
}

void input_start(ControlSink sink, void* user) {
    g_sink = sink;
    g_sink_user = user;
}

uint32_t millis() { return SDL_GetTicks(); }

namespace sim {

const uint16_t* framebuffer() { return g_fb; }
const uint8_t* leds() { return g_leds; }
void emit(const ControlEvent& e) { if (g_sink) g_sink(e, g_sink_user); }
void disable_audio() { g_audio_disabled = true; }
void set_flush_hook(FlushHook hook, void* user) { g_hook = hook; g_hook_user = user; }

}  // namespace sim
}  // namespace hal
