// DANGER simulator.
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "core/app.hpp"
#include "core/mirror/mirror_sender.hpp"
#include "hal/sim/hal_sim.hpp"
#include "sim/input.hpp"
#include "sim/mirror_client.hpp"
#include "sim/replica.hpp"

namespace {

constexpr int kScreenW = 320, kScreenH = 240;

struct Options {
    bool screen_only = false;
    std::string mirror_port, mirror_replay, mirror_record, screenshot;
    float scale = 0;  // 0 = automatic
    bool no_audio = false, demo = false;
    int exit_after_ms = 0;
};

void usage() {
    std::puts(
        "danger_sim [options]\n"
        "  (no flags)            full replica with audio\n"
        "  --screen-only         only the 320x240 UI\n"
        "  --mirror <port>       show the live screen of the real device (COM5, /dev/ttyACM0)\n"
        "  --scale <factor>      window scale\n"
        "  --no-audio            do not open an audio device\n"
        "  --mirror-record <f>   write this UI as a mirror stream to a file\n"
        "  --mirror-replay <f>   play a recorded mirror stream\n"
        "  --demo                hold a chord at startup\n"
        "  --screenshot <f.bmp>  save the window before exiting\n"
        "  --exit-after <ms>     quit automatically\n");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--screen-only") o.screen_only = true;
        else if (a == "--no-audio") o.no_audio = true;
        else if (a == "--demo") o.demo = true;
        else if (a == "--mirror" && (v = value())) o.mirror_port = v;
        else if (a == "--mirror-record" && (v = value())) o.mirror_record = v;
        else if (a == "--mirror-replay" && (v = value())) o.mirror_replay = v;
        else if (a == "--screenshot" && (v = value())) o.screenshot = v;
        else if (a == "--scale" && (v = value())) o.scale = float(std::atof(v));
        else if (a == "--exit-after" && (v = value())) o.exit_after_ms = std::atoi(v);
        else { usage(); return false; }
    }
    return true;
}

float automatic_scale(int w, int h) {
    SDL_Rect usable{0, 0, 1920, 1080};
    SDL_GetDisplayUsableBounds(0, &usable);
    const float fit = std::min(float(usable.w) / float(w), (float(usable.h) - 40.0f) / float(h));
    return std::min(1.0f, fit);
}

void save_screenshot(SDL_Renderer* r, const std::string& path) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
}

struct Recorder {
    FILE* file = nullptr;
    std::unique_ptr<uint16_t[]> shadow{new uint16_t[kScreenW * kScreenH]()};
    danger::mirror::Sender sender;
    uint32_t last_ms = 0;

    struct Bands {
        uint16_t* p[danger::mirror::kTilesY];
        explicit Bands(uint16_t* base) {
            for (int i = 0; i < danger::mirror::kTilesY; ++i) p[i] = base + i * danger::mirror::kBandPixels;
        }
    };
    Recorder() : sender(Bands(shadow.get()).p) {}

    static void on_flush(int x, int y, int w, int h, const uint16_t* px, void* user) {
        static_cast<Recorder*>(user)->sender.on_flush(x, y, w, h, px);
    }
    static size_t write(const uint8_t* d, size_t n, void* user) {
        return std::fwrite(d, 1, n, static_cast<Recorder*>(user)->file);
    }
    void tick(uint32_t now) {
        if (now - last_ms < 50) return;
        last_ms = now;
        sender.pump(write, this);
    }
};

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) return 1;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }

    const bool mirroring = !opt.mirror_port.empty() || !opt.mirror_replay.empty();
    const bool replica = !opt.screen_only && !mirroring;
    const int logical_w = replica ? sim::Replica::kWidth : kScreenW;
    const int logical_h = replica ? sim::Replica::kHeight : kScreenH;
    const float scale = opt.scale > 0 ? opt.scale : (replica ? automatic_scale(logical_w, logical_h) : 1.0f);

    SDL_Window* window = SDL_CreateWindow("DANGER", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          int(float(logical_w) * scale), int(float(logical_h) * scale),
                                          SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, 0) : nullptr;
    if (!renderer) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, logical_w, logical_h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, scale < 1.0f ? "linear" : "nearest");
    SDL_Texture* screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);

    sim::Inputs inputs;
    sim::Keyboard keyboard(&inputs);
    sim::Replica box(&inputs);
    std::unique_ptr<sim::MirrorClient> mirror;
    std::unique_ptr<Recorder> recorder;
    static uint16_t mirror_fb[kScreenW * kScreenH];

    if (mirroring) {
        mirror = std::make_unique<sim::MirrorClient>();
        if (!opt.mirror_port.empty()) mirror->start(sim::MirrorClient::Source::Serial, opt.mirror_port);
        else mirror->start(sim::MirrorClient::Source::Replay, opt.mirror_replay);
    } else {
        if (opt.no_audio) hal::sim::disable_audio();
        danger::app().init();
        hal::input_start(danger::App::control_sink, &danger::app());
        danger::app().start_audio();
        if (!opt.mirror_record.empty()) {
            recorder = std::make_unique<Recorder>();
            recorder->file = std::fopen(opt.mirror_record.c_str(), "wb");
            if (!recorder->file) {
                std::fprintf(stderr, "cannot write %s\n", opt.mirror_record.c_str());
                return 1;
            }
            hal::sim::set_flush_hook(Recorder::on_flush, recorder.get());
        }
        if (opt.demo) {
            for (int k : {0, 4, 7}) inputs.press(hal::key_control(k));
        }
    }

    const uint32_t start = SDL_GetTicks();
    uint32_t last_present = 0, last_title = 0;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (mirroring) continue;
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = false;
            keyboard.handle(e);
            if (replica) box.handle(e);
        }

        const uint32_t now = SDL_GetTicks();
        if (mirroring) {
            if (mirror->copy_if_new(mirror_fb)) SDL_UpdateTexture(screen, nullptr, mirror_fb, kScreenW * 2);
            if (now - last_title > 500) {
                last_title = now;
                char title[96];
                std::snprintf(title, sizeof(title), "DANGER mirror  %s  %u frames  %u errors",
                              mirror->connected() ? "connected" : "waiting for device", mirror->frames(), mirror->errors());
                SDL_SetWindowTitle(window, title);
            }
        } else {
            danger::app().ui_tick();
            if (recorder) recorder->tick(now);
        }

        if (now - last_present >= 16) {
            last_present = now;
            if (!mirroring) SDL_UpdateTexture(screen, nullptr, hal::sim::framebuffer(), kScreenW * 2);
            if (replica) {
                box.draw(renderer, screen, hal::sim::leds());
            } else {
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, screen, nullptr, nullptr);
            }
            if (opt.exit_after_ms > 0 && now - start >= uint32_t(opt.exit_after_ms)) {
                if (!opt.screenshot.empty()) save_screenshot(renderer, opt.screenshot);
                running = false;
            }
            SDL_RenderPresent(renderer);
        }
        SDL_Delay(2);
    }

    if (recorder && recorder->file) std::fclose(recorder->file);
    mirror.reset();
    SDL_Quit();
    return 0;
}
