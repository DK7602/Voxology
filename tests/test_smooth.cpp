// Smooth (v0.43): the Dynamic EQ's resonance smoother, inside the noise remover's spectra (no added delay).
#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/Denoise.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

std::vector<float> withResonance (const std::vector<float>& x, double hz, double db, double q)
{
    Biquad b;
    design::apply (b, design::bell (hz, db, q, kSr));
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float> (b.process (x[i]));
    return y;
}

std::vector<double> smoothed (const std::vector<float>& x, double pct)
{
    Denoiser d;
    d.prepare (kSr, 1);
    d.setParams ({ 0.0, 0.0, pct });
    const int lat = d.latencySamples();
    std::vector<double> y (x.size() + static_cast<size_t> (lat), 0.0);
    std::copy (x.begin(), x.end(), y.begin());
    for (size_t s = 0; s < y.size(); s += 256)
    {
        double* p = y.data() + s;
        d.process (&p, 1, static_cast<int> (std::min<size_t> (256, y.size() - s)));
    }
    return std::vector<double> (y.begin() + lat, y.end());
}

/** Third-octave level of `band` (Hz) minus the average of the two a third of an octave either side (the bump). */
double bump (const std::vector<double>& tone, double band)
{
    const auto& bands = analysisBands();
    for (size_t k = 1; k + 1 < bands.size(); ++k)
        if (std::abs (bands[k] - band) < 1.0) return tone[k] - 0.5 * (tone[k - 1] + tone[k + 1]);
    return 0.0;
}
}

TEST_CASE ("Smooth: a ringing resonance comes down, a smooth voice stays as it is; 0 = untouched", "[smooth]")
{
    const auto v = testsig::vocal (kSr, 14.0, -70.0, -14.0, 3, 150.0);
    const std::vector<float> clean (v.begin(), v.end());
    const auto ringing = withResonance (clean, 3150.0, 10.0, 8.0);

    // 0 %: the input, exactly (one frame late).
    const auto off = smoothed (ringing, 0.0);
    for (size_t i = 0; i < ringing.size(); ++i) REQUIRE (off[i] == static_cast<double> (ringing[i]));

    auto tone = [] (const std::vector<double>& y) { return analyseVocal ({ std::vector<float> (y.begin(), y.end()) }, kSr).bandDb; };
    const auto before = tone (std::vector<double> (ringing.begin(), ringing.end()));
    for (const double pct : { 40.0, 80.0 })
    {
        const auto after = tone (smoothed (ringing, pct));
        INFO ("Smooth " << pct << " %: resonance bump " << bump (before, 3150.0) << " -> " << bump (after, 3150.0) << " dB");
        CHECK (bump (after, 3150.0) < bump (before, 3150.0) - (pct > 50.0 ? 4.0 : 2.0));
        // Elsewhere the voice keeps its tone.
        const auto& bands = analysisBands();
        for (size_t k = 0; k < bands.size(); ++k)
            if ((bands[k] < 1600.0 || bands[k] > 6300.0) && bands[k] > 150.0 && bands[k] < 12000.0)
            {
                INFO (bands[k] << " Hz");
                CHECK (std::abs ((after[k] - after[analysisBands().size() / 2]) - (before[k] - before[analysisBands().size() / 2])) < 1.5);
            }
    }
    // A voice with no resonance: every band within 1.5 dB at 80 %.
    const auto plain = tone (std::vector<double> (clean.begin(), clean.end())), plainOut = tone (smoothed (clean, 80.0));
    for (size_t k = 0; k < plain.size(); ++k)
        if (analysisBands()[k] > 150.0 && analysisBands()[k] < 12000.0)
        {
            INFO (analysisBands()[k] << " Hz: " << plain[k] << " -> " << plainOut[k]);
            CHECK (std::abs (plainOut[k] - plain[k]) < 1.5);
        }
}

TEST_CASE ("Smooth in the chain: the Dynamic EQ's switch turns it off; no added delay", "[smooth]")
{
    const auto v = testsig::vocal (kSr, 8.0, -70.0, -14.0, 3, 150.0);
    const auto ringing = withResonance (std::vector<float> (v.begin(), v.end()), 3150.0, 10.0, 8.0);
    ChainParams p;
    VocalChain a;
    a.prepare (kSr, 1);
    const int lat = a.latencySamples();
    p.dynEq.smoothPct = 60.0;
    VocalChain b;
    b.setParams (p);
    b.prepare (kSr, 1);
    CHECK (b.latencySamples() == lat);
    const auto plain = VocalChain::render ({ ringing }, kSr, ChainParams {});
    const auto on = VocalChain::render ({ ringing }, kSr, p);
    p.dynEq.enabled = false;
    const auto switchedOff = VocalChain::render ({ ringing }, kSr, p);
    for (size_t i = 0; i < plain[0].size(); ++i) REQUIRE (switchedOff[0][i] == plain[0][i]);
    double diff = 0.0;
    for (size_t i = 0; i < plain[0].size(); ++i) diff = std::max (diff, std::abs (static_cast<double> (on[0][i] - plain[0][i])));
    CHECK (diff > 0.001);
}
