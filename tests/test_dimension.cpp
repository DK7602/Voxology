// Dimension (v0.39): chorus, flanger, phaser on the finished vocal.
#include "Signals.h"
#include "vox/Dimension.h"
#include "vox/Fft.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

std::array<std::vector<double>, 2> run (const std::vector<float>& x, const std::function<DimensionParams (double)>& at)
{
    Dimension d;
    d.setParams (at (0.0));
    d.prepare (kSr);
    std::array<std::vector<double>, 2> y { std::vector<double> (x.begin(), x.end()), std::vector<double> (x.begin(), x.end()) };
    for (size_t s = 0; s < x.size(); s += 256)
    {
        d.setParams (at (static_cast<double> (s) / kSr));
        std::array<double*, 2> p { y[0].data() + s, y[1].data() + s };
        d.process (p.data(), 2, static_cast<int> (std::min<size_t> (256, x.size() - s)));
    }
    return y;
}

double rmsDb (const std::vector<double>& x, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += x[i] * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1.0e-30);
}

double correlation (const std::vector<double>& a, const std::vector<double>& b, size_t from, size_t to)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = from; i < to; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / std::sqrt (aa * bb + 1.0e-30);
}

/** Top end over 5 kHz: the biggest jump in [a, b) (clicks). */
double topPeak (const std::vector<double>& x, size_t a, size_t b)
{
    Biquad h1, h2;
    design::apply (h1, design::butterworth (true, 5000.0, kSr));
    design::apply (h2, design::butterworth (true, 5000.0, kSr));
    double peak = 0.0;
    for (size_t i = a - 4800; i < b; ++i)
    {
        const double y = h2.process (h1.process (x[i]));
        if (i >= a) peak = std::max (peak, std::abs (y));
    }
    return peak;
}

DimensionParams dim (int mode, double amount, double width = 70.0)
{
    DimensionParams p;
    p.mode = mode;
    p.amount = amount;
    p.width = width;
    return p;
}
}

TEST_CASE ("Dimension: off is untouched; each mode keeps the level and widens", "[dimension]")
{
    const auto v = testsig::vocal (kSr, 6.0, -70.0, -14.0, 3);
    const std::vector<float> x (v.begin(), v.end());
    const auto off = run (x, [] (double) { return dim (0, 0.0); });
    for (size_t i = 0; i < x.size(); ++i) { REQUIRE (off[0][i] == static_cast<double> (x[i])); REQUIRE (off[1][i] == static_cast<double> (x[i])); }

    const std::vector<double> dry (x.begin(), x.end());
    const auto a = static_cast<size_t> (1.0 * kSr), b = static_cast<size_t> (6.0 * kSr) - 1;
    for (int mode = 0; mode < kDimensionModes; ++mode)
    {
        INFO (kDimensionNames[static_cast<size_t> (mode)]);
        const auto y = run (x, [&] (double) { return dim (mode, 100.0); });
        for (int c = 0; c < 2; ++c) CHECK (std::abs (rmsDb (y[static_cast<size_t> (c)], a, b) - rmsDb (dry, a, b)) < 1.0);   // (measured within 0.9 dB)
        CHECK (correlation (y[0], y[1], a, b) < 0.98);   // left and right differ: wider
        const auto narrow = run (x, [&] (double) { return dim (mode, 100.0, 0.0); });
        CHECK (correlation (narrow[0], narrow[1], a, b) > 0.999);   // Width 0: the same both sides
        for (const auto& ch : y) for (double s : ch) REQUIRE (std::isfinite (s));
    }
}

TEST_CASE ("Dimension: switching on / off and changing mode doesn't click", "[dimension]")
{
    const auto v = testsig::vocal (kSr, 6.0, -70.0, -14.0, 3);
    const std::vector<float> x (v.begin(), v.end());
    const auto steady = run (x, [] (double) { return dim (1, 100.0); });
    const double ref = topPeak (steady[0], static_cast<size_t> (1.0 * kSr), static_cast<size_t> (5.5 * kSr));
    const auto y = run (x, [] (double t) { return t < 1.5 ? dim (0, 0.0) : t < 3.0 ? dim (0, 100.0) : t < 4.5 ? dim (1, 100.0) : dim (2, 0.0); });
    for (const double t : { 1.5, 3.0, 4.5 })
    {
        INFO ("at " << t << " s");
        CHECK (topPeak (y[0], static_cast<size_t> ((t - 0.05) * kSr), static_cast<size_t> ((t + 0.4) * kSr)) < 1.3 * ref);
    }
    // Off again: back to the dry vocal exactly, once the fade is done.
    for (size_t i = static_cast<size_t> (5.0 * kSr); i < x.size(); ++i) REQUIRE (y[0][i] == static_cast<double> (x[i]));
}
