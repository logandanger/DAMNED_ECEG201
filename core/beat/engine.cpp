#include "core/beat/engine.hpp"

#include <cmath>

#include "core/config/control_map.hpp"

namespace danger {
namespace {

constexpr int kLoop = cfg::kLoopSteps;
constexpr int kBar = cfg::kStepsPerBar;
constexpr float kFs = static_cast<float>(cfg::kSampleRate);
constexpr int kSub = cfg::kBlockFrames;  // largest sub-block rendered at once
constexpr uint8_t kNoCapture = 0xFF;

// y = x - (4/27) x^3: unity slope at 0, reaches +-1 with zero slope at x = +-1.5.
inline float soft_clip(float x) {
    x = x < -1.5f ? -1.5f : (x > 1.5f ? 1.5f : x);
    return x * (1.0f - 0.148148148f * x * x);
}

// Wrap-safe: a was created before b.
inline bool older(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) < 0; }

inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

// Keys play in the register of the sample: a bass sampled at C2 plays C2 to B2.
int register_offset(const Sample& s) {
    return 12 * static_cast<int>(std::floor((static_cast<float>(s.root - kBaseNote) + 6.0f) / 12.0f));
}

// Moves gain toward target at rate per frame over n frames.
float ramp(float gain, float target, float rate, int n) {
    const float step = rate * static_cast<float>(n);
    if (gain < target) return gain + step > target ? target : gain + step;
    return gain - step < target ? target : gain - step;
}

}  // namespace

void BeatEngine::init() {
    for (uint32_t& v : key_voice_) v = Sampler::kNoVoice;
    for (uint8_t& c : key_capture_) c = kNoCapture;
    rng_.seed(0xDA4E5u);
    reverb_.set_amount(cfg::kReverbDefault);
}

void BeatEngine::set_bpm(float bpm) {
    bpm_target_.store(bpm < cfg::kBpmMin ? cfg::kBpmMin : (bpm > cfg::kBpmMax ? cfg::kBpmMax : bpm),
                      std::memory_order_relaxed);
}

void BeatEngine::set_reverb(float amount) { reverb_target_.store(clamp01(amount), std::memory_order_relaxed); }

float BeatEngine::frames_per_step() const { return kFs * 60.0f / (bpm_ * cfg::kStepsPerBeat); }

int BeatEngine::active_layers() const {
    int n = 0;
    for (const Layer& l : layers_) n += l.state == LayerState::Active;
    return n;
}

// The layer A+B clears: the current instrument's layer; else none while a new seed is pending
// (A+B then discards only that seed); else the newest layer.
int BeatEngine::current_slot() const {
    if (current_ >= 0 && layers_[current_].state == LayerState::Active) return current_;
    if (capture_count_ > 0) return -1;
    int best = -1;
    for (int i = 0; i < kLayerSlots; ++i) {
        if (layers_[i].state != LayerState::Active) continue;
        if (best < 0 || older(layers_[best].serial, layers_[i].serial)) best = i;
    }
    return best;
}

// ---- Input --------------------------------------------------------------------------------------

