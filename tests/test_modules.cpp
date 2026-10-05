#include "Signals.h"
#include "vox/Modules.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

template <typename Module, typename Params>
std::vector<double> run (Module& m, const Params& p, const std::vector<float>& in, int block = 512)
{
    m.prepare (kSr, 1);
    m.setParams (p);
    std::vector<double> x (in.begin(), in.end());
    for (size_t s = 0; s < x.size(); s += static_cast<size_t> (block))
    {
        double* ptr = x.data() + s;
        m.process (&ptr, 1, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), x.size() - s)));
    }
    return x;
}

double rmsDb (const std::vector<double>& x, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from; i < to; ++i) e += x[i] * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (to - from) + 1.0e-30);
}

bool identical (const std::vector<double>& y, const std::vector<float>& x)
{
    for (size_t i = 0; i < x.size(); ++i) if (y[i] != static_cast<double> (x[i])) return false;
    return true;
}
}

TEST_CASE ("Every insert module is an exact pass-through at its neutral settings", "[modules]")
{
    const auto x = testsig::vocal (kSr, 2.0, -60.0, -2.0);
    { Cleanup m; CHECK (identical (run (m, CleanupParams {}, x), x)); }
    { VocalEQ m; CHECK (identical (run (m, EqParams {}, x), x)); }
    { DeEsser m; CHECK (identical (run (m, DeEsserParams {}, x), x)); }
    { Rider m; CHECK (identical (run (m, RiderParams {}, x), x)); }
    { VocalCompressor m; CHECK (identical (run (m, CompParams {}, x), x)); }
    // Switched off with strong settings is also untouched.
    { Cleanup m; CleanupParams p; p.enabled = false; p.lowCutHz = 200; p.gateRangeDb = 20; CHECK (identical (run (m, p, x), x)); }
    { VocalCompressor m; CompParams p; p.enabled = false; p.thrDb = -40; p.ratio = 8; CHECK (identical (run (m, p, x), x)); }
}

TEST_CASE ("Cleanup low cut removes rumble and keeps the voice", "[modules][cleanup]")
{
    CleanupParams p; p.lowCutHz = 100.0;
    Cleanup a, b;
    const auto low = run (a, p, testsig::sine (kSr, 30.0, -12.0, 1.0));
    const auto mid = run (b, p, testsig::sine (kSr, 1000.0, -12.0, 1.0));
    CHECK (rmsDb (low, 24000, 48000) < -15.0 - 40.0);   // 24 dB/oct: ~42 dB down 1.7 octaves below
    CHECK (rmsDb (mid, 24000, 48000) == Approx (-15.05).margin (0.1));
}

TEST_CASE ("Cleanup gate turns the gaps down by Range and leaves words alone", "[modules][cleanup]")
{
    // 0.5 s tone at -12 dBFS, then 1 s of noise at -60 dBFS RMS.
    auto x = testsig::sine (kSr, 300.0, -12.0, 0.5);
    std::mt19937 rng (1);
    std::normal_distribution<double> w (0.0, 1.0);
    for (int i = 0; i < 48000; ++i) x.push_back (static_cast<float> (0.001 * w (rng)));
    CleanupParams p; p.gateThrDb = -40.0; p.gateRangeDb = 18.0;
    Cleanup g;
    const auto y = run (g, p, x);
    CHECK (rmsDb (y, 4800, 20000) == Approx (testsig::rmsDb (x, 4800, 20000)).margin (0.05));
    CHECK (rmsDb (y, 24000 + 24000, 72000) == Approx (-60.0 - 18.0).margin (1.0));
}

TEST_CASE ("Tone EQ bands land where the response says", "[modules][eq]")
{
    EqParams p;
    p.gainDb = { 0, 0, 0, 6.0, 0 };
    p.freqHz[3] = 4000.0;
    VocalEQ e;
    const auto y = run (e, p, testsig::sine (kSr, 4000.0, -12.0, 1.0));
    CHECK (rmsDb (y, 24000, 48000) - (-15.05) == Approx (6.0).margin (0.1));
    CHECK (VocalEQ::responseDb (p, 4000.0, kSr) == Approx (6.0).margin (0.01));
    CHECK (std::abs (VocalEQ::responseDb (p, 300.0, kSr)) < 0.3);
    // Air shelf: +4 dB well above 12 kHz, ~0 at 1 kHz.
    EqParams q; q.gainDb = { 0, 0, 0, 0, 4.0 };
    CHECK (VocalEQ::responseDb (q, 18000.0, kSr) == Approx (4.0).margin (0.5));
    CHECK (std::abs (VocalEQ::responseDb (q, 1000.0, kSr)) < 0.1);
}

