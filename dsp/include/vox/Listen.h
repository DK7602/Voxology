#pragma once

#include "KWeighting.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vox {

/** LISTEN: hear only what a module takes out of the vocal (monitoring only). Not a host parameter: the plug-in sets it
    from its window, forgets it when the window closes and never applies it offline (an export, Auto-Edit's renders),
    so it can't end up in a bounce. The numbers are shared with the web UI. */
namespace listen {
    enum Mode : int
    {
        off = 0,
        cleanup = 1,   // what Cleanup takes out: Noise + Room (its spectral stage) and the low cut, gate, pops, clicks, breaths
        dynEq = 2,     // what the Dynamic EQ takes out: its four bands and Smooth (resonances)
        deEsser = 3,   // what the De-Esser takes out
        count = 4
    };
    inline constexpr bool isValid (int m) noexcept { return m >= 0 && m < count; }
}

/** The listen switch at the end of the chain: plays the vocal, or what one module took out in its place. The same
    design as Masterology's (its Listen buttons), in Voxology's namespace:

    - Switching never clicks. Off -> a mode: the vocal keeps playing while the mode's signal fills its delay lines
      and its level is measured (arm), then a 15 ms cross-fade. A mode -> off: a 15 ms cross-fade back. A mode ->
      another: the first fades out, the second arms, then fades in.
    - Level ("hear it" gain): what a module takes out often sits 20 - 40 dB under the vocal, so it is turned up
      toward kTargetUnderVocalDb under the vocal (K-weighted, ~1 s averages), never more than kMaxBoostDb, and
      turned down when its peaks would pass 0 dBFS. appliedGainDb() and levelUnderVocalDb() say how much, so the
      window can be honest about it. setAutoLevel (false) plays it as it is (tests).

    The caller computes the signal already aligned with the vocal (same latency) and at the level it has in it.
    prepare() allocates nothing; nothing allocates. */
class ListenMix
{
public:
    static constexpr double kFadeSeconds = 0.015;
    static constexpr double kMeasureSeconds = 0.1;       // level measured before the fade-in
    static constexpr double kTargetUnderVocalDb = 12.0;
    static constexpr double kMaxBoostDb = 30.0;
    static constexpr double kLoudnessSeconds = 1.0;
    static constexpr double kPeakReleaseSeconds = 3.0;

    void prepare (double sampleRate) noexcept;
    /** Back to the vocal at once (no fade): sample-rate changes, offline. */
    void reset() noexcept;
    void setAutoLevel (bool on) noexcept { autoLevel = on; }

    /** The mode wanted now and how many samples its signal needs once it starts being computed (its delay lines). */
    void request (int mode, int armSamples) noexcept
    {
        wanted = listen::isValid (mode) ? mode : listen::off;
        wantedArm = std::max (0, armSamples);
    }
    int requested() const noexcept { return wanted; }

    /** Once per block, before computing the signal: the mode whose signal must be computed (off: none).
        startedArming(): that mode starts this block (the caller clears its delay lines first). */
    int beginBlock() noexcept;
    bool startedArming() const noexcept { return justStarted; }
    bool isActive() const noexcept { return computing != listen::off || wanted != listen::off || vocalGain < 1.0; }

    /** One sample: `vocal` (in place) is the processed vocal, `signal` what the module took out (both nch <= 2). */
    void process (double* vocal, const double* signal, int nch) noexcept
    {
        nch = std::clamp (nch, 1, 2);
        trackVocal (vocal, nch);
        double s[2] { 0.0, 0.0 };
        if (computing != listen::off)
        {
            s[0] = signal[0];
            s[1] = nch > 1 ? signal[1] : s[0];
            trackSignal (s, nch);
        }
        step();
        const double gv = smooth (vocalGain), gs = smooth (signalGain) * level;
        for (int c = 0; c < nch; ++c)
            vocal[c] = gv * vocal[c] + gs * s[c];
    }

    /** The mode being heard or prepared now (off once back on the vocal). */
    int playing() const noexcept { return computing; }
    /** The mode whose signal is ready and playing (or fading), off while it still arms. */
    int audible() const noexcept { return armLeft == 0 ? computing : static_cast<int> (listen::off); }
    /** Gain the signal is played with (dB; + = turned up so it can be heard). */
    double appliedGainDb() const noexcept { return 20.0 * std::log10 (std::max (level, 1.0e-6)); }
    /** How far the signal sits under the vocal, as it is in it (dB; -100 when there is nothing to hear). */
    double levelUnderVocalDb() const noexcept
    {
        return signalMs > 1.0e-14 && vocalMs > 1.0e-14 ? 10.0 * std::log10 (signalMs / vocalMs) : -100.0;
    }

private:
    static double smooth (double x) noexcept { return x * x * (3.0 - 2.0 * x); }   // S-curve: s(x) + s(1 - x) = 1
    void trackVocal (const double* v, int nch) noexcept;
    void trackSignal (const double* s, int nch) noexcept;
    void step() noexcept;
    double targetLevel() const noexcept;

    double sr = 48000.0, fadeStep = 1.0, loudCoeff = 0.0, peakRelease = 0.0, levelDown = 0.0, levelUp = 0.0;
    int measureSamples = 4800;
    bool autoLevel = true;
    int wanted = listen::off, wantedArm = 0;
    int computing = listen::off, armLeft = 0;
    bool justStarted = false;
    double vocalGain = 1.0, signalGain = 0.0;      // fade positions 0..1
    double level = 1.0, levelTarget = 1.0;         // "hear it" gain (linear)
    std::array<KWeighting, 2> kwVocal {}, kwSignal {};
    double vocalMs = 0.0, signalMs = 0.0, peak = 0.0;
    long tracked = 0;
    int countdown = 0;
};

} // namespace vox