void BeatEngine::handle_key(const KeyEvent& e) {
    const int k = e.key;
    if (k >= hal::kKeyCount) return;
    const int drum = kKeyDrum[k];
    if (drum >= 0) {  // pads play over the loop and are never captured
        if (e.velocity) {
            const Sample& s = kDrumSamples[drum];
            sampler_.start({&s, s.root, static_cast<float>(e.velocity) / 127.0f * cfg::kDrumGain, kBusLive,
                            static_cast<uint8_t>(drum + 1), 0});
        }
        return;
    }

    const uint16_t bit = static_cast<uint16_t>(1u << k);
    if (e.velocity) {
        const Sample& s = kInstrumentSamples[instrument_];
        const int note = kBaseNote + kKeySemitone[k] + register_offset(s);
        sampler_.release(key_voice_[k]);
        key_voice_[k] = sampler_.start({&s, note, static_cast<float>(e.velocity) / 127.0f * cfg::kLiveGain, kBusLive,
                                        0, 0});
        keys_down_ |= bit;
        last_activity_ = frames_;
        if (capture_count_ < cfg::kMaxCaptureNotes) {
            capture_[capture_count_] = {step_, frac_, -1.0f, static_cast<uint8_t>(note), e.velocity,
                                        static_cast<uint8_t>(k)};
            key_capture_[k] = static_cast<uint8_t>(capture_count_++);
        } else {
            key_capture_[k] = kNoCapture;
        }
        return;
    }

    sampler_.release(key_voice_[k]);
    key_voice_[k] = Sampler::kNoVoice;
    if (!(keys_down_ & bit)) return;
    keys_down_ &= static_cast<uint16_t>(~bit);
    last_activity_ = frames_;
    const uint8_t i = key_capture_[k];
    key_capture_[k] = kNoCapture;
    if (i != kNoCapture && i < capture_count_) {
        CaptureNote& c = capture_[i];
        c.len = static_cast<float>(static_cast<int32_t>(step_ - c.step)) + (frac_ - c.frac);
    }
}

void BeatEngine::handle_command(const Command& c) {
    switch (c.type) {
        case CommandType::SelectInstrument:
            if (c.arg >= kInstrumentCount || c.arg == instrument_) return;
            commit_seed();  // the old instrument's seed is complete; its loop keeps playing
            instrument_ = c.arg;
            current_ = -1;  // the next seed starts a new layer
            break;
        case CommandType::ClearLayer: {
            const int slot = current_slot();
            capture_count_ = 0;
            for (uint8_t& k : key_capture_) k = kNoCapture;
            if (slot >= 0) fade_layer(slot, cfg::kClearLayerFade);
            if (active_layers() == 0) {
                drums_on_ = false;
                fade_drums(0, cfg::kClearLayerFade);
            }
            break;
        }
        case CommandType::ClearAll:
            capture_count_ = 0;
            for (uint8_t& k : key_capture_) k = kNoCapture;
            for (int i = 0; i < kLayerSlots; ++i) {
                if (layers_[i].state == LayerState::Active) fade_layer(i, cfg::kClearAllFade);
            }
            drums_on_ = false;
            fade_drums(0, cfg::kClearAllFade);
            break;
    }
}

// ---- Seeds and layers ---------------------------------------------------------------------------

void BeatEngine::fade_layer(int slot, float seconds) {
    Layer& l = layers_[slot];
    l.state = LayerState::FadingOut;
    l.target = 0;
    l.rate = l.gain / (seconds * kFs);
    if (current_ == slot) current_ = -1;
}

void BeatEngine::fade_drums(float target, float seconds) {
    drum_target_ = target;
    drum_rate_ = std::fabs(target - drum_gain_) / (seconds * kFs);
}

int BeatEngine::new_layer() {
    if (active_layers() >= cfg::kMaxLayers) {  // the oldest fades out slowly; it is never cut
        int oldest = -1;
        for (int i = 0; i < kLayerSlots; ++i) {
            if (layers_[i].state != LayerState::Active) continue;
            if (oldest < 0 || older(layers_[i].serial, layers_[oldest].serial)) oldest = i;
        }
        fade_layer(oldest, cfg::kEvictFadeBars * kBar * frames_per_step() / kFs);
    }
    int slot = -1;
    for (int i = 0; i < kLayerSlots && slot < 0; ++i) {
        if (layers_[i].state == LayerState::Free) slot = i;
    }
    if (slot < 0) {  // every spare slot is still fading: reuse the quietest, with a short fade
        for (int i = 0; i < kLayerSlots; ++i) {
            if (layers_[i].state == LayerState::FadingOut && (slot < 0 || layers_[i].gain < layers_[slot].gain)) slot = i;
        }
        sampler_.kill_bus(static_cast<uint8_t>(slot));
    }
    layers_[slot].reset(instrument_, ++layer_serial_, rng_.next());
    return slot;
}

