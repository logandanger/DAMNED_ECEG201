// Headless unit tests: DSP, voice allocation, real-time rules, mapping, menu, mirror protocol.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "core/mirror/mirror_proto.hpp"
#include "core/mirror/mirror_sender.hpp"
#include "core/synth/engine.hpp"
#include "core/synth/presets.hpp"
#include "core/ui/mapper.hpp"
#include "core/ui/menu.hpp"

using namespace danger;
using hal::Control;
using hal::ControlEvent;
using hal::EventType;

// Counts every heap allocation, to prove render() makes none.
static size_t g_allocs = 0;
void* operator new(size_t n) { ++g_allocs; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) do { const double x_ = (a), y_ = (b); if (std::fabs(x_ - y_) <= (tol)) ++g_pass; else { ++g_fail; \
    std::printf("FAIL %s:%d  %s = %g, expected %g\n", __FILE__, __LINE__, #a, x_, y_); } } while (0)

struct Rig {
    ParamStore params;
    Engine engine;
    Rig() { engine.init(&params); }
    std::vector<int16_t> run(double seconds) {
        const int blocks = static_cast<int>(seconds * kSampleRate / kBlockFrames);
        std::vector<int16_t> out(static_cast<size_t>(blocks) * kBlockFrames * 2);
        for (int b = 0; b < blocks; ++b) engine.render(out.data() + static_cast<size_t>(b) * kBlockFrames * 2, kBlockFrames);
        return out;
    }
    void on(int n) { engine.post_note({static_cast<uint8_t>(n), 100}); }
    void off(int n) { engine.post_note({static_cast<uint8_t>(n), 0}); }
};

static double rms(const std::vector<int16_t>& s, size_t from = 0) {
    double a = 0;
    for (size_t i = from; i < s.size(); ++i) a += double(s[i]) * s[i];
    return std::sqrt(a / double(s.size() - from)) / 32768.0;
}

static double goertzel(const std::vector<int16_t>& s, double hz) {
    const double w = 2 * M_PI * hz / kSampleRate;
    double re = 0, im = 0;
    const size_t n = s.size() / 2;
    for (size_t i = 0; i < n; ++i) {
        const double win = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n));
        re += s[i * 2] * win * std::cos(w * double(i));
        im += s[i * 2] * win * std::sin(w * double(i));
    }
    return std::sqrt(re * re + im * im) / double(n) / 32768.0;
}

static double pitch_hz(const std::vector<int16_t>& s, size_t skip) {
    double first = -1, last = -1;
    int crossings = 0;
    for (size_t i = skip + 2; i < s.size(); i += 2) {
        if (s[i - 2] < 0 && s[i] >= 0) {
            const double t = double(i / 2) - double(s[i]) / double(s[i] - s[i - 2]);
            if (first < 0) first = t;
            last = t;
            ++crossings;
        }
    }
    return (crossings - 1) * double(kSampleRate) / (last - first);
}

static void test_pitch_all_waves() {
    const struct { Wave w; const char* n; } waves[] = {{Wave::Sine, "sine"}, {Wave::Saw, "saw"}, {Wave::Square, "square"}, {Wave::Triangle, "tri"}};
    for (auto& w : waves) {
        Rig r;
        r.params.set(ParamId::OscWave, float(w.w));
        r.params.set(ParamId::FltCutoff, 600);  // leaves the fundamental, so crossings are clean
        r.on(69);
        const double hz = pitch_hz(r.run(0.5), 4000);
        std::printf("  %-6s A4 -> %.2f Hz\n", w.n, hz);
        NEAR(hz, 440.0, 0.5);
    }
}

static void test_noise() {
    Rig w, p;
    w.params.set(ParamId::OscWave, float(Wave::White));
    p.params.set(ParamId::OscWave, float(Wave::Pink));
    w.params.set(ParamId::FltCutoff, 18000);
    p.params.set(ParamId::FltCutoff, 18000);
    w.on(60); p.on(60);
    const auto sw = w.run(1.0), sp = p.run(1.0);
    CHECK(rms(sw, 4000) > 0.01);
    CHECK(rms(sp, 4000) > 0.01);
    // Pink falls 3 dB/oct: 200 Hz to 3.2 kHz is 4 octaves, about 12 dB. White stays flat.
    auto band = [](const std::vector<int16_t>& s, double hz) {
        double a = 0;
        for (int i = -8; i <= 8; ++i) a += std::pow(goertzel(s, hz * (1 + i * 0.01)), 2);
        return 10 * std::log10(a);
    };
    const double tilt_p = band(sp, 200) - band(sp, 3200), tilt_w = band(sw, 200) - band(sw, 3200);
    std::printf("  tilt 200 Hz to 3.2 kHz: pink %.1f dB, white %.1f dB\n", tilt_p, tilt_w);
    CHECK(tilt_p > 6 && tilt_p < 18);
    CHECK(std::fabs(tilt_w) < 6);
}

static void test_blep_reduces_aliasing() {
    // A 3520 Hz saw has harmonics at multiples of 3520; a naive one folds energy between them.
    Rig r;
    r.params.set(ParamId::OscWave, float(Wave::Saw));
    r.params.set(ParamId::FltCutoff, 18000);
    r.params.set(ParamId::FltReso, 0);
    r.params.set(ParamId::AmpS, 1);
    r.on(105);
    const auto s = r.run(1.0);
    const double fund = goertzel(s, 3520);
    const double alias = goertzel(s, 44100.0 - 13 * 3520.0);  // 13th harmonic folded to -1660 Hz
    std::printf("  saw alias at 1.66 kHz: %.1f dB below fundamental\n", 20 * std::log10(fund / alias));
    CHECK(fund / alias > 100);  // a naive saw's 13th harmonic sits only 22 dB down; require 40 dB
}

static double filter_gain(FilterType t, double hz, double cutoff) {
    Svf f;
    f.set(float(cutoff), 0.0f);
    double a = 0, b = 0;
    const int n = 44100;
    for (int i = 0; i < n; ++i) {
        const float x = std::sin(2 * M_PI * hz * i / kSampleRate);
        const float y = t == FilterType::LowPass ? f.low(x) : t == FilterType::HighPass ? f.high(x) : f.band(x);
        if (i > n / 2) { a += double(y) * y; b += double(x) * x; }
    }
    return 10 * std::log10(a / b);
}

static void test_filter() {
    NEAR(filter_gain(FilterType::LowPass, 100, 1000), 0, 0.5);
    CHECK(filter_gain(FilterType::LowPass, 10000, 1000) < -30);
    NEAR(filter_gain(FilterType::HighPass, 15000, 1000), 0, 0.5);
    CHECK(filter_gain(FilterType::HighPass, 100, 1000) < -30);
    CHECK(filter_gain(FilterType::BandPass, 1000, 1000) > filter_gain(FilterType::BandPass, 100, 1000) + 10);
    CHECK(filter_gain(FilterType::BandPass, 1000, 1000) > filter_gain(FilterType::BandPass, 10000, 1000) + 10);

    // Maximum resonance with a swept cutoff stays bounded.
    Svf f;
    double peak = 0;
    for (int i = 0; i < 200000; ++i) {
        if (i % 32 == 0) f.set(20.0f + 19000.0f * (0.5f + 0.5f * std::sin(i * 0.001f)), 1.0f);
        const float y = f.low((i / 50) % 2 ? 1.0f : -1.0f);
        CHECK(std::isfinite(y));
        peak = std::fmax(peak, std::fabs(y));
    }
    std::printf("  SVF swept at max resonance: peak %.1f\n", peak);
    CHECK(peak < 100);
}

static void test_envelope() {
    Rig r;
    r.params.set(ParamId::OscWave, float(Wave::Sine));
    r.params.set(ParamId::AmpA, 100);
    r.params.set(ParamId::AmpD, 100);
    r.params.set(ParamId::AmpS, 0.5f);
    r.params.set(ParamId::AmpR, 100);
    r.on(69);
    const auto a = r.run(0.05);
    NEAR(r.engine.voice(0).level(), 0.5, 0.05);  // linear attack, halfway at 50 ms
    r.run(0.5);
    NEAR(r.engine.voice(0).level(), 0.5, 0.001);
    r.off(69);
    r.run(0.1);
    CHECK(r.engine.voice(0).level() < 0.002);  // -60 dB in the release time
    r.run(0.1);
    CHECK(!r.engine.voice(0).active());
    (void)a;
}

static int held_voices(const Engine& e) { int n = 0; for (int i = 0; i < kVoices; ++i) n += e.voice(i).active(); return n; }
static bool has_note(const Engine& e, int note) { for (int i = 0; i < kVoices; ++i) if (e.voice(i).active() && e.voice(i).note() == note) return true; return false; }

static void test_voice_stealing() {
    {   // Oldest held voice is stolen.
        Rig r;
        for (int n : {60, 62, 64, 65}) { r.on(n); r.run(0.01); }
        CHECK(held_voices(r.engine) == 4);
        r.on(67);
        r.run(0.01);
        CHECK(held_voices(r.engine) == 4);
        CHECK(!has_note(r.engine, 60));
        CHECK(has_note(r.engine, 62) && has_note(r.engine, 67));
    }
    {   // A releasing voice goes before a held one, however old the held one is.
        Rig r;
        r.params.set(ParamId::AmpR, 2000);
        for (int n : {60, 62, 64, 65}) { r.on(n); r.run(0.01); }
        r.off(64);
        r.run(0.01);
        r.on(67);
        r.run(0.01);
        CHECK(!has_note(r.engine, 64));
        CHECK(has_note(r.engine, 60) && has_note(r.engine, 67));
    }
    {   // Same note reuses its voice.
        Rig r;
        r.on(60); r.run(0.01); r.on(60); r.run(0.01);
        CHECK(held_voices(r.engine) == 1);
    }
    {   // Stealing does not click: the sample-to-sample step stays near the waveform's own.
        Rig r;
        r.params.set(ParamId::OscWave, float(Wave::Sine));
        r.params.set(ParamId::AmpS, 1);
        for (int n : {48, 52, 55, 59}) { r.on(n); r.run(0.05); }
        r.on(40);
        const auto s = r.run(0.02);
        int step = 0;
        for (size_t i = 2; i < s.size(); i += 2) step = std::max(step, std::abs(s[i] - s[i - 2]));
        std::printf("  steal: max step %d of 32768\n", step);
        CHECK(step < 2000);
    }
}

static void test_all_presets_and_rt() {
    for (int p = 0; p < preset_count(); ++p) {
        Rig r;
        preset_load(p, r.params);
        const size_t before = g_allocs;
        static int16_t block[kBlockFrames * 2];
        int peak = 0;
        double energy = 0;
        for (int b = 0; b < 2000; ++b) {
            if (b % 200 == 0) for (int k = 0; k < 6; ++k) r.on(48 + k * 5);
            if (b % 200 == 120) for (int k = 0; k < 6; ++k) r.off(48 + k * 5);
            r.engine.render(block, kBlockFrames);
            for (int i = 0; i < kBlockFrames * 2; ++i) {
                peak = std::max(peak, std::abs(int(block[i])));
                energy += double(block[i]) * block[i];
            }
        }
        std::printf("  %-6s peak %5.1f %%  rms %.3f\n", preset_name(p), peak / 327.68,
                    std::sqrt(energy / (2000.0 * kBlockFrames * 2)) / 32768);
        CHECK(g_allocs == before);
        CHECK(peak > 300);
        CHECK(peak <= 32767);
    }
}

static void test_effects_active() {
    const auto level_after_release = [](ParamId mix, float value) {
        Rig r;
        r.params.set(ParamId::OscWave, float(Wave::Saw));
        r.params.set(ParamId::AmpR, 5);
        r.params.set(mix, value);
        r.on(60); r.run(0.1); r.off(60); r.run(0.05);
        return rms(r.run(0.3));
    };
    CHECK(level_after_release(ParamId::DlyMix, 0) < 1e-4);
    CHECK(level_after_release(ParamId::DlyMix, 0.5f) > 1e-3);
    CHECK(level_after_release(ParamId::RevMix, 0.5f) > 1e-3);

    // Reverb tail ends and does not blow up at maximum size.
    Rig r;
    r.params.set(ParamId::RevMix, 1);
    r.params.set(ParamId::RevSize, 1);
    r.params.set(ParamId::RevDamp, 0);
    r.params.set(ParamId::DlyMix, 1);
    r.params.set(ParamId::DlyFeedback, 0.9f);
    r.on(60); r.run(0.5); r.off(60);
    const double early = rms(r.run(1)), late = (r.run(10), rms(r.run(1)));
    std::printf("  max feedback tail: %.4f then %.5f\n", early, late);
    CHECK(late < early * 0.2);

    // Chorus and phaser produce a stereo difference; dry does not.
    for (float mode : {0.f, 1.f, 2.f}) {
        Rig m;
        m.params.set(ParamId::ModMode, mode);
        m.params.set(ParamId::ModMix, 0.7f);
        m.on(60);
        const auto s = m.run(0.5);
        double d = 0;
        for (size_t i = 0; i + 1 < s.size(); i += 2) d += std::abs(s[i] - s[i + 1]);
        if (mode == 0) CHECK(d == 0); else CHECK(d > 1000);
    }

    // Crush at 3 bits leaves a few levels, and not silence.
    Rig c;
    c.params.set(ParamId::OscWave, float(Wave::Sine));
    c.params.set(ParamId::DrvMode, 2);
    c.params.set(ParamId::DrvBits, 3);
    c.params.set(ParamId::OscLevel, 1);
    c.params.set(ParamId::AmpS, 1);
    c.params.set(ParamId::GlbVolume, 0);
    c.on(60);
    const auto s = c.run(0.3);
    static bool seen[65536];
    int distinct = 0;
    for (size_t i = s.size() / 2; i < s.size(); i += 2) { auto& b = seen[s[i] + 32768]; if (!b) { b = true; ++distinct; } }
    std::printf("  crush 3 bit: %d distinct values\n", distinct);
    CHECK(distinct >= 3 && distinct <= 5);
}

static void test_soft_clip() {
    NEAR(soft_clip(0.01f) / 0.01f, 1.0, 0.001);
    NEAR(soft_clip(1.5f), 1.0, 1e-5);
    NEAR(soft_clip(9.0f), 1.0, 1e-5);
    NEAR(soft_clip(-9.0f), -1.0, 1e-5);
}

// ---- Mapping ----------------------------------------------------------------------------------
static ControlEvent press(Control c) { return {c, EventType::Press, 0, 0}; }
static ControlEvent release(Control c) { return {c, EventType::Release, 0, 0}; }
static ControlEvent turn(Control c, int d) { return {c, EventType::Turn, int16_t(d), 0}; }

static void test_mapper() {
    ControlMapper m;
    CHECK(m.map(press(Control::Up)).action == Action::MenuUp);
    CHECK(m.map(release(Control::Up)).action == Action::None);
    CHECK(m.map(press(Control::Down)).action == Action::MenuDown);
    CHECK(m.map(press(Control::Left)).action == Action::GroupPrev);
    CHECK(m.map(press(Control::Right)).action == Action::GroupNext);
    CHECK(m.map(press(Control::A)).action == Action::Select);
    CHECK(m.map(press(Control::Enc1)).action == Action::ResetValue);
    CHECK(m.map(press(Control::Enc2)).action == Action::ResetValue);
    auto t = m.map(turn(Control::Enc1, 3));
    CHECK(t.action == Action::AdjustCoarse && t.arg == 3);
    t = m.map(turn(Control::Enc2, -2));
    CHECK(t.action == Action::AdjustFine && t.arg == -2);

    // B tap = Back, on release.
    CHECK(m.map(press(Control::B)).action == Action::None);
    CHECK(m.map(release(Control::B)).action == Action::Back);

    // B + Up/Down shifts octave, never navigates, and suppresses Back.
    CHECK(m.map(press(Control::B)).action == Action::None);
    CHECK(m.map(press(Control::Up)).action == Action::OctaveUp);
    CHECK(m.map(release(Control::Up)).action == Action::None);
    CHECK(m.map(press(Control::Down)).action == Action::OctaveDown);
    CHECK(m.map(release(Control::Down)).action == Action::None);
    CHECK(m.map(release(Control::B)).action == Action::None);
    CHECK(m.map(press(Control::Up)).action == Action::MenuUp);  // back to normal

    // Unbound chord does nothing, and also suppresses Back.
    CHECK(m.map(press(Control::B)).action == Action::None);
    CHECK(m.map(press(Control::Left)).action == (kUseProposedMapping ? Action::PresetPrev : Action::None));
    CHECK(m.map(turn(Control::Enc1, 1)).action == (kUseProposedMapping ? Action::AdjustVolume : Action::None));
    CHECK(m.map(release(Control::B)).action == Action::None);

    // Up held first, then B: Up navigated once, B release is a plain tap.
    CHECK(m.map(press(Control::Up)).action == Action::MenuUp);
    CHECK(m.map(press(Control::B)).action == Action::None);
    CHECK(m.map(release(Control::Up)).action == Action::None);
    CHECK(m.map(release(Control::B)).action == Action::Back);
}

static void test_tables() {
    for (size_t i = 0; i < sizeof(kDefaultBindings) / sizeof(Binding); ++i) {
        for (size_t j = 0; j < sizeof(kProposedBindings) / sizeof(Binding); ++j) {
            const Binding &a = kDefaultBindings[i], &b = kProposedBindings[j];
            CHECK(!(a.control == b.control && a.trigger == b.trigger && a.modifier == b.modifier));
        }
    }
    bool key_seen[hal::kKeyCount] = {};
    for (const PcKey& k : kPcKeys) {
        for (const PcKey& o : kPcKeys) CHECK(&k == &o || std::strcmp(k.key, o.key) != 0);
        if (hal::is_key(k.control)) key_seen[hal::key_index(k.control)] = true;
    }
    for (bool s : key_seen) CHECK(s);
}

static void test_menu() {
    ParamStore p;
    Menu m(&p);
    auto act = [&](Action a, int arg = 0) { m.handle({a, int16_t(arg)}); };

    CHECK(m.page() == PageId::Oscillator && m.cursor() == 0);
    act(Action::MenuUp);
    CHECK(m.cursor() == 0);
    act(Action::MenuDown);
    CHECK(m.cursor() == 1);

    // Coarse and fine on an integer, then reset.
    act(Action::AdjustCoarse, 1);
    NEAR(p.get(ParamId::OscSemi), 12, 0);
    act(Action::AdjustFine, 1);
    NEAR(p.get(ParamId::OscSemi), 13, 0);
    act(Action::ResetValue);
    NEAR(p.get(ParamId::OscSemi), 0, 0);

    // A cycles an enum and wraps.
    act(Action::MenuUp);
    for (int i = 0; i < 5; ++i) act(Action::Select);
    NEAR(p.get(ParamId::OscWave), 0, 0);

    // Group order, with wrap.
    const PageId order[] = {PageId::Filter, PageId::Envelope, PageId::Effects, PageId::Global, PageId::Oscillator};
    for (PageId g : order) { act(Action::GroupNext); CHECK(m.page() == g); }
    act(Action::GroupPrev);
    CHECK(m.page() == PageId::Global);

    // Scrolling: 8 envelope rows, 6 visible.
    act(Action::GroupPrev); act(Action::GroupPrev);
    CHECK(m.page() == PageId::Envelope);
    for (int i = 0; i < 12; ++i) act(Action::MenuDown);
    CHECK(m.cursor() == 7 && m.top() == 2);
    for (int i = 0; i < 12; ++i) act(Action::MenuUp);
    CHECK(m.cursor() == 0 && m.top() == 0);

    // Enter and leave a subpage; the parent keeps its cursor.
    act(Action::GroupNext);
    act(Action::MenuDown); act(Action::MenuDown);
    act(Action::Select);
    CHECK(m.page() == PageId::Delay && m.in_subpage());
    act(Action::Back);
    CHECK(m.page() == PageId::Effects && m.cursor() == 2);
    act(Action::Back);
    CHECK(m.page() == PageId::Effects);

    // Left/Right from a subpage goes to the neighbouring group.
    act(Action::Select);
    act(Action::GroupNext);
    CHECK(m.page() == PageId::Global && !m.in_subpage());

    // Encoders and reset do nothing on a link row.
    const uint32_t gen = p.generation();
    act(Action::AdjustCoarse, 1);
    act(Action::ResetValue);
    CHECK(p.generation() == gen);

    // Presets keep global values.
    p.set(ParamId::GlbVolume, -20);
    act(Action::Select);
    CHECK(m.page() == PageId::Presets);
    act(Action::MenuDown);
    act(Action::Select);
    CHECK(m.preset() == 1);
    NEAR(p.get(ParamId::GlbVolume), -20, 0);
    NEAR(p.get(ParamId::OscWave), float(Wave::Square), 0);

    // Octave clamps at +-3.
    for (int i = 0; i < 9; ++i) act(Action::OctaveUp);
    NEAR(p.get(ParamId::GlbOctave), 3, 0);

    // Every row of every page is reachable and every param appears exactly once.
    int seen[kParamCount] = {};
    for (int g = 0; g < kGroupCount; ++g) {
        Menu w(&p);
        for (int i = 0; i < g; ++i) w.handle({Action::GroupNext, 0});
        for (int i = 0; i < w.row_count(); ++i) {
            const Row r = w.row(i);
            if (r.kind == RowKind::Param) ++seen[int(r.param)];
            if (r.kind == RowKind::Page && r.target != PageId::Presets) {
                for (int k = 0; k < i; ++k) w.handle({Action::MenuDown, 0});
                w.handle({Action::Select, 0});
                for (int j = 0; j < w.row_count(); ++j) ++seen[int(w.row(j).param)];
                w.handle({Action::Back, 0});
                for (int k = 0; k < i; ++k) w.handle({Action::MenuUp, 0});
            }
        }
    }
    for (int s : seen) CHECK(s == 1);
}

static void test_params() {
    for (int i = 0; i < kParamCount; ++i) {
        const ParamId id = ParamId(i);
        const ParamDef& d = param_def(id);
        CHECK(d.def >= d.min && d.def <= d.max);
        NEAR(param_normalized(id, d.min), 0, 1e-5);
        NEAR(param_normalized(id, d.max), 1, 1e-5);
        if (d.curve == Curve::Exp || d.curve == Curve::Linear) {
            NEAR(param_from_normalized(id, param_normalized(id, d.def)), d.def, std::fabs(d.def) * 1e-4 + 1e-5);
        }
        char buf[24];
        param_format(id, d.min, buf, sizeof buf);
        CHECK(buf[0] != 0);
        param_format(id, d.max, buf, sizeof buf);
        CHECK(buf[0] != 0);
    }
    ParamStore p;
    char b[24];
    param_format(ParamId::FltCutoff, 8000, b, sizeof b);
    CHECK(!std::strcmp(b, "8.00 kHz"));
    param_format(ParamId::AmpR, 150, b, sizeof b);
    CHECK(!std::strcmp(b, "150 ms"));
    param_format(ParamId::OscWave, 3, b, sizeof b);
    CHECK(!std::strcmp(b, "Triangle"));
}

// ---- Mirror -----------------------------------------------------------------------------------
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
        {"soft clip", test_soft_clip}, {"pitch", test_pitch_all_waves}, {"noise", test_noise},
        {"polyBLEP", test_blep_reduces_aliasing}, {"filter", test_filter}, {"envelope", test_envelope},
        {"voice stealing", test_voice_stealing}, {"presets and no-alloc", test_all_presets_and_rt},
        {"effects", test_effects_active}, {"params", test_params}, {"mapper", test_mapper},
        {"mapping tables", test_tables}, {"menu", test_menu}, {"rle", test_rle}, {"mirror stream", test_mirror_stream},
    };
    for (auto& t : tests) {
        std::printf("%s\n", t.name);
        t.fn();
    }
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
