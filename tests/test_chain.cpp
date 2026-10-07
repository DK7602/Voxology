#include "Signals.h"
#include "vox/VocalChain.h"

#include <numbers>
#include <random>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

ChainParams busy()
{
    ChainParams p;
    p.pitch.amount = 100; p.pitch.speedMs = 20; p.pitch.scale = 2; p.pitch.key = 9;
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

size_t latency()
{
    VocalChain c;
    c.prepare (kSr, 2);
    return static_cast<size_t> (c.latencySamples());
}

const size_t kLat = latency();

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
    for (size_t i = kLat; i < x.size(); ++i)
        err = std::max (err, std::abs (y[0][i] - x[i - kLat]));
    CHECK (err < 1.0e-9);
}

TEST_CASE ("Bypass and A (original) play the delayed input", "[chain]")
{
    auto p = busy();
    p.bypass = true;
    const auto x = testsig::vocal (kSr, 1.0);
    const auto y = process (p, x, 2, 333);
    double err = 0.0;
    for (size_t i = kLat; i < x.size(); ++i)
        err = std::max ({ err, std::abs (y[0][i] - x[i - kLat]), std::abs (y[1][i] - x[i - kLat]) });
    CHECK (err < 1.0e-9);

    p.bypass = false; p.listenOriginal = true; p.gainOriginalDb = -6.0;
    VocalChain c; c.setParams (p); c.prepare (kSr, 1);
    std::vector<double> z (x.begin(), x.end());
    double* ptr = z.data();
    c.process (&ptr, 1, static_cast<int> (z.size()));
    CHECK (z[30000] == Approx (x[30000 - kLat] * std::pow (10.0, -6.0 / 20.0)).margin (1e-9));
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

TEST_CASE ("In the chain the gate opens before a soft word start (look-ahead from Pitch's latency)", "[chain][cleanup]")
{
    // Room noise at -70 dB, then a word that swells from -60 to -12 dBFS over 60 ms and holds.
    std::vector<float> x (static_cast<size_t> (1.5 * kSr), 0.0f);
    std::mt19937 rng (3);
    std::normal_distribution<double> w (0.0, 1.0);
    for (auto& v : x) v = static_cast<float> (3.0e-4 * w (rng));
    const auto w0 = static_cast<size_t> (0.8 * kSr), ramp = static_cast<size_t> (0.06 * kSr);
    for (size_t i = w0; i < x.size(); ++i)
    {
        const double t = std::min (1.0, static_cast<double> (i - w0) / static_cast<double> (ramp));
        x[i] += static_cast<float> (std::pow (10.0, (-60.0 + 48.0 * t) / 20.0) * std::sin (2.0 * std::numbers::pi * 220.0 * static_cast<double> (i) / kSr));
    }
    ChainParams p;
    p.cleanup.gateThrDb = -30.0;
    p.cleanup.gateRangeDb = 18.0;
    const auto y = VocalChain::render ({ x }, kSr, p);
    // The swell crosses -30 dB peak at 37.5 ms; the 8 ms before that already pass at full level.
    const auto cross = w0 + static_cast<size_t> (0.0375 * kSr), a = cross - static_cast<size_t> (0.008 * kSr);
    double ein = 0.0, eout = 0.0;
    for (size_t i = a; i < cross; ++i) { ein += static_cast<double> (x[i]) * x[i]; eout += static_cast<double> (y[0][i]) * y[0][i]; }
    CHECK (10.0 * std::log10 (eout / ein) > -1.0);
    // The gaps are still turned down.
    double gin = 0.0, gout = 0.0;
    for (size_t i = static_cast<size_t> (0.3 * kSr); i < static_cast<size_t> (0.6 * kSr); ++i) { gin += static_cast<double> (x[i]) * x[i]; gout += static_cast<double> (y[0][i]) * y[0][i]; }
    CHECK (10.0 * std::log10 (gout / gin) == Approx (-18.0).margin (1.0));
}
