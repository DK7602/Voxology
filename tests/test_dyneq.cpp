#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/DynamicEq.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

/** A clean voice whose "oh" vowels bloom: every 1.2 s, for 250 ms, a bell at bloomHz rises by
    bloomDb (smooth 30 ms fades). Scaled so the voice sits at levelDb RMS. */
std::vector<float> bloomingVoice (double levelDb, double bloomHz = 350.0, double bloomDb = 12.0, double seconds = 8.0, double period = 1.2)
{
    auto v = testsig::vocal (kSr, seconds, -120.0, -60.0, 7, 140.0);
    // Make it continuous (no word gaps), so the bloom is the only thing that changes.
    std::vector<double> src (v.size());
    double phase = 0.0;
    for (size_t i = 0; i < src.size(); ++i)
    {
        phase += 140.0 / kSr;
        double s = 0.0;
        for (int h = 1; h * 140.0 < 0.45 * kSr && h < 60; ++h) s += std::sin (2.0 * std::numbers::pi * h * phase) / h;
        src[i] = s;
    }
    Biquad bell;
    design::apply (bell, design::bell (bloomHz, bloomDb, 1.4, kSr));
    double e = 0.0;
    for (size_t i = 0; i < src.size(); ++i)
    {
        const double t = std::fmod (static_cast<double> (i) / kSr, period);
        double env = 0.0;
        if (t >= 0.6 && t < 0.85) env = std::min ({ 1.0, (t - 0.6) / 0.03, (0.85 - t) / 0.03 });
        const double b = bell.process (src[i]);
        src[i] = src[i] + env * (b - src[i]);
        e += src[i] * src[i];
    }
    const double k = std::pow (10.0, levelDb / 20.0) / std::sqrt (e / static_cast<double> (src.size()));
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float> (src[i] * k);
    return v;
}

std::vector<double> run (DynamicEq& m, const DynEqParams& p, const std::vector<float>& in, int block = 512,
                         std::vector<std::array<double, kDynBands>>* cuts = nullptr)
{
    m.prepare (kSr, 1);
    m.setParams (p);
    std::vector<double> x (in.begin(), in.end());
    for (size_t s = 0; s < x.size(); s += static_cast<size_t> (block))
    {
        double* ptr = x.data() + s;
        m.process (&ptr, 1, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), x.size() - s)));
        if (cuts) cuts->push_back (m.takeCutDb());
    }
    return x;
}

DynEqParams mudOnly (double maxCut = 8.0)
{
    DynEqParams p;
    p.maxCutDb = { 0, maxCut, 0, 0 };
    p.freqHz[1] = 350.0;
    p.sensitivity = 75.0;   // the test bloom rises ~5 dB over the rest of the voice
    return p;
}

/** Deepest cut (dB, >= 0) of a band inside / outside the blooms, after the first 2.4 s of learning. */
void bloomCuts (const std::vector<std::array<double, kDynBands>>& cuts, int block, int band, double& inBloom, double& outside)
{
    inBloom = outside = 0.0;
    for (size_t k = 0; k < cuts.size(); ++k)
    {
        const double t0 = static_cast<double> (k) * block / kSr;
        if (t0 < 2.4) continue;
        const double t = std::fmod (t0, 1.2);
        const double c = -cuts[k][static_cast<size_t> (band)];
        if (t >= 0.65 && t < 0.8) inBloom = std::max (inBloom, c);
        else if (t < 0.5 || t > 1.05) outside = std::max (outside, c);
    }
}
}

TEST_CASE ("Dynamic EQ is an exact pass-through when off or at Max Cut 0", "[dyneq]")
{
    const auto x = bloomingVoice (-20.0);
    DynamicEq a, b;
    const auto y = run (a, DynEqParams {}, x);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE (y[i] == static_cast<double> (x[i]));
    auto off = mudOnly (12.0);
    off.enabled = false;
    const auto z = run (b, off, x);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE (z[i] == static_cast<double> (x[i]));
}

TEST_CASE ("Dynamic EQ cuts the bloom only while it happens", "[dyneq]")
{
    const int block = 240;   // 5 ms
    std::vector<std::array<double, kDynBands>> cuts;
    DynamicEq m;
    run (m, mudOnly(), bloomingVoice (-20.0), block, &cuts);
    double in = 0.0, out = 0.0;
    bloomCuts (cuts, block, 1, in, out);
    INFO ("in bloom " << in << " dB, outside " << out << " dB");
    CHECK (in > 2.5);
    CHECK (in <= 8.0 + 1.0e-9);   // never past Max Cut
    CHECK (out < 0.5);
}