void BeatEngine::commit_seed() {
    if (capture_count_ == 0) return;
    int first = 0;
    for (int i = 0; i < capture_count_; ++i) {
        CaptureNote& c = capture_[i];
        if (c.len < 0) c.len = static_cast<float>(static_cast<int32_t>(step_ - c.step)) + (frac_ - c.frac);
        const int32_t d = static_cast<int32_t>(c.step - capture_[first].step);
        if (d < 0 || (d == 0 && c.frac < capture_[first].frac)) first = i;
    }

    // Nothing audible: restart the loop so the first note played lands on step 1 of bar 1.
    bool silent = true;
    for (const Layer& l : layers_) silent = silent && l.state == LayerState::Free;
    if (silent) origin_ = step_ + 1;

    SeedNote q[cfg::kMaxCaptureNotes];
    for (int i = 0; i < capture_count_; ++i) {
        const CaptureNote& c = capture_[i];
        float rel;
        if (silent) rel = static_cast<float>(static_cast<int32_t>(c.step - capture_[first].step)) + (c.frac - capture_[first].frac);
        else rel = static_cast<float>((c.step - origin_) & (kLoop - 1)) + c.frac;
        const int step = static_cast<int>(std::floor(rel + 0.5f)) & (kLoop - 1);  // nearest 16th, wrapped
        int len = static_cast<int>(std::floor(c.len + 0.5f));
        len = len < 1 ? 1 : (len > cfg::kMaxNoteSteps ? cfg::kMaxNoteSteps : len);
        q[i] = {static_cast<uint8_t>(step), c.note, c.vel, static_cast<uint8_t>(len)};
    }

    int slot = current_;
    if (slot < 0 || layers_[slot].state != LayerState::Active || layers_[slot].instrument != instrument_) {
        slot = new_layer();
        current_ = slot;
    }
    layers_[slot].add_seed(q, capture_count_, step_ + 1);
    capture_count_ = 0;
    for (uint8_t& k : key_capture_) k = kNoCapture;

    if (!drums_on_) {
        drums_on_ = true;
        drums_.reset(rng_.next());
        fade_drums(1, cfg::kFadeInBars * kBar * frames_per_step() / kFs);
        if (!silent) new_drum_bar(loop_step() / kBar);
    }
}

// ---- Sequencing ---------------------------------------------------------------------------------

void BeatEngine::new_drum_bar(int bar) {
    float acc[kLoop] = {};
    bool onset[kLoop] = {};
    for (const Layer& l : layers_) {
        if (l.state != LayerState::Active) continue;
        for (int i = 0; i < l.count(); ++i) {
            const PatternNote& n = l.note(i);
            if (n.muted()) continue;
            const int s = n.eff_step();
            const float len = static_cast<float>(n.len < 8 ? n.len : 8);
            acc[s] += static_cast<float>(n.vel) / 127.0f * (1.0f + len / 8.0f) * Layer::note_ramp(n, step_);
            onset[s] = true;
        }
    }
    float max = 0;
    int onsets = 0;
    for (int s = 0; s < kLoop; ++s) {
        if (acc[s] > max) max = acc[s];
        onsets += onset[s];
    }
    float bar_acc[kBar];
    for (int s = 0; s < kBar; ++s) bar_acc[s] = max > 0 ? acc[bar * kBar + s] / max : 0;
    drums_.new_bar(bar, bar_acc, static_cast<float>(onsets) / kLoop);
}

void BeatEngine::play_drums(int bar_step) {
    for (int d = 0; d < kDrumCount; ++d) {
        const uint8_t v = drums_.hit(bar_step, static_cast<Drum>(d));
        if (!v) continue;
        const Sample& s = kDrumSamples[d];
        sampler_.start({&s, s.root, static_cast<float>(v) / 127.0f * cfg::kDrumGain, kBusDrums,
                        static_cast<uint8_t>(d + 1), 0});
    }
}

