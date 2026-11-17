// The Output's PEAKS (Beta 25): Off leaves the vocal alone, Clip and Limit never let a sample past the Ceiling, Clip
// leaves everything under its knee alone, Limit comes back up after a loud moment, and A / bypass never get either.
#include "Signals.h"
#include "vox/OutputPeaks.h"
#include "vox/VocalChain.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

double db (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

std::vector<std::vector<double>> chainRun (const ChainParams& p, const std::vector<float>& mono)
{
    VocalChain c;
    c.setParams (p);
    c.prepare (kSr, 2);
    std::vector<std::vector<double>> out (2, std::vector<double> (mono.begin(), mono.end()));
    const size_t block = 512;
    for (size_t s = 0; s < mono.size(); s += block)
    {
        std::array<double*, 2> ptr { out[0].data() + s, out[1].data() + s };
        c.process (ptr.data(), 2, static_cast<int> (std::min (block, mono.size() - s)));
    }
    return out;
}

double peakOf (const std::vector<std::vector<double>>& o)
{
    double p = 0.0;
    for (const auto& ch : o)
        for (double v : ch) p = std::max (p, std::abs (v));
    return p;
}
}

TEST_CASE ("Output Peaks: Clip and Limit keep every sample under the Ceiling", "[peaks]")
{
    const double ceilingDb = -1.0, ceiling = std::pow (10.0, ceilingDb / 20.0);
    for (const auto mode : { PeakMode::clip, PeakMode::limit })
    {
        INFO ((mode == PeakMode::clip ? "Clip" : "Limit"));
        OutputPeaks op;
        op.prepare (kSr);
        op.setParams (mode, ceilingDb);
        double worst = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            // a 220 Hz tone at +6 dBFS (both channels, the right a little lower)
            const double x = 2.0 * std::sin (2.0 * M_PI * 220.0 * i / kSr);
            double f[2] { x, 0.8 * x };
            op.process (f, 2);
            worst = std::max ({ worst, std::abs (f[0]), std::abs (f[1]) });
        }
        CHECK (worst <= ceiling + 1.0e-12);
        CHECK (worst > ceiling * 0.9);   // and it doesn't simply go quiet
        CHECK (op.takeReductionDb() < -3.0);
        CHECK (op.takeReductionDb() == 0.0);   // read once
    }
}

TEST_CASE ("Output Peaks: Off is untouched, Clip leaves what sits under its knee", "[peaks]")
{
    OutputPeaks off, clip;
    off.prepare (kSr);
    clip.prepare (kSr);
    off.setParams (PeakMode::off, -1.0);
    clip.setParams (PeakMode::clip, -1.0);   // knee 3 dB under: -4 dBFS
    for (int i = 0; i < 4800; ++i)
    {
        const double loud = 3.0 * std::sin (2.0 * M_PI * 440.0 * i / kSr);
        const double soft = 0.5 * std::sin (2.0 * M_PI * 440.0 * i / kSr);   // -6 dBFS peaks
        double a[1] { loud }, b[1] { soft };
        off.process (a, 1);
        clip.process (b, 1);
        REQUIRE (a[0] == loud);
        REQUIRE (b[0] == soft);
    }
    CHECK (clip.takeReductionDb() == 0.0);
}

TEST_CASE ("Output Peaks: Limit comes back up after a loud moment", "[peaks]")
{
    OutputPeaks op;
    op.prepare (kSr);
    op.setParams (PeakMode::limit, -1.0);
    // 0.2 s at +6 dBFS, then a quiet tone (-20 dBFS): within half a second it plays at its own level again.
    for (int i = 0; i < 9600; ++i)
    {
        double f[1] { 2.0 * std::sin (2.0 * M_PI * 220.0 * i / kSr) };
        op.process (f, 1);
    }
    double lastGainDb = -100.0;
    for (int i = 0; i < 48000; ++i)
    {
        const double x = 0.1 * std::sin (2.0 * M_PI * 220.0 * i / kSr);
        double f[1] { x };
        op.process (f, 1);
        if (i > 24000 && std::abs (x) > 0.05) lastGainDb = db (std::abs (f[0] / x));
    }
    CHECK (lastGainDb == Catch::Approx (0.0).margin (0.05));
}

TEST_CASE ("Output Peaks in the chain: after the Output level; A and bypass untouched", "[peaks]")
{
    const auto x = testsig::vocal (kSr, 3.0, -40.0, -6.0);
    ChainParams p;
    p.outputDb = 12.0;   // pushed well past 0 dBFS
    const auto loud = chainRun (p, x);
    REQUIRE (peakOf (loud) > 1.0);
    for (const int mode : { 1, 2 })
    {
        INFO ((mode == 1 ? "Clip" : "Limit"));
        auto q = p;
        q.peakMode = mode;
        q.ceilingDb = -2.0;
        CHECK (peakOf (chainRun (q, x)) <= std::pow (10.0, -2.0 / 20.0) + 1.0e-9);
        // A (the original) and bypass: the vocal as it came in, PEAKS or not.
        for (const bool a : { true, false })
        {
            auto with = q, without = p;
            (a ? with.listenOriginal : with.bypass) = true;
            (a ? without.listenOriginal : without.bypass) = true;
            const auto w = chainRun (with, x), wo = chainRun (without, x);
            for (size_t i = 0; i < x.size(); ++i) REQUIRE (w[0][i] == wo[0][i]);
        }
    }
    // Off: the chain exactly as before.
    auto q = p;
    q.peakMode = 0;
    const auto again = chainRun (q, x);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE (again[1][i] == loud[1][i]);
}
