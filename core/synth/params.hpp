// Parameter table and lock-free store. The UI writes, the audio task reads.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace danger {

enum class ParamId : uint8_t {
    OscWave, OscSemi, OscFine, OscPw, OscLevel,
    FltType, FltCutoff, FltReso, FltEnvAmt, FltKeyTrack,
    AmpA, AmpD, AmpS, AmpR,
    FenvA, FenvD, FenvS, FenvR,
    DrvMode, DrvAmount, DrvBits, DrvRate,
    ModMode, ModRate, ModDepth, ModMix,
    DlyTime, DlyFeedback, DlyMix,
    RevSize, RevDamp, RevMix,
    GlbVolume, GlbOctave, GlbLed,
    Count
};
constexpr int kParamCount = static_cast<int>(ParamId::Count);

enum class Wave : uint8_t { Sine, Saw, Square, Triangle, White, Pink };
enum class FilterType : uint8_t { LowPass, HighPass, BandPass };
enum class DriveMode : uint8_t { Off, Drive, Crush };
enum class ModMode : uint8_t { Off, Chorus, Phaser };

enum class Curve : uint8_t { Linear, Exp, Int, Enum };
enum class Unit : uint8_t { None, Hz, Ms, Percent, Semitone, Cent, Db, Bit, Octave, Factor };

struct ParamDef {
    const char* name;
    Unit unit;
    Curve curve;
    float min, max, def;
    float coarse, fine;  // Linear/Exp: normalized step. Int/Enum: absolute step.
    const char* const* labels;
    bool in_patch;  // false: survives preset changes
};

const ParamDef& param_def(ParamId id);
float param_normalized(ParamId id, float value);  // 0..1
float param_from_normalized(ParamId id, float n);
float param_step(ParamId id, float value, int detents, bool coarse);
void param_format(ParamId id, float value, char* out, size_t size);

class ParamStore {
public:
    ParamStore() { reset_all(); }

    float get(ParamId id) const {
        return values_[static_cast<int>(id)].load(std::memory_order_relaxed);
    }
    int get_int(ParamId id) const {
        const float v = get(id);
        return static_cast<int>(v + (v < 0 ? -0.5f : 0.5f));
    }
    void set(ParamId id, float v);
    void adjust(ParamId id, int detents, bool coarse) { set(id, param_step(id, get(id), detents, coarse)); }
    void reset(ParamId id) { set(id, param_def(id).def); }
    void reset_all();
    void reset_patch();

    uint32_t generation() const { return generation_.load(std::memory_order_acquire); }

private:
    std::atomic<float> values_[kParamCount];
    std::atomic<uint32_t> generation_{0};
};

}  // namespace danger