TEST_CASE ("Dynamic EQ reacts the same whispered or shouted", "[dyneq]")
{
    const int block = 240;
    double inQuiet = 0.0, outQuiet = 0.0, inLoud = 0.0, outLoud = 0.0;
    {
        std::vector<std::array<double, kDynBands>> cuts;
        DynamicEq m;
        run (m, mudOnly(), bloomingVoice (-40.0), block, &cuts);
        bloomCuts (cuts, block, 1, inQuiet, outQuiet);
    }
    {
        std::vector<std::array<double, kDynBands>> cuts;
        DynamicEq m;
        run (m, mudOnly(), bloomingVoice (-8.0), block, &cuts);
        bloomCuts (cuts, block, 1, inLoud, outLoud);
    }
    CHECK (inQuiet == Approx (inLoud).margin (0.2));
    CHECK (outQuiet == Approx (outLoud).margin (0.2));
}

TEST_CASE ("Dynamic EQ: a steady tone (no bloom) is left alone; Max Cut and Sensitivity work", "[dyneq]")
{
    const int block = 240;
    {
        // The same voice with the bell on all the time: that's its normal, not a problem moment.
        std::vector<std::array<double, kDynBands>> cuts;
        DynamicEq m;
        run (m, mudOnly(), bloomingVoice (-20.0, 350.0, 0.0), block, &cuts);
        double in = 0.0, out = 0.0;
        bloomCuts (cuts, block, 1, in, out);
        CHECK (std::max (in, out) < 0.5);
    }
    {
        std::vector<std::array<double, kDynBands>> cuts;
        DynamicEq m;
        auto p = mudOnly (3.0);
        p.sensitivity = 95.0;
        run (m, p, bloomingVoice (-20.0), block, &cuts);
        double in = 0.0, out = 0.0;
        bloomCuts (cuts, block, 1, in, out);
        CHECK (in == Approx (3.0).margin (0.05));
    }
    double lowSens = 0.0, highSens = 0.0, dummy = 0.0;
    for (double sens : { 10.0, 90.0 })
    {
        auto p = mudOnly (12.0);
        p.sensitivity = sens;
        std::vector<std::array<double, kDynBands>> cuts;
        DynamicEq m;
        run (m, p, bloomingVoice (-20.0), block, &cuts);
        bloomCuts (cuts, block, 1, sens < 50.0 ? lowSens : highSens, dummy);
    }
    CHECK (highSens > lowSens + 3.0);
}

TEST_CASE ("Dynamic EQ: other bands don't react to a mud bloom; results don't depend on block size", "[dyneq]")
{
    DynEqParams p = mudOnly();
    p.maxCutDb = { 8, 8, 8, 8 };
    std::vector<std::array<double, kDynBands>> cuts;
    DynamicEq a;
    const auto y1 = run (a, p, bloomingVoice (-20.0), 240, &cuts);
    double in = 0.0, out = 0.0;
    bloomCuts (cuts, 240, 3, in, out);
    CHECK (in < 1.0);   // Harsh (3.5 kHz) stays put while the mud blooms
    bloomCuts (cuts, 240, 1, in, out);
    CHECK (in > 2.5);

    DynamicEq b;
    const auto y2 = run (b, p, bloomingVoice (-20.0), 37);
    for (size_t i = 0; i < y1.size(); ++i) REQUIRE (y1[i] == Approx (y2[i]).margin (1.0e-12));
    for (double v : y1) REQUIRE (std::isfinite (v));
}

TEST_CASE ("Auto-Edit finds a blooming spot and sets the Dynamic EQ there; a steady voice gets none", "[dyneq][autoedit]")
{
    const auto bloom = autoEdit ({ bloomingVoice (-20.0, 400.0, 18.0, 16.0, 2.4) }, kSr, { 0, 1, 0.0 });
    REQUIRE (bloom.ok);
    const auto& d = bloom.params.dynEq;
    INFO ("mud " << d.maxCutDb[1] << " dB at " << d.freqHz[1] << " Hz");
    CHECK (d.maxCutDb[1] >= 2.0);
    CHECK (d.freqHz[1] >= 315.0);
    CHECK (d.freqHz[1] <= 500.0);
    CHECK (d.maxCutDb[3] == 0.0);   // nothing harsh jumps out
    CHECK_FALSE (bloom.kept[static_cast<size_t> (Module::dynEq)]);

    const auto steady = autoEdit ({ bloomingVoice (-20.0, 400.0, 0.0, 16.0, 2.4) }, kSr, { 0, 1, 0.0 });
    REQUIRE (steady.ok);
    for (double c : steady.params.dynEq.maxCutDb) CHECK (c == 0.0);
    CHECK (steady.kept[static_cast<size_t> (Module::dynEq)]);
}