void BeatEngine::play_layers(int loop_step) {
    const float fps = frames_per_step();
    for (int slot = 0; slot < kLayerSlots; ++slot) {
        const Layer& l = layers_[slot];
        if (l.state == LayerState::Free) continue;
        const Sample& s = kInstrumentSamples[l.instrument];
        for (int i = 0; i < l.count(); ++i) {
            const PatternNote& n = l.note(i);
            if (n.muted() || n.eff_step() != loop_step) continue;
            // Fade-in: probability and velocity both ramp from kFadeInFloor to 1.
            const float p = cfg::kFadeInFloor + (1.0f - cfg::kFadeInFloor) * Layer::note_ramp(n, step_);
            if (p < 1.0f && !rng_.chance(p)) continue;
            sampler_.start({&s, n.pitch(), static_cast<float>(n.vel) / 127.0f * p * cfg::kLayerGain,
                            static_cast<uint8_t>(slot), 0, static_cast<uint32_t>(static_cast<float>(n.len) * fps)});
        }
    }
}

void BeatEngine::on_step() {
    const int ls = loop_step();
    if (ls == 0) {
        for (Layer& l : layers_) {
            if (l.state != LayerState::Active) continue;
            const float loops = static_cast<float>(static_cast<int32_t>(step_ - l.first_birth)) / kLoop;
            l.mutate(step_, clamp01(loops / cfg::kMutationRampLoops));
        }
    }
    if (drums_on_ || drum_gain_ > 0) {
        if (ls % kBar == 0) new_drum_bar(ls / kBar);
        play_drums(ls % kBar);
    }
    play_layers(ls);
}

// ---- Audio --------------------------------------------------------------------------------------

void BeatEngine::render(int16_t* out, size_t frames) {
    KeyEvent k;
    while (keys_.pop(k)) handle_key(k);
    Command c;
    while (commands_.pop(c)) handle_command(c);

    const float glide = 1.0f - std::exp(-static_cast<float>(frames) / (cfg::kBpmGlide * kFs));
    bpm_ += (bpm_target_.load(std::memory_order_relaxed) - bpm_) * glide;
    reverb_.set_amount(reverb_target_.load(std::memory_order_relaxed));

    if (capture_count_ && !keys_down_ &&
        frames_ - last_activity_ >= static_cast<uint64_t>(cfg::kSeedIdleCommit * kFs))
        commit_seed();

    float peak = 0;
    for (size_t done = 0; done < frames;) {
        const int n = static_cast<int>(frames - done < static_cast<size_t>(kSub) ? frames - done : kSub);
        float mono[kSub] = {};
        float left[kSub], right[kSub];

        // Split at 16th boundaries so every step fires on its exact frame.
        for (int i = 0; i < n;) {
            const float spf = 1.0f / frames_per_step();
            int to_step = static_cast<int>(std::ceil((1.0f - frac_) / spf));
            if (to_step < 1) to_step = 1;
            const int chunk = n - i < to_step ? n - i : to_step;

            float g0[kBusCount], g1[kBusCount];
            for (int b = 0; b < kLayerSlots; ++b) {
                Layer& l = layers_[b];
                g0[b] = l.gain;
                if (l.state == LayerState::FadingOut) {
                    l.gain = ramp(l.gain, l.target, l.rate, chunk);
                    if (l.gain <= 0) {
                        l.state = LayerState::Free;
                        sampler_.kill_bus(static_cast<uint8_t>(b));
                    }
                }
                g1[b] = l.gain;
            }
            g0[kBusDrums] = drum_gain_;
            drum_gain_ = ramp(drum_gain_, drum_target_, drum_rate_, chunk);
            g1[kBusDrums] = drum_gain_;
            g0[kBusLive] = g1[kBusLive] = 1.0f;

            sampler_.render(mono + i, chunk, g0, g1);
            frames_ += static_cast<uint64_t>(chunk);
            i += chunk;
            if (chunk == to_step) {
                frac_ += static_cast<float>(chunk) * spf - 1.0f;
                if (frac_ < 0) frac_ = 0;
                ++step_;
                on_step();
            } else {
                frac_ += static_cast<float>(chunk) * spf;
            }
        }

        for (int i = 0; i < n; ++i) left[i] = right[i] = mono[i];
        reverb_.process(mono, left, right, n);
        for (int i = 0; i < n; ++i) {
            const float l = soft_clip(left[i] * cfg::kMasterGain);
            const float r = soft_clip(right[i] * cfg::kMasterGain);
            *out++ = static_cast<int16_t>(l * 32767.0f);
            *out++ = static_cast<int16_t>(r * 32767.0f);
            const float a = std::fabs(l);
            if (a > peak) peak = a;
        }
        done += static_cast<size_t>(n);
    }
    publish(peak);
}

