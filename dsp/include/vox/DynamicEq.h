#pragma once

#include "Modules.h"

#include <array>

/** Dynamic EQ: four vocal problem spots that are cut only in the moments they jump out.
      1 Boom   bell (Q 1.0)   80 - 300 Hz    proximity boom, low notes and plosives blooming
      2 Mud    bell (Q 1.4)  200 - 800 Hz    boxy vowels ("oh", "oo") clouding up
      3 Nasal  bell (Q 1.6)  600 - 2500 Hz   honky, pinched words
      4 Harsh  bell (Q 1.4) 2000 - 8000 Hz   piercing, shouty notes on loud lines
    Each band listens to its own slice of the voice compared with the whole voice (so it reacts the
    same whether you whisper or shout) and learns your voice's normal balance there (how it usually sits,
    over the last few seconds). When the band jumps above that normal by more than Sensitivity allows, it is cut, by up to
    the band's Max Cut, and only for as long as it sticks out. Steady tone is left to Tone EQ, and
    "s" sounds to the De-Esser (Harsh holds still while there's more energy above 6 kHz than in it).
    Linked detection (one cut for every channel), double precision, no allocation after prepare(),
    exact pass-through when every Max Cut is 0 or the module is off. */
namespace vox {

inline constexpr int kDynBands = 4;

struct DynEqParams
{
    bool enabled = true;
    std::array<double, kDynBands> freqHz { 150, 350, 1000, 3500 };
    std::array<double, kDynBands> maxCutDb { 0, 0, 0, 0 };   // 0 = band off; up to kDynMaxCutDb
    double sensitivity = 50.0;                              // 0..100 %: how easily a jump counts
};

/** Per band: name, frequency range and default, Q, how long the detector averages (s), how fast
    the cut comes in and lets go (s). Lower bands average longer (their waves are longer). */
struct DynBandInfo { const char* name; double lo, hi, def, q, averageS, attackS, releaseS; };
inline constexpr std::array<DynBandInfo, kDynBands> kDynBandInfo {{
    { "Boom", 80.0, 300.0, 150.0, 1.0, 0.010, 0.006, 0.120 },
    { "Mud", 200.0, 800.0, 350.0, 1.4, 0.008, 0.004, 0.090 },
    { "Nasal", 600.0, 2500.0, 1000.0, 1.6, 0.006, 0.003, 0.070 },
    { "Harsh", 2000.0, 8000.0, 3500.0, 1.4, 0.004, 0.002, 0.060 },
}};
inline constexpr double kDynMaxCutDb = 12.0;

class DynamicEq
{
public:
    /** How far (dB) a band may rise above the voice's normal balance before it's pulled back
        (Sensitivity 0 % = 5.5 dB, 50 % = 3 dB, 100 % = 0.5 dB). */
    static double thresholdDb (double sensitivity) noexcept { return 5.5 - 0.05 * std::clamp (sensitivity, 0.0, 100.0); }
    static constexpr double kKneeDb = 3.0;
    /** dB of cut per dB over the threshold. Above 1 because when one slice blooms the rest of the
        voice rises a little too, so the band-vs-rest reading understates the bloom. */
    static constexpr double kSlope = 1.5;
    static constexpr double kLearnUpSeconds = 4.0, kLearnDownSeconds = 1.0;   // how "your normal" follows the voice
    static constexpr double kWarmUpSeconds = 0.05;   // voice heard before the first cut
    static constexpr double kVoiceFloorDb = -60.0;   // quieter than this (RMS dBFS) = not singing: hold
    static constexpr double kVoiceWindowDb = 24.0;   // further than this under your recent voice = a gap: hold

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const DynEqParams& p) noexcept { params = p; }
    static bool isNeutral (const DynEqParams& p) noexcept
    {
        if (! p.enabled) return true;
        for (double c : p.maxCutDb) if (c >= 0.05) return false;
        return true;
    }
    void process (double* const* ch, int nch, int n) noexcept;

    /** Deepest cut per band since the last call (dB, <= 0). */
    std::array<double, kDynBands> takeCutDb() noexcept
    {
        std::array<double, kDynBands> v {};
        for (size_t b = 0; b < v.size(); ++b) { v[b] = -maxCut[b]; maxCut[b] = 0.0; }
        return v;
    }
    /** The cut each band applies right now (dB, >= 0), for tests. */
    double currentCutDb (int band) const noexcept { return bands[static_cast<size_t> (band)].cut; }

    /** Static cut curve for a given cut (dB) at f, for the UI / tests. */
    static double bandResponseDb (int band, double cutDb, double freqHz, double f, double sr);

private:
    struct Band
    {
        Biquad detect;                                  // band-pass on the linked (mono) signal
        std::array<Biquad, kMaxChannels> cutFilter {};  // the moving bell
        double env = 0.0, env2 = 0.0, fullEnv = 0.0, fullEnv2 = 0.0, normal = 0.0, cut = 0.0;
        double curFreq = 0.0, designedFreq = -1.0, designedCut = 0.0, detectFreq = -1.0;
        double envCoeff = 0.0, cutAtk = 0.0, cutRel = 0.0;
        bool learned = false;
        double learnedSeconds = 0.0;
    };
    void design (Band& b, int index) noexcept;
    static void clearBand (Band& b) noexcept;
    bool bandActive (size_t b) const noexcept;

    DynEqParams params;
    double sr = 48000.0;
    int channels = 2;
    std::array<Band, kDynBands> bands {};
    Biquad refHp;                                       // the whole voice, rumble ignored
    std::array<Biquad, 2> sibHp {};                     // above 6 kHz: tells an "s" from a harsh note
    double sibEnv = 0.0, sibEnv2 = 0.0;
    double voiceEnv = 0.0, voiceEnv2 = 0.0, voicePeak = 0.0, voiceAvg = 0.0, voiceFall = 0.0;   // recent voice level (energy)
    double learnFast = 0.0, learnUp = 0.0, learnDown = 0.0, freqGlide = 0.0;
    int countdown = 0;
    std::array<double, kDynBands> maxCut {};
};

} // namespace vox
