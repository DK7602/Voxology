#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/PopBreath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

/** A sung line with the things these tools are for. Every 1.5 s: a 700 ms vowel (f0 110 Hz, so the
    voice has real low end of its own), then a 300 ms breath (airy noise, 1.5 - 4 kHz, breathDb under
    the voice), then a 150 ms "s" (5 - 9 kHz noise) right before the next word. Every other word
    starts with a "p": a 60 Hz thump (40 ms, popDb over the voice). Returns where each part is. */
struct Line
{
    std::vector<float> x;
    std::vector<std::pair<size_t, size_t>> vowels, breaths, esses, pops;
};

Line line (double seconds, double breathDb = -16.0, double popDb = 6.0, bool withPops = true, bool withBreaths = true)
{
    Line l;
    const size_t n = static_cast<size_t> (seconds * kSr);
    l.x.assign (n, 0.0f);
    std::mt19937 rng (11);
    std::normal_distribution<double> white (0.0, 1.0);
    Biquad a1, a2, s1, s2;
    design::apply (a1, design::butterworth (true, 1500.0, kSr));
    design::apply (a2, design::butterworth (false, 4000.0, kSr));
    design::apply (s1, design::butterworth (true, 5000.0, kSr));
    design::apply (s2, design::butterworth (false, 9000.0, kSr));
    const double voiceAmp = std::pow (10.0, -18.0 / 20.0) * std::sqrt (2.0) / 1.6;   // ~ -18 dB RMS
    const double airAmp = std::pow (10.0, (-18.0 + breathDb) / 20.0) * 2.4;
    const double sAmp = std::pow (10.0, (-18.0 - 6.0) / 20.0) * 2.4;
    double phase = 0.0;
    int word = 0;
    for (double t0 = 0.0; t0 + 1.5 <= seconds; t0 += 1.5, ++word)
    {
        const auto v0 = static_cast<size_t> (t0 * kSr), v1 = v0 + static_cast<size_t> (0.7 * kSr);
        const auto b0 = v1 + static_cast<size_t> (0.15 * kSr), b1 = b0 + static_cast<size_t> (0.3 * kSr);
        const auto e0 = b1 + static_cast<size_t> (0.15 * kSr), e1 = e0 + static_cast<size_t> (0.15 * kSr);
        l.vowels.push_back ({ v0, v1 });
        for (size_t i = v0; i < v1; ++i)
        {
            const double t = static_cast<double> (i - v0) / kSr;
            const double env = std::min ({ 1.0, t / 0.02, (0.7 - t) / 0.02 });
            phase += 110.0 / kSr;
            double s = 0.0;
            for (int h = 1; h < 40; ++h) s += std::sin (2.0 * std::numbers::pi * h * phase) / h;
            l.x[i] += static_cast<float> (env * voiceAmp * s);
        }
        if (withPops && word % 2 == 1)
        {
            const auto p1 = v0 + static_cast<size_t> (0.04 * kSr);
            l.pops.push_back ({ v0, p1 });
            const double amp = voiceAmp * std::pow (10.0, popDb / 20.0) * 1.6;
            for (size_t i = v0; i < p1 + static_cast<size_t> (0.04 * kSr); ++i)
            {
                const double t = static_cast<double> (i - v0) / kSr;
                l.x[i] += static_cast<float> (amp * std::exp (-t / 0.015) * std::sin (2.0 * std::numbers::pi * 60.0 * t));
            }
        }
        if (withBreaths)
        {
            l.breaths.push_back ({ b0, b1 });
            for (size_t i = b0; i < b1; ++i)
            {
                const double t = static_cast<double> (i - b0) / kSr;
                const double env = std::sin (std::numbers::pi * t / 0.3);
                l.x[i] += static_cast<float> (env * airAmp * a2.process (a1.process (white (rng))));
            }
        }
        l.esses.push_back ({ e0, e1 });
        for (size_t i = e0; i < e1; ++i)
        {
            const double t = static_cast<double> (i - e0) / kSr;
            const double env = std::sin (std::numbers::pi * t / 0.15);
            l.x[i] += static_cast<float> (env * sAmp * s2.process (s1.process (white (rng))));
        }
    }
    for (auto& v : l.x) v += static_cast<float> (1.0e-4 * white (rng));   // -80 dB room
    return l;
}

