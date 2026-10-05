#include "Signals.h"
#include "vox/PitchCorrector.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

std::vector<double> run (const PitchParams& p, const std::vector<double>& x, int block = 256, int* latency = nullptr)
{
    PitchCorrector pc;
    pc.setParams (p);
    pc.prepare (kSr, 1);
    if (latency) *latency = pc.latencySamples();
    std::vector<double> y = x;
    for (size_t s = 0; s < y.size(); s += static_cast<size_t> (block))
    {
        double* ptr = y.data() + s;
        pc.process (&ptr, 1, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), y.size() - s)));
    }
    return y;
}

/** A buzzy voice-like tone (harmonics with a formant) at f0 Hz (constant or a glide). */
std::vector<double> voice (double f0a, double f0b, double seconds, double vibratoCents = 0.0)
{
    const size_t n = static_cast<size_t> (kSr * seconds);
    std::vector<double> x (n);
    double ph = 0.0;
    Biquad formant; design::apply (formant, design::bell (700.0, 9.0, 2.0, kSr));
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        const double f = f0a * std::pow (f0b / f0a, t / seconds) * std::pow (2.0, vibratoCents / 1200.0 * std::sin (2 * std::numbers::pi * 5.5 * t));
        ph += f / kSr;
        double s = 0.0;
        for (int h = 1; h * f < 8000.0; ++h) s += std::sin (2 * std::numbers::pi * h * ph) / h;
        x[i] = 0.1 * formant.process (s);
    }
    return x;
}

/** f0 of x[from, from+len) by normalised autocorrelation with parabolic interpolation. */
double measureHz (const std::vector<double>& x, size_t from, size_t len = 4800)
{
    const int minLag = static_cast<int> (kSr / 1000), maxLag = static_cast<int> (kSr / 70);
    std::vector<double> r (static_cast<size_t> (maxLag + 2), 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = from; i < from + len; ++i) { xy += x[i] * x[i + static_cast<size_t> (lag)]; xx += x[i] * x[i]; yy += x[i + static_cast<size_t> (lag)] * x[i + static_cast<size_t> (lag)]; }
        r[static_cast<size_t> (lag)] = xy / std::sqrt (xx * yy + 1e-30);
    }
    double best = -1; int bl = minLag;
    for (int lag = minLag + 1; lag <= maxLag; ++lag) if (r[static_cast<size_t> (lag)] > best) { best = r[static_cast<size_t> (lag)]; bl = lag; }
    // prefer the shortest lag nearly as good (avoid octave-down)
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (r[static_cast<size_t> (lag)] > 0.97 * best && r[static_cast<size_t> (lag)] >= r[static_cast<size_t> (lag - 1)] && r[static_cast<size_t> (lag)] >= r[static_cast<size_t> (lag + 1)]) { bl = lag; break; }
    const double a = r[static_cast<size_t> (bl - 1)], b = r[static_cast<size_t> (bl)], c = r[static_cast<size_t> (bl + 1)];
    const double d = a - 2 * b + c;
    return kSr / (bl + (std::abs (d) > 1e-12 ? 0.5 * (a - c) / d : 0.0));
}
double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }
double rmsDb (const std::vector<double>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += x[i] * x[i]; return 10 * std::log10 (e / static_cast<double> (b - a) + 1e-30); }
}

TEST_CASE ("Pitch: off / Amount 0 = exact delayed pass-through", "[pitch]")
{
    const auto x = voice (220.0, 230.0, 1.0);
    int lat = 0;
    PitchParams p;   // amount 0
    const auto y = run (p, x, 256, &lat);
    CHECK (lat >= 1500);
    for (size_t i = static_cast<size_t> (lat); i < x.size(); ++i) REQUIRE (y[i] == x[i - static_cast<size_t> (lat)]);
}

TEST_CASE ("Pitch: chromatic, instant retune pulls a sharp note onto the note", "[pitch]")
{
    for (double f : { 110.0 * std::pow (2.0, 0.35 / 12), 233.08 * std::pow (2.0, -0.4 / 12), 440.0 * std::pow (2.0, 0.3 / 12) })
    {
        const auto x = voice (f, f, 1.2);
        PitchParams p; p.amount = 100.0; p.speedMs = 0.0;
        int lat = 0;
        const auto y = run (p, x, 256, &lat);
        const double target = 440.0 * std::pow (2.0, std::round (12.0 * std::log2 (f / 440.0)) / 12.0);
        const double got = measureHz (y, 30000);
        INFO ("in " << f << " Hz, target " << target << ", got " << got);
        CHECK (std::abs (cents (got, target)) < 3.0);
        // Level and tone kept: within 1 dB.
        CHECK (rmsDb (y, 30000, 50000) == Approx (rmsDb (x, 30000 - static_cast<size_t> (lat), 50000 - static_cast<size_t> (lat))).margin (1.0));
    }
}

