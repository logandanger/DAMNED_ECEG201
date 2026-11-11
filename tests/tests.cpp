// Headless unit tests: samples, sampler, reverb, A/B logic, seeding, layers, mutation, drums,
// real-time rules, mirror protocol.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include "core/audio/reverb.hpp"
#include "core/audio/sampler.hpp"
#include "core/beat/drums.hpp"
#include "core/beat/engine.hpp"
#include "core/config/control_map.hpp"
#include "core/mirror/mirror_proto.hpp"
#include "core/mirror/mirror_sender.hpp"
#include "core/ui/buttons.hpp"

using namespace danger;

// Counts every heap allocation, to prove render() makes none.
static size_t g_allocs = 0;
void* operator new(size_t n) { ++g_allocs; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) do { const double x_ = (a), y_ = (b); if (std::fabs(x_ - y_) <= (tol)) ++g_pass; else { ++g_fail; \
    std::printf("FAIL %s:%d  %s = %g, expected %g\n", __FILE__, __LINE__, #a, x_, y_); } } while (0)

constexpr double kFs = cfg::kSampleRate;
constexpr int kMelodic[8] = {0, 2, 4, 5, 7, 9, 10, 11};  // keys that are not drum pads

// Engine driven in real time: keys and commands land at block starts, as on the device.
struct Rig {
    std::unique_ptr<BeatEngine> e{new BeatEngine};
    std::vector<int16_t> out;  // last run
    double t = 0;

    explicit Rig(float bpm = 120) {
        e->init();
        e->set_bpm(bpm);
        run(1.5);  // tempo glide settles
    }
    void run(double seconds) {
        const int blocks = static_cast<int>(seconds * kFs / cfg::kBlockFrames + 0.5);
        out.assign(static_cast<size_t>(blocks) * cfg::kBlockFrames * 2, 0);
        for (int b = 0; b < blocks; ++b) e->render(out.data() + static_cast<size_t>(b) * cfg::kBlockFrames * 2, cfg::kBlockFrames);
        t += blocks * cfg::kBlockFrames / kFs;
    }
    void key(int k, bool on, int vel = 100) { e->post_key({uint8_t(k), uint8_t(on ? vel : 0)}); }
    void tap(int k, double hold = 0.1) { key(k, true); run(hold); key(k, false); }
    void cmd(CommandType c, int arg = 0) { e->post_command({c, uint8_t(arg)}); }
    EngineStatus st() const { return e->status(); }
    int peak() const {
        int p = 0;
        for (int16_t s : out) p = std::max(p, std::abs(int(s)));
        return p;
    }
    int active() const {
        int n = 0;
        for (int i = 0; i < kLayerSlots; ++i) n += e->layer(i).state == LayerState::Active;
        return n;
    }
    int slot_of(int instrument) const {
        for (int i = 0; i < kLayerSlots; ++i)
            if (e->layer(i).state == LayerState::Active && e->layer(i).instrument == instrument) return i;
        return -1;
    }
    // Plays a short phrase, then waits for the commit.
    void seed(std::initializer_list<int> keys, double gap = 0.25) {
        for (int k : keys) { tap(k, gap * 0.6); run(gap * 0.4); }
        run(cfg::kSeedIdleCommit + 0.1);
    }
};

static double steps_per_s(float bpm) { return bpm / 60.0 * cfg::kStepsPerBeat; }

// ---- Samples -----------------------------------------------------------------------------------

static void test_samples() {
    CHECK(kInstrumentCount >= 2);
    for (int i = 0; i < kDrumCount; ++i) {
        const Sample& s = kDrumSamples[i];
        CHECK(s.length > 100 && s.loop_end == 0);
        CHECK(std::abs(s.data[s.length - 1]) < 64);  // ends at zero: no click at the end
    }
    for (int i = 0; i < kInstrumentCount; ++i) {
        const Sample& s = kInstrumentSamples[i];
        CHECK(s.length > 1000 && s.release_s > 0);
        CHECK(s.loop_end <= s.length && (s.loop_end == 0 || s.loop_end > s.loop_start + 1000));
        if (s.loop_end == 0) CHECK(std::abs(s.data[s.length - 1]) < 64);
    }
}

// ---- Sampler -----------------------------------------------------------------------------------

static float max_step(const std::vector<float>& x, size_t from, size_t to) {
    float m = 0;
    for (size_t i = from + 1; i < to; ++i) m = std::max(m, std::fabs(x[i] - x[i - 1]));
    return m;
}

static void render_sampler(Sampler& s, std::vector<float>& out, int frames, float g = 1) {
    float g0[kBusCount], g1[kBusCount];
    for (int b = 0; b < kBusCount; ++b) g0[b] = g1[b] = g;
    for (int done = 0; done < frames; done += 64) {
        float buf[64] = {};
        s.render(buf, 64, g0, g1);
        out.insert(out.end(), buf, buf + 64);
    }
}

static void test_sampler_pitch() {
    // A one-shot read an octave up finishes in half the time; at the root it lasts its stored length.
    const Sample& s = kInstrumentSamples[0];
    CHECK(s.loop_end == 0);
    auto frames_until_idle = [&](int note) {
        Sampler sm;
        sm.start({&s, note, 1, kBusLive, 0, 0});
        float g[kBusCount];
        for (float& x : g) x = 1;
        int frames = 0;
        float buf[64];
        while (sm.active() && frames < 10 * cfg::kSampleRate) {
            sm.render(buf, 64, g, g);
            frames += 64;
        }
        return frames;
    };
    const int root = frames_until_idle(s.root), up = frames_until_idle(s.root + 12), down = frames_until_idle(s.root - 7);
    NEAR(double(root), double(s.length) * cfg::kSampleRate / kSampleRateHz, 128);
    NEAR(double(root) / up, 2.0, 0.01);
    NEAR(double(down) / root, std::pow(2.0, 7.0 / 12.0), 0.01);
}

static void test_sampler_no_clicks() {
    const Sample* pad = &kInstrumentSamples[0];
    for (int i = 0; i < kInstrumentCount; ++i)
        if (kInstrumentSamples[i].loop_end) pad = &kInstrumentSamples[i];

    // Steady level of the sustained note, sample to sample.
    Sampler s;
    const uint32_t h = s.start({pad, pad->root, 1, 0, 0, 0});
    std::vector<float> out;
    render_sampler(s, out, 30000);
    const float normal = max_step(out, 10000, out.size());

    // Start: ramps in, no step at the first sample.
    CHECK(std::fabs(out[0]) < 0.01f);

    // Kill the bus mid-note: fades out over the steal time, never jumps.
    s.kill_bus(0);
    const size_t at = out.size();
    render_sampler(s, out, 4096);
    CHECK(max_step(out, at - 1, out.size()) <= normal * 1.05f);
    CHECK(std::fabs(out.back()) < 1e-6f);
    CHECK(s.active() == 0);
    s.release(h);  // stale handle: harmless

    // Voice stealing: fill every voice with held notes, then one more. The stolen voice fades.
    Sampler full;
    for (int i = 0; i < cfg::kVoices; ++i) full.start({pad, pad->root + (i % 12), 0.05f, 1, 0, 0});
    std::vector<float> o2;
    render_sampler(full, o2, 8192);
    const float before = max_step(o2, 4096, o2.size());
    const size_t at2 = o2.size();
    full.start({pad, pad->root + 3, 0.05f, kBusLive, 0, 0});
    render_sampler(full, o2, 8192);
    CHECK(max_step(o2, at2 - 1, o2.size()) <= before * 1.5f);
    CHECK(full.active() == cfg::kVoices);
}

static void test_reverb() {
    Reverb r;
    r.set_amount(1);
    std::vector<float> l(44100, 0), rr(44100, 0), in(44100, 0);
    in[0] = 1;
    for (int i = 0; i < 44100; i += 128) {
        for (int j = 0; j < 128 && i + j < 44100; ++j) l[size_t(i + j)] = rr[size_t(i + j)] = 0;
        r.process(in.data() + i, l.data() + i, rr.data() + i, std::min(128, 44100 - i));
    }
    float tail = 0;
    for (int i = 4410; i < 44100; ++i) tail = std::max(tail, std::fabs(l[size_t(i)]));
    CHECK(tail > 1e-4f);  // a tail exists 0.1 s after the impulse
    CHECK(std::fabs(l[2000] - rr[2000]) > 0 || std::fabs(l[3000] - rr[3000]) > 0);  // stereo
}

// ---- A/B logic ---------------------------------------------------------------------------------

static void test_buttons() {
    using A = ButtonAction;
    {
        ButtonLogic b;
        CHECK(b.press_a(0) == A::None);
        CHECK(b.tick(100) == A::None);
        CHECK(b.release_a(200) == A::Up);  // tap fires on release
        CHECK(b.press_b(300) == A::None);
        CHECK(b.release_b(400) == A::Down);
    }
    {   // A hold selects at kSelectHold, once, while held; the release does nothing.
        ButtonLogic b;
        b.press_a(1000);
        CHECK(b.tick(1000 + uint32_t(cfg::kSelectHold * 1000) - 5) == A::None);
        NEAR(b.progress(1000 + uint32_t(cfg::kSelectHold * 500)), 0.5, 0.02);
        CHECK(b.tick(1000 + uint32_t(cfg::kSelectHold * 1000)) == A::Select);
        CHECK(b.tick(5000) == A::None);
        CHECK(b.release_a(5100) == A::None);
    }
    {   // A+B, 80 ms skew, released at 0.5 s: nothing at all, no Up, no Down.
        ButtonLogic b;
        b.press_a(0);
        b.press_b(80);
        for (uint32_t t = 80; t < 580; t += 5) CHECK(b.tick(t) == A::None);
        CHECK(b.release_a(580) == A::None);
        CHECK(b.release_b(600) == A::None);
        CHECK(b.mode() == ButtonLogic::Mode::Idle);
    }
    {   // B first, then A, released after 1.5 s: clear layer, exactly once.
        ButtonLogic b;
        b.press_b(0);
        b.press_a(40);
        for (uint32_t t = 40; t < 1540; t += 5) CHECK(b.tick(t) == A::None);
        CHECK(b.release_b(1540) == A::ClearLayer);
        CHECK(b.tick(1600) == A::None);
        CHECK(b.release_a(1700) == A::None);  // the leftover A is not a tap
    }
    {   // Held 3 s: clear all while held; releases do nothing more.
        ButtonLogic b;
        b.press_a(0);
        b.press_b(30);
        int all = 0;
        for (uint32_t t = 30; t < 3500; t += 5) all += b.tick(t) == A::ClearAll;
        CHECK(all == 1);
        CHECK(b.release_a(3500) == A::None);
        CHECK(b.release_b(3500) == A::None);
    }
    {   // A held past select, then B: no chord, nothing fires until both are up.
        ButtonLogic b;
        b.press_a(0);
        CHECK(b.tick(700) == A::Select);
        b.press_b(800);
        CHECK(b.tick(5000) == A::None);
        CHECK(b.release_b(5100) == A::None);
        CHECK(b.release_a(5200) == A::None);
        b.press_a(6000);
        CHECK(b.release_a(6100) == A::Up);  // re-armed
    }
}

// ---- Engine ------------------------------------------------------------------------------------

static void test_silent_until_seed_and_live_keys() {
    Rig r;
    r.run(2);
    CHECK(r.peak() == 0);  // no drums, no loop before the first seed
    r.key(0, true);
    r.run(0.01);
    CHECK(r.peak() > 1000);  // live response within one block
    r.key(0, false);
    r.run(1);
    r.key(1, true);  // kick pad, live
    r.run(0.05);
    CHECK(r.peak() > 1000);
    r.key(1, false);
    CHECK(r.st().seed_notes == 1);  // the melodic note was captured, the pad was not
}

static void test_commit_timing() {
    Rig r;
    r.tap(0);
    r.run(cfg::kSeedIdleCommit - 0.2);
    CHECK(r.active() == 0);
    CHECK(r.st().commit_in > 0 && r.st().commit_in < 0.3f);
    r.run(0.3);
    CHECK(r.active() == 1);
    CHECK(r.st().seed_notes == 0);

    // Keys held: never commits, however long.
    Rig h;
    h.key(0, true);
    h.run(cfg::kSeedIdleCommit + 2);
    CHECK(h.active() == 0);
    h.key(0, false);
    h.run(cfg::kSeedIdleCommit + 0.1);
    CHECK(h.active() == 1);
}

static void test_quantize() {
    Rig r(120);  // 8 steps per second
    const double sps = steps_per_s(120);
    // Off-grid: 0, 2.3, 3.8 and 6.4 steps after the first note.
    r.key(0, true); r.run(0.05); r.key(0, false);
    r.run(2.3 / sps - 0.05);
    r.key(4, true); r.run(0.05); r.key(4, false);
    r.run(1.5 / sps - 0.05);
    r.key(7, true); r.run(0.05); r.key(7, false);
    r.run(2.6 / sps - 0.05);
    r.key(4, true); r.run(1.0 / sps); r.key(4, false);  // held one step
    r.run(cfg::kSeedIdleCommit + 0.1);
    const int slot = r.slot_of(0);
    CHECK(slot >= 0);
    if (slot < 0) return;
    const Layer& l = r.e->layer(slot);
    CHECK(l.count() == 4);
    int steps[4] = {}, notes[4] = {};
    for (int i = 0; i < l.count() && i < 4; ++i) { steps[i] = l.note(i).step; notes[i] = l.note(i).note; }
    CHECK(steps[0] == 0 && steps[1] == 2 && steps[2] == 4 && steps[3] == 6);  // first note on bar 1, beat 1
    CHECK(notes[0] == 60 && notes[1] == 64 && notes[2] == 67 && notes[3] == 64);
    CHECK(l.note(3).len == 1);
    // The loop restarted at the commit: the seed's first note is on loop step 0.
}

static void test_accumulate_and_switch() {
    Rig r;
    r.seed({0, 4, 7});
    const int a = r.slot_of(0);
    CHECK(a >= 0 && r.e->layer(a).count() == 3);
    r.seed({9, 11});  // same instrument: adds to the same layer
    CHECK(r.active() == 1);
    CHECK(r.e->layer(a).count() == 5);
    CHECK(r.e->layer(a).seed_count() == 5);

    r.cmd(CommandType::SelectInstrument, 1);
    r.run(0.05);
    CHECK(r.e->layer(a).state == LayerState::Active);  // the old loop keeps playing
    CHECK(r.st().instrument == 1);
    r.seed({2, 5});
    CHECK(r.active() == 2);
    const int b = r.slot_of(1);
    CHECK(b >= 0 && b != a);
    CHECK(r.e->layer(b).count() == 2);
    CHECK(r.e->layer(a).count() >= 4);  // untouched by the new layer (mutations aside)
}

static void test_switch_does_not_cut() {
    // A held live note and the old layer's notes keep sounding across an instrument switch.
    Rig r;
    r.seed({0});
    r.key(4, true);
    r.run(0.05);
    const int voices = r.e->active_voices();
    r.cmd(CommandType::SelectInstrument, 2);
    r.run(0.003);
    CHECK(r.e->active_voices() >= voices);
    r.run(0.2);
    CHECK(r.peak() > 500);
    r.key(4, false);
}

static void test_max_layers() {
    Rig r;
    for (int i = 0; i < cfg::kMaxLayers; ++i) {
        r.cmd(CommandType::SelectInstrument, i % kInstrumentCount);
        r.run(0.01);
        r.seed({kMelodic[i]});
    }
    CHECK(r.active() == cfg::kMaxLayers);
    int oldest = -1;
    for (int i = 0; i < kLayerSlots; ++i)
        if (r.e->layer(i).state == LayerState::Active && (oldest < 0 || r.e->layer(i).serial < r.e->layer(oldest).serial)) oldest = i;
    r.cmd(CommandType::SelectInstrument, cfg::kMaxLayers % kInstrumentCount);
    r.run(0.01);
    r.seed({kMelodic[5]});
    CHECK(r.active() == cfg::kMaxLayers);
    CHECK(r.e->layer(oldest).state == LayerState::FadingOut);
    const float g0 = r.e->layer(oldest).gain;
    r.run(1);
    const float g1 = r.e->layer(oldest).gain;
    CHECK(g1 < g0 && g1 > 0.5f * g0);  // slow, not cut
    r.run(cfg::kEvictFadeBars * cfg::kStepsPerBar / steps_per_s(120) + 1);
    CHECK(r.e->layer(oldest).state == LayerState::Free);
}

static void test_clear() {
    Rig r;
    r.seed({0, 4});
    r.cmd(CommandType::SelectInstrument, 1);
    r.run(0.01);
    r.seed({7});
    CHECK(r.active() == 2);
    const int cur = r.slot_of(1);
    r.cmd(CommandType::ClearLayer);
    r.run(0.05);
    CHECK(r.e->layer(cur).state == LayerState::FadingOut);
    CHECK(r.e->layer(r.slot_of(0)).state == LayerState::Active);
    r.run(cfg::kClearLayerFade);
    CHECK(r.e->layer(cur).state == LayerState::Free);
    CHECK(r.st().drums > 0.1f);  // one layer left: drums go on

    // New instrument with only a pending seed: A+B discards that seed and leaves the loops alone.
    r.cmd(CommandType::SelectInstrument, 2);
    r.run(0.01);
    r.tap(4);
    CHECK(r.st().seed_notes == 1);
    r.cmd(CommandType::ClearLayer);
    r.run(0.05);
    CHECK(r.st().seed_notes == 0);
    CHECK(r.active() == 1);
    r.run(cfg::kSeedIdleCommit + 0.5);
    CHECK(r.active() == 1);  // nothing commits later either

    r.cmd(CommandType::ClearAll);
    r.run(cfg::kClearAllFade + 0.1);
    CHECK(r.active() == 0);
    CHECK(r.st().layer_count == 0);
    CHECK(r.st().drums == 0);
    r.run(8);  // reverb tail gone, nothing new starts
    CHECK(r.peak() < 8);
}

static void test_fade_in_and_first_pass_exact() {
    Rig r;
    r.seed({0, 2, 4, 5, 7, 9});
    const int s = r.slot_of(0);
    const Layer& l = r.e->layer(s);
    CHECK(l.fade_in(r.e->global_step()) < 0.2f);
    // First full pass: no mutation.
    const double loop_s = cfg::kLoopSteps / steps_per_s(120);
    r.run(loop_s * 0.98);
    int drifted = 0;
    for (int i = 0; i < l.count(); ++i) drifted += l.note(i).drifted();
    CHECK(drifted == 0);
    CHECK(l.count() == 6);
    // Fade-in completes after kFadeInBars.
    r.run(cfg::kFadeInBars * cfg::kStepsPerBar / steps_per_s(120));
    NEAR(l.fade_in(r.e->global_step()), 1.0, 1e-6);
}

static void test_mutation_constraints() {
    Rig r(180);  // faster loops
    r.seed({0, 4, 7, 11}, 0.12);
    const int s = r.slot_of(0);
    const Layer& l = r.e->layer(s);
    const uint16_t pcs = (1u << 0) | (1u << 4) | (1u << 7) | (1u << 11);
    int any_drift = 0;
    for (int loop = 0; loop < 40; ++loop) {
        r.run(cfg::kLoopSteps / steps_per_s(180));
        int added = 0;
        for (int i = 0; i < l.count(); ++i) {
            const PatternNote& n = l.note(i);
            CHECK(pcs & (1u << (n.pitch() % 12)));  // only pitch classes that were played
            CHECK(std::abs(n.nudge) <= cfg::kMaxNudgeSteps);
            CHECK(n.pitch() >= 60 - 12 && n.pitch() <= 71 + 12);
            added += n.added();
            any_drift += n.drifted();
        }
        CHECK(added <= std::max(2, int(4 * cfg::kMaxAddedRatio)));
        CHECK(l.seed_count() == 4);
    }
    CHECK(any_drift > 0);  // it does mutate
}

static void test_drums() {
    DrumGen d;
    d.reset(7);
    float acc[cfg::kStepsPerBar] = {};
    acc[6] = 1.0f;  // a strong seed note on the "and" of 2
    d.new_bar(1, acc, 0.1f);
    CHECK(d.hit(0, Drum::Kick) > 0);
    CHECK(d.hit(4, Drum::Snare) > 0 && d.hit(12, Drum::Snare) > 0);
    CHECK(d.hit(6, Drum::Kick) > 0);  // kick follows the strong note
    int hats_sparse = 0;
    for (int s = 0; s < cfg::kStepsPerBar; ++s) hats_sparse += d.hit(s, Drum::Hat) > 0;
    const float sparse = d.density();

    DrumGen busy;
    busy.reset(7);
    float flat[cfg::kStepsPerBar] = {};
    busy.new_bar(1, flat, 0.4f);
    int hats_busy = 0;
    for (int s = 0; s < cfg::kStepsPerBar; ++s) hats_busy += busy.hit(s, Drum::Hat) > 0;
    CHECK(busy.density() > sparse);
    CHECK(hats_busy > hats_sparse);

    d.new_bar(0, flat, 0.2f);
    CHECK(d.hit(0, Drum::Crash) > 0);  // phrase boundary

    // Density walks slowly: bar-to-bar change is bounded.
    DrumGen w;
    w.reset(3);
    float prev = -1;
    for (int bar = 0; bar < 64; ++bar) {
        w.new_bar(bar % 4, flat, 0.2f);
        if (prev >= 0) CHECK(std::fabs(w.density() - prev) <= cfg::kDrumDriftPerBar + 1e-6f);
        prev = w.density();
    }
}

static void test_drums_follow_layers() {
    Rig r;
    r.seed({0});
    CHECK(r.st().drums > 0 && r.st().drums < 0.3f);  // fading in
    r.run(cfg::kFadeInBars * cfg::kStepsPerBar / steps_per_s(120) + 0.5);
    NEAR(r.st().drums, 1.0, 1e-6);
}

static void test_tempo_keeps_sync() {
    Rig r(100);
    r.seed({0, 4, 7});
    r.run(1.1);
    const float before = r.st().loop_pos;
    r.e->set_bpm(170);
    r.run(0.01);
    const float after = r.st().loop_pos;
    // Phase is continuous: a tempo change only alters the rate.
    CHECK(after >= before && after - before < 0.5f);
    const uint32_t s0 = r.e->global_step();
    r.run(2);  // glide settles to 170 BPM
    const uint32_t s1 = r.e->global_step();
    r.run(4);
    NEAR(double(r.e->global_step() - s1) / 4.0, steps_per_s(170), 0.3);
    CHECK(s1 > s0);
}

static void test_no_alloc_under_load() {
    Rig r;
    for (int i = 0; i < cfg::kMaxLayers + 1; ++i) {
        r.cmd(CommandType::SelectInstrument, i % kInstrumentCount);
        r.run(0.01);
        r.seed({0, 2, 4, 5, 7, 9, 10, 11}, 0.08);
    }
    r.e->set_reverb(1);
    const size_t before = g_allocs;
    static int16_t block[cfg::kBlockFrames * 2];
    for (int i = 0; i < 20000; ++i) {
        if (i % 50 == 0) r.e->post_key({uint8_t(kMelodic[i / 50 % 8]), uint8_t(i / 50 % 2 ? 0 : 90)});
        if (i % 300 == 0) r.e->post_key({1, 100});
        r.e->render(block, cfg::kBlockFrames);
    }
    CHECK(g_allocs == before);
}

struct Screen : mirror::Decoder::Sink {
    std::vector<uint16_t> fb = std::vector<uint16_t>(320 * 240, 0);
    int frames = 0, errors = 0, hellos = 0, rects = 0;
    void on_hello(int w, int h) override { CHECK(w == 320 && h == 240); ++hellos; }
    void on_rect(int x, int y, int w, int h, const uint16_t* px) override {
        ++rects;
        for (int r = 0; r < h; ++r) std::memcpy(&fb[size_t(y + r) * 320 + x], px + r * w, size_t(w) * 2);
    }
    void on_frame_end() override { ++frames; }
    void on_error() override { ++errors; }
};

static size_t sink_write(const uint8_t* d, size_t n, void* u) {
    auto* v = static_cast<std::vector<uint8_t>*>(u);
    v->insert(v->end(), d, d + n);
    return n;
}

static uint32_t g_lcg = 1;
static uint32_t rnd() { return g_lcg = g_lcg * 1664525u + 1013904223u; }

static void test_rle() {
    for (int trial = 0; trial < 2000; ++trial) {
        const int n = 1 + int(rnd() % 4096);
        std::vector<uint16_t> a(static_cast<size_t>(n));
        uint16_t v = 0;
        const unsigned change = 1 + rnd() % 40;
        for (auto& p : a) { if (rnd() % change == 0) v = uint16_t(rnd() >> 16); p = v; }
        std::vector<uint8_t> enc(size_t(n) * 3 + 64);
        const size_t len = mirror::rle_encode(a.data(), n, enc.data(), enc.size());
        CHECK(len > 0);
        CHECK(len <= size_t(mirror::kMaxPayload));
        std::vector<uint16_t> b(static_cast<size_t>(n));
        CHECK(mirror::rle_decode(enc.data(), len, b.data(), n));
        CHECK(a == b);
    }
    uint8_t junk[3] = {200, 1, 2};
    uint16_t out[4];
    CHECK(!mirror::rle_decode(junk, 3, out, 4));  // run overflows
}

// Deliberately separate allocations, as on the device.
static std::vector<std::vector<uint16_t>> g_bands(mirror::kTilesY, std::vector<uint16_t>(mirror::kBandPixels));

static void test_mirror_stream() {
    uint16_t* bands[mirror::kTilesY];
    for (int i = 0; i < mirror::kTilesY; ++i) bands[i] = g_bands[size_t(i)].data();
    mirror::Sender tx(bands);
    Screen rx;
    mirror::Decoder dec(&rx);
    std::vector<uint8_t> wire;

    // First full frame, a noisy pattern.
    std::vector<uint16_t> src(320 * 240);
    for (auto& p : src) p = uint16_t(rnd() >> 16);
    tx.on_flush(0, 0, 320, 240, src.data());
    CHECK(tx.pump(sink_write, &wire));
    std::printf("  full noisy frame: %zu B\n", wire.size());
    dec.feed(wire.data(), wire.size());
    CHECK(rx.fb == src && rx.frames == 1 && rx.hellos == 1 && rx.errors == 0);

    // Small update: only touched tiles travel.
    wire.clear();
    uint16_t patch[10 * 5];
    for (auto& p : patch) p = 0xABCD;
    tx.on_flush(100, 50, 10, 5, patch);
    for (int r = 0; r < 5; ++r) for (int c = 0; c < 10; ++c) src[size_t(50 + r) * 320 + 100 + c] = 0xABCD;
    CHECK(tx.pump(sink_write, &wire));
    std::printf("  10x5 patch: %zu B\n", wire.size());
    CHECK(wire.size() < 2000);
    dec.feed(wire.data(), wire.size());
    CHECK(rx.fb == src && rx.frames == 2);

    // Nothing dirty: nothing sent.
    wire.clear();
    CHECK(tx.pump(sink_write, &wire));
    CHECK(wire.empty());

    // Flat frame compresses.
    std::vector<uint16_t> flat(320 * 240, 0xFFFF);
    tx.on_flush(0, 0, 320, 240, flat.data());
    wire.clear();
    tx.pump(sink_write, &wire);
    std::printf("  flat frame: %zu B of 153600\n", wire.size());
    CHECK(wire.size() < 12000);
    dec.feed(wire.data(), wire.size());
    CHECK(rx.fb == flat);

    // Log text between packets, split feeds of random size.
    for (auto& p : src) p = uint16_t((rnd() >> 16) & 0xF800);
    tx.on_flush(0, 0, 320, 240, src.data());
    wire.clear();
    tx.pump(sink_write, &wire);
    const char log[] = "I (1234) boot: hello\r\n\xD5\xD5 M noise\xD5\x4D";
    std::vector<uint8_t> noisy(log, log + sizeof(log));
    noisy.insert(noisy.end(), wire.begin(), wire.end());
    const int errors_before = rx.errors;
    for (size_t i = 0; i < noisy.size();) {
        const size_t n = std::min(noisy.size() - i, size_t(1 + rnd() % 700));
        dec.feed(noisy.data() + i, n);
        i += n;
    }
    std::printf("  after log noise: errors +%d\n", rx.errors - errors_before);
    CHECK(rx.fb == src);

    // A corrupted payload is rejected and reported; the next full frame repairs the screen.
    tx.mark_all();
    wire.clear();
    tx.pump(sink_write, &wire);
    wire[wire.size() / 2] ^= 0x55;
    const int e0 = rx.errors;
    dec.feed(wire.data(), wire.size());
    CHECK(rx.errors > e0);
    tx.mark_all();
    wire.clear();
    tx.pump(sink_write, &wire);
    dec.feed(wire.data(), wire.size());
    CHECK(rx.fb == src);

    // A stalled link loses nothing: the rest stays dirty and goes out later.
    struct Limited { std::vector<uint8_t>* out; size_t budget; };
    auto limited = [](const uint8_t* d, size_t n, void* u) -> size_t {
        auto* l = static_cast<Limited*>(u);
        if (n > l->budget) return 0;
        l->budget -= n;
        l->out->insert(l->out->end(), d, d + n);
        return n;
    };
    for (auto& p : src) p = uint16_t(rnd() >> 16);
    tx.on_flush(0, 0, 320, 240, src.data());
    wire.clear();
    Limited lim{&wire, 20000};
    CHECK(!tx.pump(limited, &lim));
    dec.feed(wire.data(), wire.size());
    wire.clear();
    CHECK(tx.pump(sink_write, &wire));
    dec.feed(wire.data(), wire.size());
    CHECK(rx.fb == src);
}


int main() {
    const struct { const char* name; void (*fn)(); } tests[] = {
        {"samples", test_samples}, {"sampler pitch", test_sampler_pitch}, {"sampler no clicks", test_sampler_no_clicks},
        {"reverb", test_reverb}, {"A/B logic", test_buttons},
        {"silent until seed, live keys", test_silent_until_seed_and_live_keys}, {"commit timing", test_commit_timing},
        {"quantize", test_quantize}, {"accumulate and switch", test_accumulate_and_switch},
        {"switch does not cut", test_switch_does_not_cut}, {"max layers", test_max_layers}, {"clear", test_clear},
        {"fade-in, exact first pass", test_fade_in_and_first_pass_exact}, {"mutation constraints", test_mutation_constraints},
        {"drums", test_drums}, {"drums follow layers", test_drums_follow_layers}, {"tempo keeps sync", test_tempo_keeps_sync},
        {"no alloc under load", test_no_alloc_under_load}, {"rle", test_rle}, {"mirror stream", test_mirror_stream},
    };
    for (auto& t : tests) {
        std::printf("%s\n", t.name);
        t.fn();
    }
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
