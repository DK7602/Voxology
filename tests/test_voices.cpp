#include "Signals.h"
#include "vox/Fft.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

/** A sung A3 (220 Hz, vowel formants) in 1.2 s notes with 0.8 s gaps, over -80 dB room noise. */
std::vector<float> sungA (double seconds)
{
    std::vector<float> x (static_cast<size_t> (seconds * kSr));
    Biquad f1, f2;
    design::apply (f1, design::bell (700.0, 12.0, 3.0, kSr));
    design::apply (f2, design::bell (1800.0, 10.0, 3.0, kSr));
    std::mt19937 rng (9);
    std::normal_distribution<double> w (0.0, 1.0e-4);
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = std::fmod (static_cast<double> (i) / kSr, 2.0);
        const double env = t < 1.2 ? std::min ({ 1.0, t / 0.01, (1.2 - t) / 0.03 }) : 0.0;
        ph += 220.0 / kSr;
        double s = 0.0;
        for (int h = 1; h < 40; ++h) s += std::sin (2.0 * std::numbers::pi * h * ph) / h;
        x[i] = static_cast<float> (0.08 * env * f2.process (f1.process (s)) + w (rng));
    }
    return x;
}

double pitchHz (const std::vector<double>& x, size_t a)
{
    const size_t N = 65536;
    std::vector<double> w (N);
    for (size_t i = 0; i < N; ++i) w[i] = (a + i < x.size() ? x[a + i] : 0.0) * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / N));
    const auto p = powerSpectrum (w.data(), N);
    const double bin = kSr / N;
    double best = -1.0e300, bf = 0.0;
    for (double f = 60.0; f < 1000.0; f += 0.25)
    {
        double h = 0.0;
        for (int k = 1; k <= 5; ++k) { const auto b = static_cast<size_t> (std::lround (k * f / bin)); h += std::log (p[b - 1] + p[b] + p[b + 1] + 1.0e-30); }
        if (h > best) { best = h; bf = f; }
    }
    return bf;
}

double rmsDb (const std::vector<double>& x, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += x[i] * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1.0e-30);
}

/** The voices alone: the chain with them minus the chain without (the lead path is identical). */
std::vector<double> voicesOnly (const std::vector<float>& x, ChainParams with)
{
    ChainParams without = with;
    without.voices.interval = { 0, 0 };
    const auto a = VocalChain::render ({ x }, kSr, with), b = VocalChain::render ({ x }, kSr, without);
    std::vector<double> d (a[0].size());
    const size_t nch = a.size();
    for (size_t i = 0; i < d.size(); ++i)
    {
        double s = 0.0;
        for (size_t c = 0; c < nch; ++c) s += a[c][i] - b[c][i];
        d[i] = s / static_cast<double> (nch);
    }
    return d;
}

std::vector<double> mono (const std::vector<std::vector<float>>& y)
{
    std::vector<double> m (y[0].size());
    for (size_t i = 0; i < m.size(); ++i)
    {
        double s = 0.0;
        for (const auto& c : y) s += c[i];
        m[i] = s / static_cast<double> (y.size());
    }
    return m;
}

ChainParams harmonyIn (int a, int b)
{
    ChainParams p;
    p.pitch.key = 0;      // C major
    p.pitch.scale = 1;
    p.voices.interval = { a, b };
    p.voices.level = 50.0;
    return p;
}
}

TEST_CASE ("Voices: harmonies land on the right notes, in key", "[voices]")
{
    const auto x = sungA (6.0);
    struct Case { int interval; double hz; const char* name; };
    for (const auto& c : { Case { 1, 261.63, "3rd up: C4" }, Case { 2, 329.63, "5th up: E4" }, Case { 4, 174.61, "3rd down: F3" },
                           Case { 6, 146.83, "5th down: D3" }, Case { 7, 110.0, "octave down: A2" } })
    {
        INFO (c.name);
        const auto v = voicesOnly (x, harmonyIn (c.interval, 0));
        CHECK (pitchHz (v, static_cast<size_t> (2.1 * kSr)) == Approx (c.hz).epsilon (0.01));
    }
}

TEST_CASE ("Voices: level follows the lead, start in time with it, and stay out of the gaps", "[voices]")
{
    const auto x = sungA (8.0);
    auto p = harmonyIn (1, 6);   // 3rd up + 5th down
    const auto v = voicesOnly (x, p);
    ChainParams leadOnly = p;
    leadOnly.voices.interval = { 0, 0 };
    const auto lead = mono (VocalChain::render ({ x }, kSr, leadOnly));
    for (int note = 1; note < 4; ++note)
    {
        const auto a = static_cast<size_t> ((note * 2.0 + 0.3) * kSr), b = static_cast<size_t> ((note * 2.0 + 1.0) * kSr);
        INFO ("note " << note);
        // Two voices at 50 % each, panned apart: together about 7 dB under the lead (in mono).
        CHECK (rmsDb (v, a, b) - rmsDb (lead, a, b) == Approx (-7.5).margin (2.5));   // a backing level (summed to mono)
        // In the gap after the note (from 150 ms after it ends): nothing (no room noise pumped up).
        const auto g0 = static_cast<size_t> ((note * 2.0 + 1.35) * kSr), g1 = static_cast<size_t> ((note * 2.0 + 1.95) * kSr);
        CHECK (rmsDb (v, g0, g1) < -70.0);
        // Onset: the voices reach half their level within 15 ms of the lead doing so.
        auto onset = [&] (const std::vector<double>& s)
        {
            const auto n0 = static_cast<size_t> (note * 2.0 * kSr) - 2400;
            const double ref = std::pow (10.0, rmsDb (s, a, b) / 20.0) * 0.5;
            double e = 0.0;
            for (size_t i = n0; i < n0 + 9600; ++i)
            {
                e += (s[i] * s[i] - e) * 0.02;
                if (std::sqrt (e) > ref) return static_cast<double> (i - n0) / kSr;
            }
            return 1.0;
        };
        CHECK (std::abs (onset (v) - onset (lead)) < 0.015);
    }
}

TEST_CASE ("Formant: the lead's tone moves, its note doesn't", "[voices]")
{
    const auto x = sungA (4.0);
    ChainParams up, plain;
    up.pitch.formant = 4.0;
    const auto a = mono (VocalChain::render ({ x }, kSr, plain)), b = mono (VocalChain::render ({ x }, kSr, up));
    CHECK (pitchHz (b, static_cast<size_t> (0.1 * kSr)) == Approx (220.0).epsilon (0.01));
    auto centroid = [] (const std::vector<double>& s)
    {
        const auto p = powerSpectrum (s.data() + static_cast<size_t> (0.3 * kSr), 4096);
        double num = 0.0, den = 0.0;
        for (size_t k = 1; k < p.size(); ++k) { const double f = static_cast<double> (k) * kSr / 4096.0; if (f > 6000.0) break; num += f * p[k]; den += p[k]; }
        return num / den;
    };
    CHECK (centroid (b) > 1.15 * centroid (a));
    const auto out = VocalChain::render ({ x }, kSr, up);
    for (float v : out[0]) REQUIRE (std::isfinite (v));
}
