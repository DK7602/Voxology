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
