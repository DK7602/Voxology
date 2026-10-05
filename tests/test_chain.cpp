#include "Signals.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

ChainParams busy()
{
    ChainParams p;
    p.cleanup.lowCutHz = 90; p.cleanup.gateThrDb = -50; p.cleanup.gateRangeDb = 12;
    p.eq.gainDb = { -2, -3, -1.5, 3, 4 };
    p.deEsser.amount = 60;
    p.rider.rangeDb = 4;
    p.comp.peakThrDb = -14; p.comp.thrDb = -26; p.comp.ratio = 4; p.comp.makeupDb = 5;
    p.saturation.driveDb = 6; p.saturation.mix = 50;
    p.doubler.amount = 30;
    p.delay.mix = 15;
    p.reverb.mix = 15;
    p.outputDb = -2;
    return p;
}

std::vector<std::vector<double>> process (const ChainParams& p, const std::vector<float>& mono, int channels, int block)
{
    VocalChain c;
    c.setParams (p);
    c.prepare (kSr, channels);
    std::vector<std::vector<double>> x (static_cast<size_t> (channels), std::vector<double> (mono.begin(), mono.end()));
    for (size_t s = 0; s < mono.size(); s += static_cast<size_t> (block))
    {
        std::array<double*, 2> ptr {};
        for (int ch = 0; ch < channels; ++ch) ptr[static_cast<size_t> (ch)] = x[static_cast<size_t> (ch)].data() + s;
        c.process (ptr.data(), channels, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), mono.size() - s)));
    }
    return x;
}
}

TEST_CASE ("Neutral chain = input delayed by the latency", "[chain]")
{
    ChainParams p;
    const auto x = testsig::vocal (kSr, 1.0);
    const auto y = process (p, x, 2, 480);
    double err = 0.0;
    for (size_t i = VocalChain::kLatency; i < x.size(); ++i)
        err = std::max (err, std::abs (y[0][i] - x[i - VocalChain::kLatency]));
    CHECK (err < 1.0e-9);
}

TEST_CASE ("Bypass and A (original) play the delayed input", "[chain]")
{
    auto p = busy();
    p.bypass = true;
    const auto x = testsig::vocal (kSr, 1.0);
    const auto y = process (p, x, 2, 333);
    double err = 0.0;
    for (size_t i = VocalChain::kLatency; i < x.size(); ++i)
        err = std::max ({ err, std::abs (y[0][i] - x[i - VocalChain::kLatency]), std::abs (y[1][i] - x[i - VocalChain::kLatency]) });
    CHECK (err < 1.0e-9);

    p.bypass = false; p.listenOriginal = true; p.gainOriginalDb = -6.0;
    VocalChain c; c.setParams (p); c.prepare (kSr, 1);
    std::vector<double> z (x.begin(), x.end());
    double* ptr = z.data();
    c.process (&ptr, 1, static_cast<int> (z.size()));
    CHECK (z[30000] == Approx (x[30000 - VocalChain::kLatency] * std::pow (10.0, -6.0 / 20.0)).margin (1e-9));
}

TEST_CASE ("Any block size gives the same result", "[chain]")
{
    const auto x = testsig::vocal (kSr, 1.5, -60.0, -3.0);
    const auto a = process (busy(), x, 2, 512);
    for (int block : { 1, 17, 64, 1000, 4096 })
    {
        const auto b = process (busy(), x, 2, block);
        double err = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < x.size(); ++i) err = std::max (err, std::abs (a[c][i] - b[c][i]));
        INFO ("block " << block);
        CHECK (err < 1.0e-9);
    }
}

TEST_CASE ("Busy chain stays finite on silence, a full-scale burst and a mono track", "[chain]")
{
    std::vector<float> x (48000, 0.0f);
    for (int i = 10000; i < 12000; ++i) x[static_cast<size_t> (i)] = (i % 2) ? 1.0f : -1.0f;
    for (int ch : { 1, 2 })
    {
        const auto y = process (busy(), x, ch, 256);
        for (const auto& c : y) for (double v : c) REQUIRE (std::isfinite (v));
    }
}
