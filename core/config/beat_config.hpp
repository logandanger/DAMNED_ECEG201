// Every tunable of the beat maker. Times in s, rates in Hz, levels 0..1 unless noted.
#pragma once
#include <cstdint>

namespace danger::cfg {

// ---- Audio ------------------------------------------------------------------------------------
constexpr int kSampleRate = 44100;      // Hz, output
constexpr int kBlockFrames = 128;       // frames per audio callback, 2.9 ms
constexpr int kVoices = 32;             // sampler polyphony, shared by drums, layers and live play
constexpr float kVoiceAttack = 0.002f;  // s, fade-in at every note start
constexpr float kStealFade = 0.006f;    // s, fade of a stolen or choked voice before reuse
constexpr float kMasterGain = 0.5f;
constexpr float kLiveGain = 0.8f;       // live-played melodic notes
constexpr float kLayerGain = 0.7f;      // looped layer notes
constexpr float kDrumGain = 0.75f;

// ---- Loop -------------------------------------------------------------------------------------
constexpr int kStepsPerBeat = 4;  // 16th-note grid
constexpr int kBeatsPerBar = 4;   // 4/4
constexpr int kStepsPerBar = kStepsPerBeat * kBeatsPerBar;
constexpr int kLoopBars = 4;
constexpr int kLoopSteps = kStepsPerBar * kLoopBars;  // 64
constexpr int kMaxNoteSteps = 16;                     // longest quantized note, 1 bar

// ---- Tempo (right encoder) --------------------------------------------------------------------
constexpr float kBpmDefault = 100.0f;
constexpr float kBpmMin = 60.0f, kBpmMax = 180.0f;
constexpr float kBpmPerDetent = 1.0f;
constexpr float kBpmGlide = 0.3f;  // s, time constant of the tempo glide; the loop phase never jumps

// ---- Reverb (left encoder) --------------------------------------------------------------------
constexpr float kReverbDefault = 0.25f;
constexpr float kReverbPerDetent = 0.02f;
constexpr float kReverbGlide = 0.05f;  // s, time constant of the amount change
constexpr float kReverbMaxWet = 0.6f;

// ---- Seeding ----------------------------------------------------------------------------------
constexpr float kSeedIdleCommit = 5.0f;  // s of silence after the last key release before a seed commits
constexpr int kMaxCaptureNotes = 64;     // per seed; later notes still play live but are not captured
constexpr int kMaxPatternNotes = 96;     // per layer, seed plus mutations
constexpr float kFadeInBars = 8.0f;      // new or added material ramps probability and velocity over this
constexpr float kFadeInFloor = 0.12f;    // probability and velocity scale at the start of the fade

// ---- Layers -----------------------------------------------------------------------------------
constexpr int kMaxLayers = 4;            // a 5th fades out the oldest
constexpr float kEvictFadeBars = 8.0f;   // fade-out of the evicted oldest layer
constexpr float kClearLayerFade = 1.5f;  // s
constexpr float kClearAllFade = 2.5f;    // s

// ---- Mutation ---------------------------------------------------------------------------------
constexpr float kMutationRate = 1.5f;  // expected mutations per layer per loop at full intensity
constexpr int kMutationRampLoops = 6;  // loops after the exact first pass until full intensity
constexpr int kMaxNudgeSteps = 1;      // rhythm nudge range, 16ths
constexpr float kMaxAddedRatio = 0.5f; // added notes at most this fraction of the seed (min 2)
constexpr float kAddedVelocity = 0.6f; // relative to the seed's mean velocity

// ---- Drums ------------------------------------------------------------------------------------
constexpr float kDrumDensityMin = 0.2f, kDrumDensityMax = 1.0f;
constexpr float kDrumDensityFromSeed = 2.5f;  // onsets per step of the seeds -> density
constexpr float kDrumDensityDrift = 0.15f;    // +- range of the slow random walk
constexpr float kDrumDriftPerBar = 0.03f;     // walk step per bar
constexpr float kDrumHat16th = 0.55f;         // density above which hats play 16ths
constexpr float kDrumFillChance = 0.35f;      // per 4-bar phrase, in its last bar
constexpr float kDrumAccentKick = 0.6f;       // seed accent above which a kick follows a note

// ---- A/B buttons (see docs/controls.md) -------------------------------------------------------
constexpr float kSelectHold = 0.6f;     // s, A alone: select the browsed instrument
constexpr float kClearLayerHold = 1.0f; // s, A+B released after this: clear current layer
constexpr float kClearAllHold = 3.0f;   // s, A+B held this long: clear everything

}  // namespace danger::cfg
