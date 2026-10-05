#pragma once

// Test-signal generators shared by the unit tests.

#include "vox/FilterDesign.h"

#include <cmath>
#include <numbers>
#include <random>
#include <vector>

namespace testsig {

using Clip = std::vector<std::vector<float>>;

inline double rmsDb (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > x.size()) to = x.size();
    double e = 0.0;
    for (size_t i = from; i < to; ++i) e += static_cast<double> (x[i]) * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (std::max<size_t> (1, to - from)) + 1.0e-30);
}

inline std::vector<float> sine (double sr, double freq, double peakDb, double seconds)
{
    std::vector<float> x (static_cast<size_t> (sr * seconds));
    const double a = std::pow (10.0, peakDb / 20.0);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float> (a * std::sin (2.0 * std::numbers::pi * freq * static_cast<double> (i) / sr));
    return x;
}

/** A stand-in for a recorded rap vocal: a buzzy voice (f0 ~ 140 Hz with vibrato, three formants)
    in 250 ms words with gaps, harsh "s" bursts (5 - 9 kHz noise) at the start of some words,
    room noise at noiseDb (RMS) and alternating loud / quiet lines. Mono. */
inline std::vector<float> vocal (double sr, double seconds, double noiseDb = -62.0, double sibDb = -14.0, unsigned seed = 3,
                                 double f0 = 140.0, bool boomy = false)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> white (0.0, 1.0);
    const size_t n = static_cast<size_t> (sr * seconds);
    std::vector<double> voice (n, 0.0), sib (n, 0.0);

    // Glottal-ish source: a sum of harmonics with -6 dB/oct roll-off.
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double f = f0 * (1.0 + 0.01 * std::sin (2.0 * std::numbers::pi * 5.0 * t)) * (1.0 + 0.08 * std::sin (2.0 * std::numbers::pi * 0.4 * t));
        phase += f / sr;
        double s = 0.0;
        for (int h = 1; h * f < 0.45 * sr && h < 60; ++h) s += std::sin (2.0 * std::numbers::pi * h * phase) / h;
        voice[i] = s;
    }
    // Formants.
    vox::Biquad f1, f2, f3, lowBoost;
    vox::design::apply (f1, vox::design::bell (600.0, 10.0, 2.0, sr));
    vox::design::apply (f2, vox::design::bell (1400.0, 8.0, 2.5, sr));
    vox::design::apply (f3, vox::design::bell (2700.0, 6.0, 3.0, sr));
    vox::design::apply (lowBoost, vox::design::lowShelf (200.0, boomy ? 9.0 : 0.0, sr));
    for (auto& v : voice) v = lowBoost.process (f3.process (f2.process (f1.process (v))));

    // Sibilance: band-passed noise.
    vox::Biquad h1, h2, l1;
    vox::design::apply (h1, vox::design::butterworth (true, 5000.0, sr));
    vox::design::apply (h2, vox::design::butterworth (true, 5000.0, sr));
    vox::design::apply (l1, vox::design::butterworth (false, 9000.0, sr));
    for (auto& v : sib) v = l1.process (h2.process (h1.process (white (rng))));

    // Words: 250 ms on, 120 ms off; every 4 words a 600 ms breath gap; lines alternate loud / quiet.
    std::vector<float> out (n);
    const double word = 0.25, gapW = 0.12;
    double voiceRms = 0.0;
    for (size_t i = 0; i < n; ++i) voiceRms += voice[i] * voice[i];
    voiceRms = std::sqrt (voiceRms / static_cast<double> (n));
    const double vScale = std::pow (10.0, -18.0 / 20.0) / voiceRms;
    double sibRms = 0.0;
    for (double v : sib) sibRms += v * v;
    sibRms = std::sqrt (sibRms / static_cast<double> (n));
    const double sScale = std::pow (10.0, (-18.0 + sibDb) / 20.0) / sibRms;   // sibDb vs the voice
    const double noiseScale = std::pow (10.0, noiseDb / 20.0);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double cycle = 4 * (word + gapW) + 0.6;
        const double inCycle = std::fmod (t, cycle);
        const int line = static_cast<int> (t / cycle);
        double env = 0.0, senv = 0.0;
        if (inCycle < 4 * (word + gapW))
        {
            const double w = std::fmod (inCycle, word + gapW);
            const int wordIndex = static_cast<int> (inCycle / (word + gapW));
            if (w < word)
            {
                env = std::sin (std::numbers::pi * w / word);
                env = std::sqrt (env);
                if ((wordIndex + line) % 2 == 0 && w < 0.07) senv = std::sin (std::numbers::pi * w / 0.07);
            }
        }
        const double lineGain = line % 2 == 0 ? 1.0 : 0.35;   // quiet lines ~9 dB down
        out[i] = static_cast<float> (lineGain * (env * vScale * voice[i] + senv * sScale * sib[i])
                                     + noiseScale * white (rng));
    }
    return out;
}

} // namespace testsig
