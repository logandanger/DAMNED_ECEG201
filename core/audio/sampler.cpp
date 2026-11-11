#include "core/audio/sampler.hpp"

#include <cmath>

namespace danger {
namespace {

constexpr int kFracBits = 12;
constexpr uint32_t kFracMask = (1u << kFracBits) - 1;
constexpr float kFracScale = 1.0f / (1u << kFracBits);
constexpr int kIndexBits = 6;  // handle = serial << 6 | voice index
static_assert(cfg::kVoices <= (1 << kIndexBits));
constexpr float kFs = static_cast<float>(cfg::kSampleRate);

}  // namespace

void Sampler::begin(Voice& v, const NoteStart& n) {
    const float ratio = std::exp2(static_cast<float>(n.note - n.sample->root) * (1.0f / 12.0f)) *
                        (static_cast<float>(kSampleRateHz) / kFs);
    v.sample = n.sample;
    v.pos = 0;
    v.inc = static_cast<uint32_t>(ratio * (1u << kFracBits) + 0.5f);
    v.amp = 0;
    v.step = 1.0f / (cfg::kVoiceAttack * kFs);
    v.gain = n.gain;
    v.gate = n.gate_frames;
    v.bus = n.bus;
    v.tag = n.choke_tag;
    v.stage = Stage::Attack;
}

void Sampler::fade_out(Voice& v, float seconds, Stage stage) {
    const float s = v.amp / (seconds * kFs);
    v.step = s > 1e-7f ? s : 1e-7f;
    v.stage = stage;
}

Sampler::Voice* Sampler::allocate() {
    Voice* quiet = nullptr;
    Voice* oldest = nullptr;
    Voice* oldest_any = nullptr;
    for (Voice& v : voices_) {
        if (v.stage == Stage::Idle) return &v;
        if (v.stage == Stage::Steal) continue;  // already being reused
        if (v.stage == Stage::Release && (!quiet || v.amp < quiet->amp)) quiet = &v;
        if (v.bus != kBusLive && (!oldest || static_cast<int32_t>(v.serial - oldest->serial) < 0)) oldest = &v;
        if (!oldest_any || static_cast<int32_t>(v.serial - oldest_any->serial) < 0) oldest_any = &v;
    }
    // Quietest releasing voice, then the oldest looped or drum voice, then the oldest live voice.
    return quiet ? quiet : (oldest ? oldest : oldest_any);
}

uint32_t Sampler::start(const NoteStart& n) {
    if (!n.sample) return kNoVoice;
    if (n.choke_tag) {
        for (Voice& v : voices_) {
            if (v.tag == n.choke_tag && (v.stage == Stage::Attack || v.stage == Stage::Hold || v.stage == Stage::Release))
                fade_out(v, cfg::kStealFade, Stage::Release);
        }
    }
    Voice* v = allocate();
    if (!v) return kNoVoice;
    const uint32_t serial = ++serial_;
    const uint32_t index = static_cast<uint32_t>(v - voices_);
    if (v->stage == Stage::Idle) {
        begin(*v, n);
    } else {
        v->pending = n;
        v->has_pending = true;
        fade_out(*v, cfg::kStealFade, Stage::Steal);
    }
    v->serial = serial;
    return (serial << kIndexBits) | index;
}

void Sampler::release(uint32_t handle) {
    if (handle == kNoVoice) return;
    Voice& v = voices_[handle & ((1u << kIndexBits) - 1)];
    if ((v.serial & (0xFFFFFFFFu >> kIndexBits)) != handle >> kIndexBits) return;  // voice was reused
    if (v.stage == Stage::Steal && v.has_pending) {
        v.pending.gate_frames = 1;  // released before it began: play its attack, then release
    } else if (v.stage == Stage::Attack || v.stage == Stage::Hold) {
        fade_out(v, v.sample->release_s, Stage::Release);
    }
}

void Sampler::kill_bus(uint8_t bus) {
    for (Voice& v : voices_) {
        if (v.stage != Stage::Idle && v.bus == bus) {
            v.has_pending = v.has_pending && v.pending.bus != bus;
            fade_out(v, cfg::kStealFade, v.has_pending ? Stage::Steal : Stage::Release);
        }
        if (v.has_pending && v.pending.bus == bus) v.has_pending = false;
    }
}

void Sampler::render(float* mono, int n, const float* g0, const float* g1) {
    const float inv_n = 1.0f / static_cast<float>(n);
    for (Voice& v : voices_) {
        if (v.stage == Stage::Idle) continue;
        const Sample& s = *v.sample;
        const int16_t* d = s.data;
        const uint32_t len = s.length;
        const bool looped = s.loop_end > s.loop_start;
        const uint32_t loop_end = s.loop_end << kFracBits;
        const uint32_t loop_len = (s.loop_end - s.loop_start) << kFracBits;
        const float b0 = g0[v.bus], db = (g1[v.bus] - g0[v.bus]) * inv_n;
        const float k = v.gain * (1.0f / 32768.0f);

        for (int i = 0; i < n; ++i) {
            if (v.gate && --v.gate == 0 && (v.stage == Stage::Attack || v.stage == Stage::Hold))
                fade_out(v, s.release_s, Stage::Release);

            switch (v.stage) {
                case Stage::Attack:
                    v.amp += v.step;
                    if (v.amp >= 1.0f) { v.amp = 1.0f; v.stage = Stage::Hold; }
                    break;
                case Stage::Release:
                case Stage::Steal:
                    v.amp -= v.step;
                    break;
                default: break;
            }
            if (v.amp <= 0.0f && v.stage != Stage::Attack) {
                v.amp = 0;
                if (v.stage == Stage::Steal && v.has_pending) {
                    v.has_pending = false;
                    const uint32_t serial = v.serial;
                    begin(v, v.pending);  // starts on the next chunk
                    v.serial = serial;
                } else {
                    v.stage = Stage::Idle;
                }
                break;
            }

            const uint32_t idx = v.pos >> kFracBits;
            if (idx + 1 >= len && !looped) { v.stage = Stage::Idle; break; }
            const float a = d[idx];
            const float b = idx + 1 < len ? d[idx + 1] : a;
            const float x = a + (b - a) * static_cast<float>(v.pos & kFracMask) * kFracScale;
            mono[i] += x * v.amp * k * (b0 + db * static_cast<float>(i));
            v.pos += v.inc;
            if (looped && v.pos >= loop_end) v.pos -= loop_len;
        }
    }
}

int Sampler::active() const {
    int n = 0;
    for (const Voice& v : voices_) n += v.stage != Stage::Idle;
    return n;
}

}  // namespace danger
