// Render cost on the host. Shows relative cost; the device figure comes from the CPU readout.
#include <chrono>
#include <cstdio>
#include <memory>

#include "core/beat/engine.hpp"

using namespace danger;

namespace {

std::unique_ptr<BeatEngine> g_engine;

void render_s(double seconds) {
    static int16_t block[cfg::kBlockFrames * 2];
    const int n = static_cast<int>(seconds * cfg::kSampleRate / cfg::kBlockFrames);
    for (int i = 0; i < n; ++i) g_engine->render(block, cfg::kBlockFrames);
}

void run(const char* name) {
    static int16_t block[cfg::kBlockFrames * 2];
    constexpr int kBlocks = 40000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kBlocks; ++i) g_engine->render(block, cfg::kBlockFrames);
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / kBlocks;
    std::printf("%-34s %8.0f ns/block  %5.2f %% of real time  voices %d\n", name, ns,
                ns / (1e9 * cfg::kBlockFrames / cfg::kSampleRate) * 100, g_engine->active_voices());
}

void seed(int instrument) {
    g_engine->post_command({CommandType::SelectInstrument, static_cast<uint8_t>(instrument)});
    for (int k : {0, 2, 4, 5, 7, 9, 10, 11, 7, 4}) {
        g_engine->post_key({static_cast<uint8_t>(k), 100});
        render_s(0.06);
        g_engine->post_key({static_cast<uint8_t>(k), 0});
        render_s(0.02);
    }
    render_s(cfg::kSeedIdleCommit + 0.1);
}

}  // namespace

int main() {
    g_engine.reset(new BeatEngine);
    g_engine->init();
    g_engine->set_reverb(0);
    run("idle");
    seed(0);
    render_s(20);
    run("1 layer + drums");
    for (int i = 1; i < cfg::kMaxLayers; ++i) seed(i);
    render_s(20);
    run("4 layers + drums");
    g_engine->set_reverb(1);
    render_s(1);
    run("4 layers + drums + reverb (typical)");
    int pad = 0;  // a looped instrument sustains while held, so the voices stay busy for the run
    for (int i = 0; i < kInstrumentCount; ++i)
        if (kInstrumentSamples[i].loop_end) pad = i;
    g_engine->post_command({CommandType::SelectInstrument, static_cast<uint8_t>(pad)});
    render_s(0.01);
    for (int k : {0, 2, 4, 5, 7, 9, 10, 11}) g_engine->post_key({static_cast<uint8_t>(k), 100});
    render_s(0.01);
    run("+ 8 held pad keys");
    return 0;
}
