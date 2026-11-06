#include "core/synth/params.hpp"

#include <cmath>
#include <cstdio>

namespace danger {
namespace {

const char* const kWaveLabels[] = {"Sine", "Saw", "Square", "Triangle", "White", "Pink"};
const char* const kFilterLabels[] = {"Low-pass", "High-pass", "Band-pass"};
const char* const kDriveLabels[] = {"Off", "Drive", "Crush"};
const char* const kModLabels[] = {"Off", "Chorus", "Phaser"};

constexpr float kC = 0.04f;   // coarse: 25 detents across the range
constexpr float kF = 0.004f;  // fine: 250 detents across the range

// Order must match ParamId.
const ParamDef kDefs[kParamCount] = {
    {"Wave", Unit::None, Curve::Enum, 0, 5, 1, 1, 1, kWaveLabels, true},
    {"Pitch", Unit::Semitone, Curve::Int, -24, 24, 0, 12, 1, nullptr, true},
    {"Fine", Unit::Cent, Curve::Int, -100, 100, 0, 10, 1, nullptr, true},
    {"Pulse width", Unit::Percent, Curve::Linear, 0.05f, 0.95f, 0.5f, kC, kF, nullptr, true},
    {"Level", Unit::Percent, Curve::Linear, 0, 1, 0.8f, kC, kF, nullptr, true},

    {"Type", Unit::None, Curve::Enum, 0, 2, 0, 1, 1, kFilterLabels, true},
    {"Cutoff", Unit::Hz, Curve::Exp, 20, 18000, 8000, kC, kF, nullptr, true},
    {"Resonance", Unit::Percent, Curve::Linear, 0, 1, 0.1f, kC, kF, nullptr, true},
    {"Env amount", Unit::Octave, Curve::Linear, -6, 6, 0, kC, kF, nullptr, true},
    {"Key track", Unit::Percent, Curve::Linear, 0, 1, 0, kC, kF, nullptr, true},

    {"Attack", Unit::Ms, Curve::Exp, 1, 5000, 5, kC, kF, nullptr, true},
    {"Decay", Unit::Ms, Curve::Exp, 5, 5000, 200, kC, kF, nullptr, true},
    {"Sustain", Unit::Percent, Curve::Linear, 0, 1, 0.8f, kC, kF, nullptr, true},
    {"Release", Unit::Ms, Curve::Exp, 5, 5000, 150, kC, kF, nullptr, true},

    {"Flt attack", Unit::Ms, Curve::Exp, 1, 5000, 5, kC, kF, nullptr, true},
    {"Flt decay", Unit::Ms, Curve::Exp, 5, 5000, 300, kC, kF, nullptr, true},
    {"Flt sustain", Unit::Percent, Curve::Linear, 0, 1, 0, kC, kF, nullptr, true},
    {"Flt release", Unit::Ms, Curve::Exp, 5, 5000, 200, kC, kF, nullptr, true},

    {"Mode", Unit::None, Curve::Enum, 0, 2, 0, 1, 1, kDriveLabels, true},
    {"Amount", Unit::Percent, Curve::Linear, 0, 1, 0.3f, kC, kF, nullptr, true},
    {"Bits", Unit::Bit, Curve::Int, 2, 16, 8, 2, 1, nullptr, true},
    {"Rate div", Unit::Factor, Curve::Int, 1, 32, 4, 4, 1, nullptr, true},

    {"Mode", Unit::None, Curve::Enum, 0, 2, 0, 1, 1, kModLabels, true},
    {"Rate", Unit::Hz, Curve::Exp, 0.05f, 8, 0.6f, kC, kF, nullptr, true},
    {"Depth", Unit::Percent, Curve::Linear, 0, 1, 0.5f, kC, kF, nullptr, true},
    {"Mix", Unit::Percent, Curve::Linear, 0, 1, 0.5f, kC, kF, nullptr, true},

    {"Time", Unit::Ms, Curve::Exp, 10, 500, 300, kC, kF, nullptr, true},
    {"Feedback", Unit::Percent, Curve::Linear, 0, 0.9f, 0.35f, kC, kF, nullptr, true},
    {"Mix", Unit::Percent, Curve::Linear, 0, 1, 0, kC, kF, nullptr, true},

    {"Size", Unit::Percent, Curve::Linear, 0, 1, 0.5f, kC, kF, nullptr, true},
    {"Damping", Unit::Percent, Curve::Linear, 0, 1, 0.5f, kC, kF, nullptr, true},
    {"Mix", Unit::Percent, Curve::Linear, 0, 1, 0, kC, kF, nullptr, true},

    {"Volume", Unit::Db, Curve::Linear, -48, 0, -3, kC, kF, nullptr, false},
    {"Octave", Unit::None, Curve::Int, -3, 3, 0, 1, 1, nullptr, false},
    {"LED level", Unit::Percent, Curve::Linear, 0, 1, 0.12f, kC, kF, nullptr, false},
};

float limit(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

const ParamDef& param_def(ParamId id) { return kDefs[static_cast<int>(id)]; }

float param_normalized(ParamId id, float value) {
    const ParamDef& d = param_def(id);
    if (d.curve == Curve::Exp) return std::log(value / d.min) / std::log(d.max / d.min);
    return (value - d.min) / (d.max - d.min);
}

float param_from_normalized(ParamId id, float n) {
    const ParamDef& d = param_def(id);
    n = limit(n, 0, 1);
    if (d.curve == Curve::Exp) return d.min * std::pow(d.max / d.min, n);
    return d.min + n * (d.max - d.min);
}

float param_step(ParamId id, float value, int detents, bool coarse) {
    const ParamDef& d = param_def(id);
    const float step = (coarse ? d.coarse : d.fine) * static_cast<float>(detents);
    if (d.curve == Curve::Int || d.curve == Curve::Enum) return limit(value + step, d.min, d.max);
    return param_from_normalized(id, param_normalized(id, value) + step);
}

void param_format(ParamId id, float v, char* out, size_t size) {
    const ParamDef& d = param_def(id);
    if (d.labels) {
        std::snprintf(out, size, "%s", d.labels[static_cast<int>(v + 0.5f)]);
        return;
    }
    switch (d.unit) {
        case Unit::Hz:
            if (v >= 1000) std::snprintf(out, size, "%.2f kHz", v / 1000);
            else if (v >= 10) std::snprintf(out, size, "%.0f Hz", v);
            else std::snprintf(out, size, "%.2f Hz", v);
            break;
        case Unit::Ms:
            if (v >= 1000) std::snprintf(out, size, "%.2f s", v / 1000);
            else std::snprintf(out, size, "%.0f ms", v);
            break;
        case Unit::Percent: std::snprintf(out, size, "%.0f %%", v * 100); break;
        case Unit::Semitone: std::snprintf(out, size, "%+.0f st", v); break;
        case Unit::Cent: std::snprintf(out, size, "%+.0f ct", v); break;
        case Unit::Db: std::snprintf(out, size, "%.1f dB", v); break;
        case Unit::Bit: std::snprintf(out, size, "%.0f bit", v); break;
        case Unit::Octave: std::snprintf(out, size, "%+.1f oct", v); break;
        case Unit::Factor: std::snprintf(out, size, "/%.0f", v); break;
        case Unit::None: std::snprintf(out, size, "%+.0f", v); break;
    }
}

void ParamStore::set(ParamId id, float v) {
    const ParamDef& d = param_def(id);
    values_[static_cast<int>(id)].store(limit(v, d.min, d.max), std::memory_order_relaxed);
    generation_.fetch_add(1, std::memory_order_release);
}

void ParamStore::reset_all() {
    for (int i = 0; i < kParamCount; ++i) reset(static_cast<ParamId>(i));
}

void ParamStore::reset_patch() {
    for (int i = 0; i < kParamCount; ++i) {
        const ParamId id = static_cast<ParamId>(i);
        if (param_def(id).in_patch) reset(id);
    }
}

}  // namespace danger
