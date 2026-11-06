#include "core/synth/engine.hpp"

#include <cmath>

namespace danger {
namespace {

constexpr float kVoiceGain = 0.35f;

inline float midi_hz(float note) { return 440.0f * std::exp2((note - 69.0f) * (1.0f / 12.0f)); }

}  // namespace

void Voice::note_on(uint8_t note, uint8_t velocity, uint32_t age) {
    note_ = note;
    age_ = age;
    velocity_gain_ = static_cast<float>(velocity) * (1.0f / 127.0f);
    pitch_key_ = -1000;
    amp_.gate_on();
    fenv_.gate_on();
}

void Voice::render(float* mix, int n, const VoiceControl& c) {
    const float key = static_cast<float>(note_) + c.pitch_semitones;
    if (key != pitch_key_) {
        pitch_key_ = key;
        dt_ = clampf(midi_hz(key) * kInvSampleRate, 1e-5f, 0.45f);
    }

    const float fenv = fenv_.next(c.filter);
    const float octaves = fenv * c.env_octaves + c.key_track * (static_cast<float>(note_) - 60.0f) * (1.0f / 12.0f);
    filter_.set(c.cutoff_hz * std::exp2(octaves), c.resonance);

    float buf[kControlFrames];
    osc_.fill(buf, n, c.wave, dt_, c.pulse_width);

    const float gain = velocity_gain_ * c.level * kVoiceGain;
    switch (c.filter_type) {
        case FilterType::LowPass:
            for (int i = 0; i < n; ++i) mix[i] += filter_.low(buf[i]) * amp_.next(c.amp) * gain;
            break;
        case FilterType::HighPass:
            for (int i = 0; i < n; ++i) mix[i] += filter_.high(buf[i]) * amp_.next(c.amp) * gain;
            break;
        case FilterType::BandPass:
            for (int i = 0; i < n; ++i) mix[i] += filter_.band(buf[i]) * amp_.next(c.amp) * gain;
            break;
    }
    if (amp_.idle()) filter_.reset();
}

void Engine::init(ParamStore* params) {
    params_ = params;
    dsp_init();
    for (int i = 0; i < kVoices; ++i) voices_[i].seed(0x9E3779B9u * static_cast<uint32_t>(i + 1));
    for (auto& s : scope_) s.store(0, std::memory_order_relaxed);
    update_controls();
    gain_ = gain_target_;
}

void Engine::update_controls() {
    const uint32_t gen = params_->generation();
    if (gen == seen_generation_) return;
    seen_generation_ = gen;

    const ParamStore& p = *params_;
    VoiceControl& c = control_;
    c.wave = static_cast<Wave>(p.get_int(ParamId::OscWave));
    c.filter_type = static_cast<FilterType>(p.get_int(ParamId::FltType));
    c.pitch_semitones = p.get(ParamId::OscSemi) + p.get(ParamId::OscFine) * 0.01f;
    c.pulse_width = p.get(ParamId::OscPw);
    c.level = p.get(ParamId::OscLevel);
    c.cutoff_hz = p.get(ParamId::FltCutoff);
    c.resonance = p.get(ParamId::FltReso);
    c.env_octaves = p.get(ParamId::FltEnvAmt);
    c.key_track = p.get(ParamId::FltKeyTrack);
    c.amp = env_coefs(p.get(ParamId::AmpA), p.get(ParamId::AmpD), p.get(ParamId::AmpS), p.get(ParamId::AmpR),
                      static_cast<float>(kSampleRate));
    c.filter = env_coefs(p.get(ParamId::FenvA), p.get(ParamId::FenvD), p.get(ParamId::FenvS), p.get(ParamId::FenvR),
                         static_cast<float>(kSampleRate) / kControlFrames);

    drive_.configure(static_cast<DriveMode>(p.get_int(ParamId::DrvMode)), p.get(ParamId::DrvAmount),
                     p.get_int(ParamId::DrvBits), p.get_int(ParamId::DrvRate));
    mod_.configure(static_cast<ModMode>(p.get_int(ParamId::ModMode)), p.get(ParamId::ModRate),
                   p.get(ParamId::ModDepth), p.get(ParamId::ModMix));
    delay_.configure(p.get(ParamId::DlyTime), p.get(ParamId::DlyFeedback), p.get(ParamId::DlyMix));
    reverb_.configure(p.get(ParamId::RevSize), p.get(ParamId::RevDamp), p.get(ParamId::RevMix));

    const float db = p.get(ParamId::GlbVolume);
    gain_target_ = db <= param_def(ParamId::GlbVolume).min ? 0.0f : std::pow(10.0f, db * 0.05f);
}

// Order: same note, idle voice, quietest releasing voice, oldest held voice.
Voice* Engine::allocate(uint8_t note) {
    for (Voice& v : voices_) {
        if (v.active() && v.note() == note) return &v;
    }
    Voice* quiet = nullptr;
    Voice* oldest = nullptr;
    for (Voice& v : voices_) {
        if (!v.active()) return &v;
        if (v.releasing() && (!quiet || v.level() < quiet->level())) quiet = &v;
        if (!oldest || v.age() < oldest->age()) oldest = &v;
    }
    return quiet ? quiet : oldest;
}

void Engine::handle_note(const NoteEvent& e) {
    if (e.velocity > 0) {
        allocate(e.note)->note_on(e.note, e.velocity, ++age_);
        return;
    }
    for (Voice& v : voices_) {
        if (v.active() && v.note() == e.note && !v.releasing()) v.note_off();
    }
}

void Engine::render(int16_t* out, size_t frames) {
    update_controls();
    if (panic_.exchange(false, std::memory_order_relaxed)) {
        for (Voice& v : voices_) v.note_off();
    }
    NoteEvent e;
    while (notes_.pop(e)) handle_note(e);

    float peak = 0;
    uint32_t w = scope_write_.load(std::memory_order_relaxed);
    const float gain_step = (gain_target_ - gain_) / static_cast<float>(frames);

    for (size_t done = 0; done < frames; done += kControlFrames) {
        float mono[kControlFrames] = {};
        float left[kControlFrames];
        float right[kControlFrames];

        for (Voice& v : voices_) {
            if (v.active()) v.render(mono, kControlFrames, control_);
        }
        drive_.process(mono, kControlFrames);
        mod_.process(mono, left, right, kControlFrames);
        delay_.process(left, right, kControlFrames);
        reverb_.process(left, right, kControlFrames);

        for (int i = 0; i < kControlFrames; ++i) {
            gain_ += gain_step;
            const float l = soft_clip(left[i] * gain_);
            const float r = soft_clip(right[i] * gain_);
            const int16_t sl = static_cast<int16_t>(l * 32767.0f);
            const int16_t sr = static_cast<int16_t>(r * 32767.0f);
            *out++ = sl;
            *out++ = sr;
            const float a = std::fabs(l);
            if (a > peak) peak = a;
            scope_[w++ & (kScopeSize - 1)].store(static_cast<int16_t>((sl + sr) / 2), std::memory_order_relaxed);
        }
    }
    gain_ = gain_target_;
    scope_write_.store(w, std::memory_order_release);
    peak_.store(peak, std::memory_order_relaxed);

    uint8_t mask = 0;
    for (int i = 0; i < kVoices; ++i) {
        if (voices_[i].active() && !voices_[i].releasing()) mask |= static_cast<uint8_t>(1u << i);
    }
    voice_mask_.store(mask, std::memory_order_relaxed);
}

void Engine::read_scope(int16_t* out, int count) const {
    const uint32_t w = scope_write_.load(std::memory_order_acquire);
    const uint32_t start = w - static_cast<uint32_t>(count);
    for (int i = 0; i < count; ++i) {
        out[i] = scope_[(start + static_cast<uint32_t>(i)) & (kScopeSize - 1)].load(std::memory_order_relaxed);
    }
}

}  // namespace danger
