#include "core/synth/presets.hpp"

namespace danger {
namespace {

using P = ParamId;
struct Value { ParamId id; float value; };
struct Preset { const char* name; const Value* values; int count; };

constexpr float kSine = 0, kSaw = 1, kSquare = 2, kTri = 3, kWhite = 4, kPink = 5;

const Value kInit[] = {{P::OscWave, kSaw}};
const Value kBass[] = {
    {P::OscWave, kSquare}, {P::OscSemi, -12}, {P::OscPw, 0.3f}, {P::FltCutoff, 280}, {P::FltReso, 0.45f},
    {P::FltEnvAmt, 3.5f}, {P::FenvD, 180}, {P::AmpD, 300}, {P::AmpS, 0.7f}, {P::AmpR, 60},
    {P::DrvMode, 1}, {P::DrvAmount, 0.25f}};
const Value kLead[] = {
    {P::OscWave, kSaw}, {P::FltCutoff, 1400}, {P::FltReso, 0.35f}, {P::FltEnvAmt, 2.5f}, {P::FltKeyTrack, 0.5f},
    {P::FenvD, 400}, {P::AmpS, 0.9f}, {P::AmpR, 220}, {P::DlyMix, 0.22f}, {P::DlyTime, 340}, {P::RevMix, 0.12f}};
const Value kPad[] = {
    {P::OscWave, kTri}, {P::FltCutoff, 2200}, {P::AmpA, 900}, {P::AmpD, 1200}, {P::AmpS, 0.85f}, {P::AmpR, 1800},
    {P::ModMode, 1}, {P::ModRate, 0.35f}, {P::ModDepth, 0.6f}, {P::ModMix, 0.5f}, {P::RevMix, 0.35f},
    {P::RevSize, 0.8f}};
const Value kKeys[] = {
    {P::OscWave, kSine}, {P::AmpD, 900}, {P::AmpS, 0.15f}, {P::AmpR, 300}, {P::DrvMode, 1}, {P::DrvAmount, 0.15f},
    {P::ModMode, 2}, {P::ModRate, 0.8f}, {P::ModDepth, 0.4f}, {P::ModMix, 0.4f}, {P::RevMix, 0.15f}};
const Value kPluck[] = {
    {P::OscWave, kSaw}, {P::FltCutoff, 500}, {P::FltReso, 0.3f}, {P::FltEnvAmt, 4.5f}, {P::FenvD, 140},
    {P::AmpD, 350}, {P::AmpS, 0}, {P::AmpR, 200}, {P::DlyMix, 0.3f}, {P::DlyTime, 250}, {P::DlyFeedback, 0.45f}};
const Value kWind[] = {
    {P::OscWave, kPink}, {P::OscLevel, 1}, {P::FltType, 2}, {P::FltCutoff, 900}, {P::FltReso, 0.4f}, {P::AmpA, 1200},
    {P::AmpR, 2000},
    {P::ModMode, 2}, {P::ModRate, 0.15f}, {P::ModDepth, 0.8f}, {P::ModMix, 0.5f}, {P::RevMix, 0.4f},
    {P::RevSize, 0.85f}};
const Value kCrush[] = {
    {P::OscWave, kSquare}, {P::FltCutoff, 5000}, {P::DrvMode, 2}, {P::DrvBits, 5}, {P::DrvRate, 6}, {P::AmpS, 0.6f},
    {P::DlyMix, 0.18f}, {P::DlyTime, 120}};
const Value kHat[] = {
    {P::OscWave, kWhite}, {P::FltType, 1}, {P::FltCutoff, 6000}, {P::AmpD, 70}, {P::AmpS, 0}, {P::AmpR, 60},
    {P::RevMix, 0.1f}};

template <int N>
constexpr Preset make(const char* name, const Value (&values)[N]) { return {name, values, N}; }

const Preset kPresets[] = {
    make("Init", kInit), make("Bass", kBass), make("Lead", kLead), make("Pad", kPad), make("Keys", kKeys),
    make("Pluck", kPluck), make("Wind", kWind), make("Crush", kCrush), make("Hat", kHat),
};

}  // namespace

int preset_count() { return static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])); }
const char* preset_name(int index) { return kPresets[index].name; }

void preset_load(int index, ParamStore& params) {
    params.reset_patch();
    const Preset& p = kPresets[index];
    for (int i = 0; i < p.count; ++i) params.set(p.values[i].id, p.values[i].value);
}

}  // namespace danger
