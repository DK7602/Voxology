#include "Signals.h"
#include "vox/DeClip.h"
#include "vox/VocalChain.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

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
std::vector<double> run (std::vector<double> x)
{
    DeClip d; d.prepare (1);
    for (size_t s = 0; s < x.size(); s += 256) { double* p = x.data() + s; d.process (&p, 1, static_cast<int> (std::min<size_t> (256, x.size() - s))); }
    x.erase (x.begin(), x.begin() + DeClip::kLookahead);
    return x;
}
}

TEST_CASE ("De-clip: redraws hard-clipped peaks, leaves clean audio exactly as it was", "[declip]")
{
    const auto clean = sungVowel (1.0);
    double peak = 0.0; for (double v : clean) peak = std::max (peak, std::abs (v));
    // Clip at 70 % of the peak (about -3 dB: a typical too-hot recording).
    auto clipped = clean;
    const double lvl = 0.7 * peak;
    int flat = 0;
    for (auto& v : clipped) if (std::abs (v) > lvl) { v = v > 0 ? lvl : -lvl; ++flat; }
    REQUIRE (flat > 500);
    const auto fixedUp = run (clipped);
    // Error vs the true (unclipped) signal, over the clipped samples only.
    double eClip = 0.0, eFix = 0.0;
    for (size_t i = 2000; i + 2000 < fixedUp.size(); ++i)
        if (std::abs (clipped[i]) >= lvl * 0.9999) { eClip += std::pow (clipped[i] - clean[i], 2); eFix += std::pow (fixedUp[i] - clean[i], 2); }
    const double gainDb = 10.0 * std::log10 (eClip / (eFix + 1e-30));
    INFO ("error in the clipped stretches: " << gainDb << " dB smaller after De-clip");
    CHECK (gainDb > 5.0);
    // The top end the clipping added (> 5 kHz, vs the clean voice) mostly goes.
    auto hf = [] (const std::vector<double>& v, const std::vector<double>& ref)
    {
        Biquad h1, h2; design::apply (h1, design::butterworth (true, 5000.0, kSr)); design::apply (h2, design::butterworth (true, 5000.0, kSr));
        double e = 0; for (size_t i = 0; i + 2000 < v.size(); ++i) { const double d = h2.process (h1.process (v[i] - ref[i])); if (i > 2000) e += d * d; }
        return e;
    };
    const double hfClip = hf (clipped, clean), hfFix = hf (fixedUp, clean);
    INFO ("clipping's added top end: " << 10 * std::log10 (hfClip / (hfFix + 1e-30)) << " dB less");
    CHECK (hfFix < 0.6 * hfClip);
    // Clean audio (no flat runs): bit-exact, delayed by the look-ahead.
    const auto same = run (clean);
    for (size_t i = 0; i < same.size(); ++i) REQUIRE (same[i] == clean[i]);
    // Digital silence and quiet steady values are not "clipping".
    std::vector<double> quiet (20000, 0.0);
    for (size_t i = 5000; i < 15000; ++i) quiet[i] = 0.004;
    const auto q = run (quiet);
    for (size_t i = 0; i < q.size(); ++i) REQUIRE (q[i] == quiet[i]);
}

TEST_CASE ("De-clip in the chain: first, off in Record mode, nothing changes on clean audio", "[declip]")
{
    VocalChain full, rec;
    full.prepare (kSr, 1);
    rec.prepare (kSr, 1, true);
    VocalChain plainPitch; plainPitch.prepare (kSr, 1, true);
    CHECK (full.latencySamples() >= DeClip::kLookahead + Saturation::kLatency);
    CHECK (rec.latencySamples() == plainPitch.latencySamples());   // Record mode: no De-clip look-ahead

    const auto clean = sungVowel (0.5);
    std::vector<float> f (clean.begin(), clean.end());
    ChainParams on, off;
    off.cleanup.declip = false;
    const auto a = VocalChain::render ({ f }, kSr, on), b = VocalChain::render ({ f }, kSr, off);
    REQUIRE (a.size() == b.size());
    for (size_t c = 0; c < a.size(); ++c) CHECK (a[c] == b[c]);

    // A clipped one: De-clip changes the result.
    double peak = 0.0; for (float v : f) peak = std::max (peak, static_cast<double> (std::abs (v)));
    for (auto& v : f) v = std::clamp (v, static_cast<float> (-0.7 * peak), static_cast<float> (0.7 * peak));
    const auto c1 = VocalChain::render ({ f }, kSr, on), c2 = VocalChain::render ({ f }, kSr, off);
    CHECK (c1[0] != c2[0]);
}