TEST_CASE ("Pitch: scale snapping and hysteresis", "[pitch]")
{
    // 60.55 = C4 + 55 cents: C# is not in C major, so C (55 cents away) wins over D (145).
    CHECK (PitchCorrector::targetNote (60.55, 0, 1, -1) == 60);
    CHECK (PitchCorrector::targetNote (61.6, 0, 1, -1) == 62);
    // Chromatic: nearest, but the current note is kept until another is 0.3 semitone closer.
    CHECK (PitchCorrector::targetNote (60.6, 0, 0, -1) == 61);
    CHECK (PitchCorrector::targetNote (60.6, 0, 0, 60) == 60);
    CHECK (PitchCorrector::targetNote (60.85, 0, 0, 60) == 61);
    // A minor pentatonic (A C D E G): G#4 (68) goes to G or A.
    const int t = PitchCorrector::targetNote (68.0, 9, 4, -1);
    CHECK ((t == 67 || t == 69));
}

TEST_CASE ("Pitch: slow retune keeps vibrato, fast retune flattens it", "[pitch]")
{
    const auto x = voice (196.0, 196.0, 2.0, 40.0);   // G3 with +-40 cent vibrato
    auto spread = [&] (const std::vector<double>& y)
    {
        double lo = 1e9, hi = -1e9;
        for (size_t s = 40000; s + 2000 < 90000; s += 1200) { const double c = cents (measureHz (y, s, 1200), 196.0); lo = std::min (lo, c); hi = std::max (hi, c); }
        return hi - lo;
    };
    PitchParams fast; fast.amount = 100.0; fast.speedMs = 0.0;
    PitchParams slow; slow.amount = 100.0; slow.speedMs = 400.0;
    const double sIn = spread (x), sFast = spread (run (fast, x)), sSlow = spread (run (slow, x));
    INFO ("vibrato spread in " << sIn << " fast " << sFast << " slow " << sSlow);
    CHECK (sFast < 0.35 * sIn);
    CHECK (sSlow > 0.6 * sIn);
}

TEST_CASE ("Pitch: unvoiced noise passes through, any block size gives the same output", "[pitch]")
{
    std::mt19937 rng (3);
    std::normal_distribution<double> w (0.0, 0.05);
    std::vector<double> x (48000);
    for (auto& v : x) v = w (rng);
    PitchParams p; p.amount = 100.0; p.speedMs = 0.0;
    int lat = 0;
    const auto y = run (p, x, 256, &lat);
    double err = 0, sig = 0;
    for (size_t i = 10000; i < x.size(); ++i) { const double d = y[i] - x[i - static_cast<size_t> (lat)]; err += d * d; sig += x[i] * x[i]; }
    CHECK (10 * std::log10 (err / sig) < -40.0);

    const auto v = voice (180.0, 260.0, 1.0);
    const auto a = run (p, v, 512);
    for (int b : { 1, 37, 4096 })
    {
        const auto c = run (p, v, b);
        double e = 0; for (size_t i = 0; i < v.size(); ++i) e = std::max (e, std::abs (a[i] - c[i]));
        INFO ("block " << b);
        CHECK (e < 1e-9);
    }
}

TEST_CASE ("Pitch: a glide becomes clean steps, no clicks", "[pitch]")
{
    const auto x = voice (196.0, 262.0, 2.0);   // G3 -> C4 slide
    PitchParams p; p.amount = 100.0; p.speedMs = 0.0;   // chromatic hard tune
    const auto y = run (p, x);
    int onNote = 0, total = 0;
    for (size_t s = 6000; s + 3000 < y.size(); s += 1500)
    {
        const double m = 69 + 12 * std::log2 (measureHz (y, s, 1500) / 440.0);
        ++total;
        if (std::abs (m - std::round (m)) < 0.12) ++onNote;
    }
    INFO (onNote << " of " << total << " windows on a note");
    CHECK (onNote >= total * 8 / 10);
    // No clicks: the biggest sample-to-sample jump stays within what the input itself has.
    double jx = 0, jy = 0;
    for (size_t i = 1; i < x.size(); ++i) { jx = std::max (jx, std::abs (x[i] - x[i - 1])); jy = std::max (jy, std::abs (y[i] - y[i - 1])); }
    CHECK (jy < 1.6 * jx);
}

TEST_CASE ("Key detection from sung notes", "[pitch]")
{
    // A minor melody (A C D E G, with some B and F), slightly out of tune.
    std::vector<double> notes;
    const int mel[] = { 57, 60, 62, 64, 62, 60, 57, 55, 57, 64, 67, 64, 62, 60, 59, 57, 65, 64, 62, 60, 57, 57, 64, 60 };
    for (int rep = 0; rep < 6; ++rep)
        for (int n : mel) for (int k = 0; k < 10; ++k) notes.push_back (n + 0.15 * std::sin (rep + k));
    const auto g = detectKey (notes);
    INFO ("key " << kNoteNames[static_cast<size_t> (g.key)] << (g.minor ? " minor" : " major") << " conf " << g.confidence << " off " << g.offCents);
    CHECK ((g.key == 9 && g.minor));
    CHECK (g.offCents < 12.0);
}