// ---- Status -------------------------------------------------------------------------------------

void BeatEngine::publish(float peak) {
    constexpr auto r = std::memory_order_relaxed;
    st_bpm_.store(bpm_, r);
    st_loop_.store(static_cast<float>(loop_step()) + frac_, r);
    st_drums_.store(drum_gain_, r);
    st_peak_.store(peak, r);
    const float idle = static_cast<float>(frames_ - last_activity_) / kFs;
    st_commit_.store(capture_count_ ? (keys_down_ ? cfg::kSeedIdleCommit : cfg::kSeedIdleCommit - idle) : -1.0f, r);
    st_inst_.store(instrument_, r);
    st_seed_.store(static_cast<uint8_t>(capture_count_), r);
    st_keys_.store(keys_down_ != 0, r);
    st_voices_.store(static_cast<uint8_t>(sampler_.active()), r);
    st_current_.store(static_cast<int8_t>(current_slot()), r);

    uint8_t order[kLayerSlots];
    int n = 0;
    for (int i = 0; i < kLayerSlots; ++i) {
        const Layer& l = layers_[i];
        st_state_[i].store(static_cast<uint8_t>(l.state), r);
        st_layer_inst_[i].store(l.instrument, r);
        st_notes_[i].store(static_cast<uint8_t>(l.count()), r);
        st_fade_[i].store(l.state == LayerState::Free ? 0.0f : l.fade_in(step_), r);
        st_gain_[i].store(l.gain, r);
        if (l.state == LayerState::Free) continue;
        int j = n++;  // insertion sort, oldest first
        while (j > 0 && older(l.serial, layers_[order[j - 1]].serial)) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = static_cast<uint8_t>(i);
    }
    for (int i = 0; i < kLayerSlots; ++i) st_order_[i].store(i < n ? order[i] : 0, r);
    st_count_.store(static_cast<uint8_t>(n), r);
}

EngineStatus BeatEngine::status() const {
    constexpr auto r = std::memory_order_relaxed;
    EngineStatus s{};
    s.bpm = st_bpm_.load(r);
    s.reverb = reverb_target_.load(r);
    s.loop_pos = st_loop_.load(r);
    s.drums = st_drums_.load(r);
    s.peak = st_peak_.load(r);
    s.commit_in = st_commit_.load(r);
    s.instrument = st_inst_.load(r);
    s.seed_notes = st_seed_.load(r);
    s.keys_down = st_keys_.load(r) != 0;
    s.voices = st_voices_.load(r);
    const int current = st_current_.load(r);
    for (int i = 0; i < kLayerSlots; ++i) {
        s.layers[i] = {static_cast<LayerState>(st_state_[i].load(r)), st_layer_inst_[i].load(r), st_notes_[i].load(r),
                       i == current, st_fade_[i].load(r), st_gain_[i].load(r)};
        s.order[i] = st_order_[i].load(r);
    }
    s.layer_count = st_count_.load(r);
    return s;
}

}  // namespace danger
