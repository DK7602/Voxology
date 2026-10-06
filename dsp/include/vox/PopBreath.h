#pragma once

#include "Modules.h"

#include <array>
#include <vector>

/** Two clean-up tools that work only in the moments they're needed (part of the Cleanup module,
    after the low cut and gate). Both detect on the channels linked, run in double precision, never
    allocate after prepare() and pass audio through exactly when off.

    Look-ahead for free: in the chain they sit after Pitch, which delays the audio by its latency
    anyway. So they can listen to the chain's INPUT (the side-chain, `lookAheadAvailable` samples ahead
    of the audio they process) and act a little before the event arrives, without adding latency.
    Without a side-chain (sc == nullptr) they listen to the audio itself (no look-ahead). */
namespace vox {

// ------------------------------------------------------------------------------------------------
/** Plosive remover: "p" and "b" (and mic bumps) hit the mic with a burst of low end that's far
    bigger than the voice's own low end. The low band (under ~150 Hz) is compared with the rest of
    the voice; the remover learns how they normally sit and, when the low band suddenly jumps well
    past that, a steep low shelf (two in series, 160 Hz) cuts the thump for those few tens of ms.
    Amount sets how far it may cut (100 % = 24 dB). */
struct PopParams
{
    double amount = 0.0;   // 0..100 %; 0 = off
};

class PopRemover
{
public:
    static constexpr double kMaxCutDb = 24.0;
    static constexpr double kSplitHz = 150.0, kShelfHz = 160.0;
    static constexpr double kThresholdDb = 9.0;        // low band this far over its normal = a pop
    static constexpr double kKneeDb = 4.0, kSlope = 1.5;
    static constexpr double kLoudWindowDb = 18.0;      // the burst must be within this of the recent voice level

    static constexpr double kLookAheadSeconds = 0.003;   // the cut is in place this long before the thump

    void prepare (double sampleRate, int numChannels, int lookAheadAvailable = 0);
    void reset() noexcept;
    void setParams (const PopParams& p) noexcept { params = p; }
    static bool isNeutral (const PopParams& p) noexcept { return p.amount < 0.05; }
    /** sc: the mono side-chain, lookAheadAvailable samples ahead of ch (or nullptr). */
    void process (double* const* ch, int nch, int n, const double* sc = nullptr) noexcept;

    double takeCutDb() noexcept { const double v = maxCut; maxCut = 0.0; return -v; }   // deepest cut, dB <= 0
    double currentCutDb() const noexcept { return cut; }

private:
    void design() noexcept;

    PopParams params;
    double sr = 48000.0;
    int channels = 2;
    std::array<Biquad, 2> lowLp {}, restHp {};
    std::array<std::array<Biquad, 2>, kMaxChannels> shelf {};
    double lowEnv = 0.0, restEnv = 0.0, envAtk = 0.0, envRel = 0.0;
    double voiceMs = 0.0, voiceMs2 = 0.0, voicePeak = 0.0, voiceAvg = 0.0, voiceFall = 0.0;
    double normal = 0.0, learnedSeconds = 0.0;
    double cut = 0.0, cutAtk = 0.0, cutRel = 0.0, designedCut = 0.0;
    int countdown = 0;
    bool running = false;
    double maxCut = 0.0;
    std::vector<double> delayLine;   // the detector's decisions, held back to line up with the audio
    int delayPos = 0;
};

// ------------------------------------------------------------------------------------------------
/** Breath control: turns breaths down by Reduction, leaving the words alone. A breath is quieter
    than the voice, airy (its energy sits in 1.5 - 6 kHz, with little of the 100 - 800 Hz body a
    sung or spoken sound has), not an "s" (those sit above 6 kHz), and lasts longer than a
    consonant (at least 100 ms: a "sh", "f" or "h" is over before it acts). It's turned down smoothly
    (20 ms in); the moment a word or an "s" starts it lets go within a few ms, and with the
    side-chain it does so before the word arrives, so word starts are never dulled. */
struct BreathParams
{
    double reductionDb = 0.0;   // 0..24 dB; 0 = off
};

class BreathControl
{
public:
    static constexpr double kMaxReductionDb = 24.0;
    static constexpr double kBelowVoiceDb = 8.0;     // a breath is at least this far under the recent voice level
    static constexpr double kFloorBelowVoiceDb = 50.0;
    static constexpr double kAiryDb = 2.0;           // 1.5 - 6 kHz must beat 100 - 800 Hz by this much
    static constexpr double kMinSeconds = 0.10, kHoldSeconds = 0.04;

    static constexpr double kLookAheadSeconds = 0.025;   // lets go this long before the next word

    void prepare (double sampleRate, int numChannels, int lookAheadAvailable = 0);
    void reset() noexcept;
    void setParams (const BreathParams& p) noexcept { params = p; }
    static bool isNeutral (const BreathParams& p) noexcept { return p.reductionDb < 0.05; }
    /** sc: the mono side-chain, lookAheadAvailable samples ahead of ch (or nullptr). */
    void process (double* const* ch, int nch, int n, const double* sc = nullptr) noexcept;

    double takeGainDb() noexcept { const double v = minGain; minGain = 0.0; return v; }   // deepest turn-down, dB <= 0
    bool inBreath() const noexcept { return breath; }
    double currentGainDb() const noexcept { return gainDb; }

private:
    BreathParams params;
    double sr = 48000.0;
    int channels = 2;
    std::array<Biquad, 2> body {}, air {};   // 100 - 800 Hz and 1.5 - 6 kHz band-passes (HP then LP)
    Biquad top, refHp;                       // above 6 kHz; the whole voice above 80 Hz
    double bodyMs = 0.0, airMs = 0.0, topMs = 0.0, fullMs = 0.0;
    double bodyMs2 = 0.0, airMs2 = 0.0, topMs2 = 0.0, fullMs2 = 0.0, avg = 0.0;
    double voicePeak = 0.0, voiceFall = 0.0;
    int likeCount = 0, unlikeCount = 0;
    bool breath = false, running = false;
    double gainDb = 0.0, gain = 1.0, downCoeff = 0.0, upCoeff = 0.0, fastUpCoeff = 0.0;
    double minGain = 0.0;
    std::vector<signed char> delayLine;   // decisions (1 breath, 0 hold, -1 a word / "s" started), lined up with the audio
    int delayPos = 0;
};

} // namespace vox
