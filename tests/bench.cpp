// Worst-case render cost on the host, by stage. Shows relative cost; the device figure comes from
// the CPU readout on the status line.
#include <chrono>
#include <cstdio>

#include "core/synth/engine.hpp"

using namespace danger;

namespace {

Engine g_engine;
ParamStore g_params;

double run(const char* name, double base_ns) {
    static int16_t block[kBlockFrames * 2];
    constexpr int kBlocks = 40000;
    for (int i = 0; i < 2000; ++i) g_engine.render(block, kBlockFrames);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kBlocks; ++i) g_engine.render(block, kBlockFrames);
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / kBlocks;
    std::printf("%-26s %8.0f ns/block  %5.2f %% of real time  +%.0f ns\n", name, ns,
                ns / (1e9 * kBlockFrames / kSampleRate) * 100, ns - base_ns);
    return ns;
}

}  // namespace

int main() {
    g_engine.init(&g_params);
    g_params.set(ParamId::OscWave, float(Wave::Square));
    g_params.set(ParamId::AmpS, 1);
    g_params.set(ParamId::FltEnvAmt, 2);
    double t = run("idle", 0);
    for (int n : {48, 55, 60, 64}) g_engine.post_note({uint8_t(n), 100});
    t = run("4 voices, square, LP", t);
    g_params.set(ParamId::DrvMode, 1);
    t = run("+ drive", t);
    g_params.set(ParamId::ModMode, 1);
    t = run("+ chorus", t);
    g_params.set(ParamId::ModMode, 2);
    t = run("phaser instead", t);
    g_params.set(ParamId::DlyMix, 0.3f);
    t = run("+ delay", t);
    g_params.set(ParamId::RevMix, 0.3f);
    run("+ reverb (worst case)", t);
    return 0;
}
