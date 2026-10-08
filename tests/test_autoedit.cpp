#include "Signals.h"
#include "vox/AutoEdit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

void checkNotes (const AutoEditResult& r)
{
    // Every warning carries a plan: "TITLE: text", then "NEED:" and at least one "STEP:".
    for (const auto& n : r.notes)
    {
        INFO (n);
        CHECK (n.find (": ") != std::string::npos);
        CHECK (n.find ("\nNEED: ") != std::string::npos);
        CHECK (n.find ("\nSTEP: ") != std::string::npos);
    }
}
}

TEST_CASE ("Auto-Edit sets up a noisy, sibilant rap vocal", "[autoedit]")
{
    const auto x = testsig::vocal (kSr, 14.0, -58.0, -1.0);
    const auto r = autoEdit ({ x }, kSr, { 0, 1, 140.0 });
    REQUIRE (r.ok);
    const auto& p = r.params;
    INFO (r.summary);
    CHECK (r.analysis.f0Median == Approx (140.0).margin (20.0));
    CHECK (p.cleanup.lowCutHz >= 50.0);
    CHECK (p.cleanup.lowCutHz <= 120.0);
    CHECK (p.cleanup.gateRangeDb > 0.0);
    CHECK (p.cleanup.gateThrDb > r.analysis.noiseFloorDb);
    CHECK (p.deEsser.amount > 0.0);
    CHECK (p.comp.peakThrDb < 0.0);
    CHECK (p.comp.ratio > 1.0);
    CHECK (p.saturation.driveDb > 0.0);
    CHECK (p.delay.bpm == 140.0);
    checkNotes (r);

    std::set<std::string> modules;
    for (const auto& reason : r.reasons) { modules.insert (reason.module); CHECK (! reason.why.empty()); }
    for (const char* m : { "pitch", "cleanup", "eq", "dyneq", "deess", "rider", "comp", "sat", "double", "delay", "reverb", "out" })
        CHECK (modules.count (m) == 1);

    // The processed vocal comes out at about the loudness it went in.
    CHECK (r.outputLufs == Approx (r.analysis.inputLufs).margin (1.0));
    const auto y = VocalChain::render ({ x }, kSr, p);
    for (float v : y[0]) REQUIRE (std::isfinite (v));
}

TEST_CASE ("Auto-Edit: clean vocal keeps the gate off; styles differ", "[autoedit]")
{
    const auto x = testsig::vocal (kSr, 14.0, -95.0, -16.0, 5, 210.0);
    const auto rap = autoEdit ({ x }, kSr, { 1, 1, 0.0 });
    REQUIRE (rap.ok);
    CHECK (rap.params.cleanup.gateRangeDb == 0.0);
    CHECK (rap.params.doubler.amount == 0.0);
    CHECK (rap.params.cleanup.lowCutHz > 100.0);   // higher voice -> higher cut
    const auto rnb = autoEdit ({ x }, kSr, { 4, 1, 0.0 });
    CHECK (rnb.params.reverb.mix > rap.params.reverb.mix);
    CHECK (rnb.params.reverb.decayS > rap.params.reverb.decayS);
    checkNotes (rap);
}

TEST_CASE ("Auto-Edit: Pop, Folk and Natural Singer styles", "[autoedit]")
{
    const auto x = testsig::vocal (kSr, 14.0, -95.0, -16.0, 5, 210.0);
    const auto pop = autoEdit ({ x }, kSr, { 5, 1, 0.0 });
    const auto folk = autoEdit ({ x }, kSr, { 6, 1, 0.0 });
    const auto nat = autoEdit ({ x }, kSr, { 7, 1, 0.0 });
    REQUIRE (pop.ok);
    REQUIRE (folk.ok);
    REQUIRE (nat.ok);
    CHECK (pop.params.doubler.amount > 0.0);
    CHECK (pop.params.delay.mix > 0.0);
    // Folk and Natural Singer: one voice in a room, no doubler, no echo, gentle compression.
    for (const auto* r : { &folk, &nat })
    {
        CHECK (r->params.doubler.amount == 0.0);
        CHECK (r->params.delay.mix == 0.0);
        CHECK (r->params.reverb.mix > 0.0);
        CHECK (r->params.comp.ratio < pop.params.comp.ratio);
        checkNotes (*r);
    }
    CHECK (nat.params.reverb.decayS < pop.params.reverb.decayS);
    checkNotes (pop);
}

TEST_CASE ("Auto-Edit trims a boomy recording and flags clipping", "[autoedit]")
{
    auto x = testsig::vocal (kSr, 14.0, -70.0, -10.0, 9, 120.0, true);
    for (auto& v : x) v = std::clamp (v * 6.0f, -1.0f, 1.0f);   // hot and clipped
    const auto r = autoEdit ({ x }, kSr, { 0, 1, 0.0 });
    REQUIRE (r.ok);
    CHECK (r.params.eq.gainDb[0] < 0.0);
    bool clipNote = false;
    for (const auto& n : r.notes) clipNote |= n.rfind ("CLIPPED", 0) == 0;
    CHECK (clipNote);
    checkNotes (r);
}

TEST_CASE ("Auto-Edit asks for more voice when it heard too little", "[autoedit]")
{
    auto x = testsig::vocal (kSr, 2.0);
    const auto r = autoEdit ({ x }, kSr, {});
    CHECK_FALSE (r.ok);
    CHECK_FALSE (r.tip.empty());
}

TEST_CASE ("Auto-Edit Tone EQ: target from pro vocals follows the voice's pitch; moves stay in their jobs", "[autoedit][eq]")
{
    // Below 630 Hz the target follows the voice (a low voice has its fundamental there); above it, one curve.
    const auto low = styleTarget (1, 140.0), high = styleTarget (1, 330.0);
    const auto& bands = analysisBands();
    for (size_t b = 0; b < bands.size(); ++b)
    {
        if (bands[b] == 125.0) CHECK (low[b] > high[b] + 15.0);
        if (bands[b] >= 630.0) CHECK (low[b] == high[b]);
    }

    const auto boomy = autoEdit ({ testsig::vocal (kSr, 14.0, -70.0, -14.0, 9, 140.0, true) }, kSr, { 1, 1, 0.0 });
    const auto plain = autoEdit ({ testsig::vocal (kSr, 14.0, -70.0, -14.0, 9, 140.0, false) }, kSr, { 1, 1, 0.0 });
    for (const auto* r : { &boomy, &plain })
    {
        REQUIRE (r->ok);
        const auto& eq = r->params.eq;
        CHECK (eq.gainDb[1] <= 0.0);   // Mud and Nasal only cut
        CHECK (eq.gainDb[2] <= 0.0);
        if (eq.gainDb[0] != 0.0) CHECK (eq.freqHz[0] >= std::max (1.3 * r->params.cleanup.lowCutHz, r->analysis.f0Low) - 1.0);   // on the voice, not under it
        if (eq.gainDb[3] != 0.0) CHECK (eq.freqHz[3] <= 5000.0);   // the words, not the "s"
        if (eq.gainDb[4] != 0.0) CHECK (eq.freqHz[4] >= 8000.0);
        CHECK (eq.gainDb[3] >= -2.0);
        CHECK (eq.gainDb[4] >= -1.5);
        checkNotes (*r);
    }
    // The boomy one gets its low end trimmed, more than the plain one.
    CHECK (boomy.params.eq.gainDb[0] < 0.0);
    CHECK (boomy.params.eq.gainDb[0] < plain.params.eq.gainDb[0]);
}