template <typename M, typename P>
std::vector<double> run (M& m, const P& p, const std::vector<float>& in, int block = 512)
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

/** As in the chain: the module processes the audio delayed by kLa and listens to the undelayed input
    (side-chain), so it can act ahead. Returns the output re-aligned with the input. */
constexpr int kLa = 1536;   // 32 ms, like Pitch's latency
template <typename M, typename P>
std::vector<double> runAhead (M& m, const P& p, const std::vector<float>& in, int block = 512)
{
    m.prepare (kSr, 1, kLa);
    m.setParams (p);
    const size_t n = in.size();
    std::vector<double> sc (n + kLa, 0.0), x (n + kLa, 0.0);
    for (size_t i = 0; i < n; ++i) { sc[i] = in[i]; x[i + kLa] = in[i]; }
    for (size_t s = 0; s < x.size(); s += static_cast<size_t> (block))
    {
        double* ptr = x.data() + s;
        m.process (&ptr, 1, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), x.size() - s)), sc.data() + s);
    }
    return std::vector<double> (x.begin() + kLa, x.end());
}

double rmsDb (const std::vector<double>& x, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += x[i] * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1.0e-30);
}
double rmsDb (const std::vector<float>& x, size_t a, size_t b) { return rmsDb (std::vector<double> (x.begin() + static_cast<long> (a), x.begin() + static_cast<long> (b)), 0, b - a); }

/** Energy under 150 Hz (thump) in a span. */
double lowDb (const std::vector<double>& x, size_t a, size_t b)
{
    Biquad l1, l2;
    design::apply (l1, design::butterworth (false, 150.0, kSr));
    design::apply (l2, design::butterworth (false, 150.0, kSr));
    double e = 0.0;
    for (size_t i = a > 4800 ? a - 4800 : 0; i < b; ++i)
    {
        const double y = l2.process (l1.process (x[i]));
        if (i >= a) e += y * y;
    }
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1.0e-30);
}
}

TEST_CASE ("Pops and breaths are exact pass-throughs when off", "[popbreath]")
{
    const auto l = line (6.0);
    PopRemover p;
    BreathControl b;
    const auto y1 = run (p, PopParams {}, l.x);
    const auto y2 = run (b, BreathParams {}, l.x);
    for (size_t i = 0; i < l.x.size(); ++i)
    {
        REQUIRE (y1[i] == static_cast<double> (l.x[i]));
        REQUIRE (y2[i] == static_cast<double> (l.x[i]));
    }
}

TEST_CASE ("Plosive remover cuts the thump and leaves the voice", "[popbreath]")
{
    const auto l = line (12.0);
    PopRemover m;
    const auto y = runAhead (m, PopParams { 100.0 }, l.x);
    const std::vector<double> x (l.x.begin(), l.x.end());
    int checked = 0;
    for (const auto& [a, b] : l.pops)
    {
        if (a < static_cast<size_t> (2.0 * kSr)) continue;   // after it has heard the voice
        INFO ("pop at " << static_cast<double> (a) / kSr << " s");
        CHECK (lowDb (y, a, b) < lowDb (x, a, b) - 8.0);
        ++checked;
    }
    CHECK (checked >= 2);
    // Vowels without a pop (the voice's own 110 Hz low end) keep their level, low end included.
    for (size_t w = 2; w < l.vowels.size(); w += 2)
    {
        const auto [a, b] = l.vowels[w];
        const size_t mid = a + (b - a) / 4;
        CHECK (rmsDb (y, mid, b) == Approx (rmsDb (x, mid, b)).margin (0.2));
        CHECK (lowDb (y, mid, b) == Approx (lowDb (x, mid, b)).margin (0.3));
    }
}

