#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/ClipWatch.h"
#include "vox/VocalChain.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;
std::vector<double> sungVowel (double seconds)
{
    // A voice-like tone: harmonics with a formant and a little vibrato.
    std::vector<double> x (static_cast<size_t> (seconds * kSr));
    Biquad f; design::apply (f, design::bell (800.0, 9.0, 2.0, kSr));
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        ph += 180.0 * (1.0 + 0.004 * std::sin (2 * std::numbers::pi * 5.0 * i / kSr)) / kSr;
        double s = 0.0;
        for (int h = 1; h * 180.0 < 9000.0; ++h) s += std::sin (2 * std::numbers::pi * h * ph + 0.3 * h) / h;
        x[i] = 0.2 * f.process (s);
    }
    return x;
}
ClipWatch::Counts watch (std::vector<double> x)
{
    ClipWatch w; w.prepare (1);
    ClipWatch::Counts total;
    for (size_t s = 0; s < x.size(); s += 256)
    {
        const double* p = x.data() + s;
        w.process (&p, 1, static_cast<int> (std::min<size_t> (256, x.size() - s)));
        const auto c = w.take();
        total.runs += c.runs; total.overs += c.overs;
    }
    return total;
}
int flatStretches (const std::vector<double>& x, double lvl)
{
    int n = 0, run = 0;
    for (double v : x) { if (std::abs (v) >= lvl) { if (++run == 3) ++n; } else run = 0; }
    return n;
}
}

TEST_CASE ("Clip watch: finds clipping at any level, never on clean audio; counts overs", "[clipwatch]")
{
    const auto clean = sungVowel (1.0);
    double peak = 0.0; for (double v : clean) peak = std::max (peak, std::abs (v));
    CHECK (watch (clean).runs == 0);
    CHECK (watch (clean).overs == 0);

    // Clipped below full scale (like a brick wall at -1.05 dB), quiet and loud: every flat stretch found.
    for (double frac : { 0.9, 0.7 })
    {
        auto c = clean;
        const double lvl = frac * peak;
        for (auto& v : c) v = std::clamp (v, -lvl, lvl);
        const int truth = flatStretches (c, lvl);
        REQUIRE (truth > 20);
        CHECK (watch (c).runs == truth);
    }

    // Digital silence and a quiet steady value are not clipping.
    std::vector<double> quiet (20000, 0.0);
    for (size_t i = 5000; i < 15000; ++i) quiet[i] = 0.004;
    CHECK (watch (quiet).runs == 0);

    // Too hot: a float mix 6 dB over full scale.
    auto hot = clean;
    for (auto& v : hot) v *= 2.0 / peak;
    const auto h = watch (hot);
    CHECK (h.overs > 1000);
    CHECK (h.runs == 0);
}

TEST_CASE ("Clip watch in the chain and Auto-Edit: no latency, audio untouched, clipping reported", "[clipwatch]")
{
    VocalChain chain;
    chain.prepare (kSr, 1);
    PitchCorrector p; p.prepare (kSr, 1);
    CHECK (chain.latencySamples() == p.latencySamples() + Saturation::kLatency);

    const auto clean = sungVowel (2.0);
    double peak = 0.0; for (double v : clean) peak = std::max (peak, std::abs (v));
    std::vector<float> f (clean.size());
    const double lvl = 0.8861353;   // -1.05 dB: what the user's Stereo Out limiter left
    for (size_t i = 0; i < f.size(); ++i) f[i] = static_cast<float> (std::clamp (clean[i] * 1.4 / peak, -lvl, lvl));

    std::vector<float> buf = f;
    float* ptr = buf.data();
    int runs = 0;
    for (size_t s = 0; s + 512 <= buf.size(); s += 512)
    {
        float* q = ptr + s;
        chain.process (&q, 1, 512);
        runs += chain.takeMeters().input.runs;
    }
    CHECK (runs > 50);

    const auto a = analyseVocal ({ f }, kSr);
    CHECK (a.clippedRuns > 50);   // Auto-Edit sees clipping below the very top too
}
