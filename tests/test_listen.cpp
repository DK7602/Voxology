// LISTEN (hear what a module takes out): what plays while listening is exactly what that module took out of the vocal,
// so the normal output plus the listened one give back the original (with only that module working). It is quiet
// until it is complete, it is turned up so it can be heard, and A / bypass never play it.
#include "Signals.h"
#include "vox/VocalChain.h"

#include <cmath>

#include <catch2/catch_test_macros.hpp>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

struct Run
{
    std::vector<std::vector<double>> out;
    ChainMeters meters;
    int latency = 0;
};

Run run (const ChainParams& p, const std::vector<float>& mono, bool autoLevel = false, int channels = 2)
{
    VocalChain c;
    c.setParams (p);
    c.prepare (kSr, channels);
    c.setListenAutoLevel (autoLevel);
    Run r;
    r.latency = c.latencySamples();
    r.out.assign (static_cast<size_t> (channels), std::vector<double> (mono.begin(), mono.end()));
    const size_t block = 512;
    for (size_t s = 0; s < mono.size(); s += block)
    {
        std::array<double*, 2> ptr {};
        for (int ch = 0; ch < channels; ++ch) ptr[static_cast<size_t> (ch)] = r.out[static_cast<size_t> (ch)].data() + s;
        c.process (ptr.data(), channels, static_cast<int> (std::min (block, mono.size() - s)));
        const auto m = c.takeMeters();
        if (m.listenPlaying != listen::off) r.meters = m;
    }
    return r;
}

/** Largest |normal + listened - original| after `from` samples, against the original's peak. */
double worstSum (const Run& normal, const Run& listened, const std::vector<float>& x, size_t from)
{
    double worst = 0.0, peak = 0.0;
    const auto lat = static_cast<size_t> (normal.latency);
    for (size_t c = 0; c < normal.out.size(); ++c)
        for (size_t i = from; i < x.size(); ++i)
        {
            const double orig = x[i - lat];
            worst = std::max (worst, std::abs (normal.out[c][i] + listened.out[c][i] - orig));
            peak = std::max (peak, std::abs (orig));
        }
    return worst / std::max (peak, 1.0e-12);
}

double energy (const Run& r, size_t from)
{
    double e = 0.0;
    for (const auto& ch : r.out)
        for (size_t i = from; i < ch.size(); ++i) e += ch[i] * ch[i];
    return e;
}
}

TEST_CASE ("Listen: what plays is exactly what the module took out", "[listen]")
{
    const auto x = testsig::vocal (kSr, 4.0, -40.0, -10.0);
    struct Case { const char* name; int mode; ChainParams p; };
    std::vector<Case> cases;
    { ChainParams p; p.deEsser.amount = 80; cases.push_back ({ "De-Esser", listen::deEsser, p }); }
    { ChainParams p; p.cleanup.noiseDb = 15; p.cleanup.roomPct = 60; cases.push_back ({ "Noise + Room", listen::cleanup, p }); }
    { ChainParams p; p.cleanup.noiseDb = 10; p.cleanup.lowCutHz = 150; p.cleanup.gateThrDb = -30; p.cleanup.gateRangeDb = 12;
      p.cleanup.breathDb = 8; p.cleanup.popAmount = 60; cases.push_back ({ "all of Cleanup", listen::cleanup, p }); }
    { ChainParams p; p.cleanup.aiMode = true; p.cleanup.noiseDb = 24; cases.push_back ({ "AI cleanup", listen::cleanup, p }); }
    { ChainParams p; p.cleanup.aiMode = true; p.cleanup.noiseDb = 12; p.cleanup.gateThrDb = -30; p.cleanup.gateRangeDb = 12;
      p.cleanup.breathDb = 8; cases.push_back ({ "AI cleanup (12 dB) + gate + breaths", listen::cleanup, p }); }
    { ChainParams p; p.dynEq.smoothPct = 90; cases.push_back ({ "Smooth", listen::dynEq, p }); }
    { ChainParams p; p.dynEq.maxCutDb = { 6, 6, 6, 9 }; p.dynEq.sensitivity = 80; p.dynEq.smoothPct = 60;
      cases.push_back ({ "Dynamic EQ bands + Smooth", listen::dynEq, p }); }

    for (const auto& k : cases)
    {
        INFO (k.name);
        const auto normal = run (k.p, x);
        auto lp = k.p;
        lp.listenMode = k.mode;
        const auto listened = run (lp, x);
        // complete after the chain's latency, the arm margin, the level measurement and the 15 ms fade
        const size_t from = static_cast<size_t> (normal.latency) + 512 + VocalChain::kChunk
                          + static_cast<size_t> ((ListenMix::kMeasureSeconds + ListenMix::kFadeSeconds) * kSr) + 1024;
        CHECK (worstSum (normal, listened, x, from) < 1.0e-6);
        CHECK (energy (listened, from) > 1.0e-6 * energy (normal, from));   // it took something out
        CHECK (listened.meters.listenPlaying == k.mode);
        CHECK (listened.meters.listenUnderDb > -99.0);   // measured (how far under the vocal it really is)
    }
}

TEST_CASE ("Listen: silent while it starts, turned up to be heard, never on A or bypass", "[listen]")
{
    const auto x = testsig::vocal (kSr, 3.0, -40.0, -10.0);
    ChainParams p;
    p.deEsser.amount = 60;
    const auto normal = run (p, x);
    p.listenMode = listen::deEsser;

    // While it fills its lines the vocal keeps playing, untouched.
    const auto quiet = run (p, x, true);
    const size_t armed = static_cast<size_t> (quiet.latency + VocalChain::kChunk);
    for (size_t i = 0; i < armed; ++i) REQUIRE (quiet.out[0][i] == normal.out[0][i]);
    // Then turned up toward 12 dB under the vocal (a de-esser's take-out sits further down as it is).
    CHECK (quiet.meters.listenUnderDb < -15.0);
    CHECK (quiet.meters.listenGainDb > 3.0);

    // A (the original) and bypass: never the take-out.
    for (const bool a : { true, false })
    {
        INFO ((a ? "A" : "bypass"));
        auto q = p;
        (a ? q.listenOriginal : q.bypass) = true;
        auto plain = q;
        plain.listenMode = listen::off;
        const auto with = run (q, x, true), without = run (plain, x, true);
        for (size_t i = 0; i < x.size(); ++i) REQUIRE (with.out[0][i] == without.out[0][i]);
    }

    // Off again: the plain vocal, bit for bit (nothing left running).
    const auto off = run (ChainParams {}, x), again = run (ChainParams {}, x);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE (off.out[1][i] == again.out[1][i]);
}

TEST_CASE ("Listen: a mono vocal", "[listen]")
{
    const auto x = testsig::vocal (kSr, 3.0, -40.0, -10.0);
    ChainParams p;
    p.deEsser.amount = 70;
    p.cleanup.noiseDb = 12;
    const auto normal = run (p, x, false, 1);
    p.listenMode = listen::deEsser;
    const auto listened = run (p, x, false, 1);
    const size_t from = static_cast<size_t> (normal.latency) + 512 + VocalChain::kChunk + static_cast<size_t> (0.2 * kSr);
    // (Noise works too, so the de-esser's take-out alone doesn't add back to the original: it is what the De-Esser
    // got minus what it gave.)
    CHECK (energy (listened, from) > 0.0);
    CHECK (listened.meters.listenPlaying == listen::deEsser);
}
