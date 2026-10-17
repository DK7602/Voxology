#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/ClipWatch.h"
#include "vox/VocalChain.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>

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
/** Stretches of 3+ samples at the clip level that end before the audio does (a flat top is judged as it ends). */
int flatStretches (const std::vector<double>& x, double lvl)
{
    int n = 0, run = 0;
    for (double v : x) { if (std::abs (v) >= lvl) ++run; else { if (run >= 3) ++n; run = 0; } }
    return n;
}
/** Peak-normalised to peakDb, then (optionally) hard-clipped at clipDb, then rounded to 16 bits like a 16-bit WAV. */
std::vector<double> as16Bit (const std::vector<double>& x, double peakDb, double clipDb = 0.0)
{
    double peak = 0.0;
    for (double v : x) peak = std::max (peak, std::abs (v));
    const double g = std::pow (10.0, peakDb / 20.0) / peak, ceiling = std::pow (10.0, clipDb / 20.0);
    std::vector<double> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = std::round (std::clamp (std::clamp (x[i] * g, -ceiling, ceiling), -1.0, 32767.0 / 32768.0) * 32768.0) / 32768.0;
    return y;
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

TEST_CASE ("Clip watch: a 16-bit recording's rounded peaks aren't clipping, at any level; real clipping in 16-bit is found", "[clipwatch]")
{
    // A smooth peak in 16-bit audio can sit on one value for 3 samples or more (more often the quieter it is): v0.51
    // read 16-bit vocals as clipped, more at -25 dBFS than at -10. Tones, a sung vowel and a rap voice, 16-bit, from
    // -12 to -30 dBFS: never clipped.
    std::vector<std::pair<const char*, std::vector<double>>> clean;
    for (double hz : { 80.0, 110.0, 200.0, 440.0 })
    {
        std::vector<double> t (static_cast<size_t> (2.0 * kSr));
        for (size_t i = 0; i < t.size(); ++i) t[i] = std::sin (2.0 * std::numbers::pi * hz * static_cast<double> (i) / kSr + 0.1);
        clean.emplace_back ("tone", t);
    }
    clean.emplace_back ("sung vowel", sungVowel (3.0));
    const auto rap = testsig::vocal (kSr, 6.0, -70.0, -10.0);
    clean.emplace_back ("rap voice", std::vector<double> (rap.begin(), rap.end()));
    for (const auto& [what, x] : clean)
        for (double peakDb : { -12.0, -18.0, -24.0, -30.0 })
        {
            INFO (what << " at " << peakDb << " dBFS");
            CHECK (watch (as16Bit (x, peakDb)).runs == 0);
        }

    // Clipped, then stored in 16 bits: at full scale (the interface), and a quiet take clipped by something before it.
    for (const auto& [what, x] : clean)
    {
        if (std::string (what) == "tone") continue;
        for (double clipDb : { 0.0, -20.0 })
        {
            INFO (what << " clipped at " << clipDb << " dBFS");
            const auto c = as16Bit (x, clipDb + 6.0, clipDb);
            const int truth = flatStretches (c, std::pow (10.0, clipDb / 20.0) * 0.999);
            REQUIRE (truth > 20);
            const int found = watch (c).runs;
            CHECK (found > truth / 2);
            CHECK (found <= truth);
        }
    }
}

TEST_CASE ("Clip watch in the chain and Auto-Edit: no latency, audio untouched, clipping reported", "[clipwatch]")
{
    VocalChain chain;
    chain.prepare (kSr, 1);
    PitchCorrector p; p.prepare (kSr, 1);
    CHECK (chain.latencySamples() == Denoiser::frameSize (kSr) + p.latencySamples() + Saturation::kLatency);

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