TEST_CASE ("De-Esser cuts the s and leaves the vowel", "[modules][deess]")
{
    DeEsserParams p; p.amount = 100.0; p.freqHz = 6000.0;
    // Vowel: 300 Hz at -12 dBFS. Untouched (detector sees no sibilance).
    DeEsser a;
    const auto vowel = run (a, p, testsig::sine (kSr, 300.0, -12.0, 1.0));
    CHECK (rmsDb (vowel, 4800, 48000) == Approx (-15.05).margin (0.05));
    // Sibilant: 7.5 kHz at -12 dBFS. Cut hard.
    DeEsser b;
    const auto s = run (b, p, testsig::sine (kSr, 7500.0, -12.0, 1.0));
    CHECK (rmsDb (s, 4800, 48000) < -15.05 - 6.0);
}

TEST_CASE ("Rider brings loud and quiet lines closer together", "[modules][rider]")
{
    auto x = testsig::sine (kSr, 300.0, -10.0, 2.0);
    auto q = testsig::sine (kSr, 300.0, -26.0, 2.0);
    x.insert (x.end(), q.begin(), q.end());
    RiderParams p; p.targetDb = -21.0; p.rangeDb = 6.0; p.speed = 2;
    Rider r;
    const auto y = run (r, p, x);
    const double loud = rmsDb (y, 72000, 96000), quiet = rmsDb (y, 168000, 192000);
    CHECK (loud - quiet == Approx (16.0 - 12.0).margin (0.5));   // 16 dB apart -> 4 dB after +-6
}

TEST_CASE ("Compressor gain computers and steady-state reduction", "[modules][comp]")
{
    CHECK (VocalCompressor::peakGrDb (-30.0, -20.0) == 0.0);
    CHECK (VocalCompressor::peakGrDb (-8.0, -20.0) == Approx (-10.0));   // 12 over at 6:1 -> 2 over
    CHECK (VocalCompressor::levelGrDb (-10.0, -20.0, 4.0) == Approx (-7.5));
    CHECK (VocalCompressor::levelGrDb (-10.0, -20.0, 1.0) == 0.0);

    CompParams p; p.thrDb = -24.0; p.ratio = 4.0;
    VocalCompressor c;
    const auto y = run (c, p, testsig::sine (kSr, 1000.0, -12.0, 2.0));
    // Sine peak -12 dBFS, 12 dB over at 4:1 -> 9 dB down.
    CHECK (rmsDb (y, 72000, 96000) - (-15.05) == Approx (-9.0).margin (0.3));
}

TEST_CASE ("De-Esser: no click when an s starts (the cut must start from the current amount)", "[modules][deess]")
{
    // A vowel with "s" bursts: the jump in the output at each burst onset must not be bigger
    // than the input's own (a stale deep-cut filter used to switch in for a few samples: a click).
    std::vector<float> x (static_cast<size_t> (kSr * 2.0));
    std::mt19937 rng (5);
    std::normal_distribution<double> w (0.0, 1.0);
    Biquad hp; design::apply (hp, design::butterworth (true, 6000.0, kSr));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        const bool s = std::fmod (t, 0.25) < 0.06;
        x[i] = static_cast<float> (0.2 * std::sin (2 * std::numbers::pi * 220.0 * t) + (s ? 0.25 * hp.process (w (rng)) : hp.process (0.0)));
    }
    DeEsserParams p; p.amount = 100.0; p.sensitivity = 70.0; p.freqHz = 6000.0;
    for (int block : { 64, 512 })
    {
        DeEsser d;
        const auto y = run (d, p, x, block);
        // Compare the high-passed signals: any switch-in click shows up as a spike above 12 kHz.
        Biquad a, b; const auto c = design::butterworth (true, 12000.0, kSr); design::apply (a, c); design::apply (b, c);
        Biquad a2, b2; design::apply (a2, c); design::apply (b2, c);
        double peakIn = 0, peakOut = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            peakIn = std::max (peakIn, std::abs (b.process (a.process (x[i]))));
            peakOut = std::max (peakOut, std::abs (b2.process (a2.process (y[i]))));
        }
        INFO ("block " << block << " peak above 12k in " << peakIn << " out " << peakOut);
        CHECK (peakOut <= peakIn * 1.05);
    }
}
