#pragma once

#include "Modules.h"
#include "PitchCorrector.h"

#include <array>
#include <vector>

/** Harmony voices: up to two backing voices sung from the lead, a chosen interval away and always in
    the Pitch module's key / scale (Chromatic = plain intervals).

    Timing: they're made from the chain's INPUT (before Pitch), which is Pitch's latency ahead of the
    lead; each voice's own pitch shifter has that same latency, so they land in time with the lead.
    A short delay (Saturation's latency) lines them up exactly with the finished lead.
    Sound: each voice is a PitchCorrector in harmony mode (full shift, no scoop, formant control),
    then the lead's own clean-up low cut, Tone EQ and De-Esser, then its level follows the finished
    lead's (so the voices get the lead's compression and never jump out), times Level. Voice 1 sits
    left, voice 2 right, by Width. */
namespace vox {

struct VoicesParams
{
    std::array<int, 2> interval { 0, 0 };   // indices into kHarmonyIntervals (0 = off)
    double level = 50.0;                    // % of the lead's level
    double formant = 0.0;                   // semitones, the voices' own formant (- = deeper, darker backing)
    double width = 70.0;                    // % how far left / right
};

class HarmonyVoices
{
public:
    static constexpr double kFollowSeconds = 0.030;
    static constexpr double kMaxFollowDb = 24.0;

    void prepare (double sampleRate);
    void reset() noexcept;
    /** The lead's settings the voices share: key / scale and retune speed (Pitch), clean-up, EQ, de-esser. */
    void setParams (const VoicesParams& v, const PitchParams& lead, double lowCutHz, const EqParams& eq, const DeEsserParams& ds) noexcept;
    static bool isNeutral (const VoicesParams& v) noexcept { return (v.interval[0] == 0 && v.interval[1] == 0) || v.level < 0.05; }
    int latencySamples() const noexcept { return voice[0].latencySamples(); }

    /** side: the chain's mono input (Pitch's latency ahead of `ch`). ch: the finished lead (1 or 2
        channels), the voices are added to it; mono gets them too (for the delay / reverb). */
    void process (const double* side, double* const* ch, int nch, double* mono, int n) noexcept;

    /** The note each voice is singing right now (MIDI, -1 = none), for the UI. */
    std::array<int, 2> currentNotes() const noexcept;

private:
    VoicesParams params;
    double sr = 48000.0;
    std::array<PitchCorrector, 2> voice;
    std::array<std::vector<double>, 2> buf;          // a block of each voice
    std::array<std::array<Biquad, 2>, 2> lowCut {};  // per voice, two stages
    double designedLowCut = -1.0, lowCutHz = 20.0;
    VocalEQ eq;
    DeEsser deEsser;
    std::vector<double> align;                       // Saturation's latency, so they match the finished lead
    int alignPos = 0;
    std::array<double, 2> voiceMs {};
    double leadMs = 0.0, leadPeak = 0.0, peakFall = 0.0, follow = 0.0, followCoeff = 0.0, gain = 0.0, gainCoeff = 0.0;
    std::array<double, 2> panL {}, panR {};
    bool running = false;
    std::vector<double> scratchL, scratchR;
};

} // namespace vox