TEST_CASE ("Breath control turns breaths down and leaves words and s sounds alone", "[popbreath]")
{
    const auto l = line (12.0, -16.0, 6.0, false);
    BreathControl m;
    const auto y = runAhead (m, BreathParams { 10.0 }, l.x);
    const std::vector<double> x (l.x.begin(), l.x.end());
    for (size_t k = 2; k < l.breaths.size(); ++k)
    {
        const auto [a, b] = l.breaths[k];
        const size_t from = a + static_cast<size_t> (0.15 * kSr), to = b - static_cast<size_t> (0.04 * kSr);   // after it has recognised the breath
        INFO ("breath at " << static_cast<double> (a) / kSr << " s");
        CHECK (rmsDb (y, from, to) == Approx (rmsDb (x, from, to) - 10.0).margin (1.5));
    }
    for (size_t k = 2; k < l.vowels.size(); ++k)
    {
        const auto [a, b] = l.vowels[k];
        CHECK (rmsDb (y, a, b) == Approx (rmsDb (x, a, b)).margin (0.05));
    }
    for (size_t k = 2; k < l.esses.size(); ++k)
    {
        const auto [a, b] = l.esses[k];
        CHECK (rmsDb (y, a, b) == Approx (rmsDb (x, a, b)).margin (0.5));
    }
}

TEST_CASE ("Pops and breaths don't depend on block size", "[popbreath]")
{
    const auto l = line (6.0);
    PopRemover p1, p2;
    BreathControl b1, b2;
    const auto a = runAhead (p1, PopParams { 80.0 }, l.x, 512), b = runAhead (p2, PopParams { 80.0 }, l.x, 37);
    const auto c = runAhead (b1, BreathParams { 12.0 }, l.x, 512), d = runAhead (b2, BreathParams { 12.0 }, l.x, 37);
    for (size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE (a[i] == Approx (b[i]).margin (1.0e-12));
        REQUIRE (c[i] == Approx (d[i]).margin (1.0e-12));
        REQUIRE (std::isfinite (a[i]));
    }
}

TEST_CASE ("Auto-Edit finds pops and breaths, and leaves them off on a clean take", "[popbreath][autoedit]")
{
    const auto dirty = autoEdit ({ line (16.0).x }, kSr, { 1, 1, 0.0 });
    REQUIRE (dirty.ok);
    CHECK (dirty.params.cleanup.popAmount > 0.0);
    CHECK (dirty.params.cleanup.breathDb > 0.0);

    const auto clean = autoEdit ({ line (16.0, -16.0, 6.0, false, false).x }, kSr, { 1, 1, 0.0 });
    REQUIRE (clean.ok);
    CHECK (clean.params.cleanup.popAmount == 60.0);   // safety net: only acts on a real pop
    CHECK (dirty.params.cleanup.popAmount >= clean.params.cleanup.popAmount);
    CHECK (clean.params.cleanup.breathDb == 0.0);
}

TEST_CASE ("In the full chain, pops are cut from their first moment (look-ahead from Pitch's latency)", "[popbreath][chain]")
{
    const auto l = line (12.0);
    ChainParams off, on;
    on.cleanup.popAmount = 100.0;
    on.cleanup.breathDb = 10.0;
    const auto a = VocalChain::render ({ l.x }, kSr, off), b = VocalChain::render ({ l.x }, kSr, on);
    const std::vector<double> x (a[0].begin(), a[0].end()), y (b[0].begin(), b[0].end());
    for (const auto& [p0, p1] : l.pops)
    {
        if (p0 < static_cast<size_t> (2.0 * kSr)) continue;
        INFO ("pop at " << static_cast<double> (p0) / kSr << " s");
        CHECK (lowDb (y, p0, p1) < lowDb (x, p0, p1) - 12.0);
        // Once the thump has died away the vowel gets its low end back quickly (the cut doesn't hang on).
        const auto t0 = p0 + static_cast<size_t> (0.10 * kSr), t1 = p0 + static_cast<size_t> (0.20 * kSr);
        INFO ("vowel after it: " << lowDb (y, t0, t1) - lowDb (x, t0, t1) << " dB");
        CHECK (lowDb (y, t0, t1) > lowDb (x, t0, t1) - 1.0);
    }
    for (size_t k = 2; k < l.breaths.size(); ++k)
    {
        const auto [b0, b1] = l.breaths[k];
        CHECK (rmsDb (y, b0 + 7200, b1 - 1920) < rmsDb (x, b0 + 7200, b1 - 1920) - 7.0);
        // The next word starts at full level (breath control let go before it arrived).
        const auto [v0, v1] = l.vowels[std::min (k + 1, l.vowels.size() - 1)];
        if (k + 1 < l.vowels.size() && (k + 1) % 2 == 0)
            CHECK (rmsDb (y, v0, v0 + 960) == Approx (rmsDb (x, v0, v0 + 960)).margin (0.2));
    }
}
