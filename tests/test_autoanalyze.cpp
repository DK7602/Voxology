#include "Signals.h"
#include "vox/AutoAnalyze.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

const AnalyzeNeed* find (const std::vector<AnalyzeNeed>& needs, const std::string& key)
{
    for (const auto& n : needs)
        if (n.key == key) return &n;
    return nullptr;
}

bool retunes (const AnalyzeNeed& n, Module m) { return std::find (n.retune.begin(), n.retune.end(), m) != n.retune.end(); }

void checkNotes (const std::vector<AnalyzeNeed>& needs)
{
    // Learn's rule: "TITLE: text" (title short enough for Learn), then "NEED:" and at least one "STEP:".
    for (const auto& n : needs)
    {
        INFO (n.note);
        const auto cut = n.note.find (": ");
        CHECK (cut != std::string::npos);
        CHECK (cut < 32);
        CHECK (n.note.find ("\nNEED: ") != std::string::npos);
        CHECK (n.note.find ("\nSTEP: ") != std::string::npos);
    }
}

// A measured vocal, as a starting point for hand-made changes.
AutoEditResult measured()
{
    AutoEditResult r;
    r.ok = true;
    auto& a = r.analysis;
    a.voicedSeconds = 12.0; a.voiceRmsDb = -24.0; a.noiseFloorDb = -72.0; a.heardGaps = true; a.rumbleDb = -40.0; a.f0Low = 110.0;
    a.sibilanceDb = -10.0; a.microDynDb = 6.0; a.rangeDb = 8.0;
    a.bandDb.assign (analysisBands().size(), 0.0);
    return r;
}

std::vector<float> scaled (std::vector<float> x, double db)
{
    const auto g = static_cast<float> (std::pow (10.0, db / 20.0));
    for (auto& v : x) v *= g;
    return x;
}
}

TEST_CASE ("Auto Analyze: the same vocal again needs nothing new", "[autoanalyze]")
{
    const auto r = measured();
    CHECK (findNewNeeds (r.analysis, r.beat, r.analysis, r.beat).empty());

    // Small differences (normal between two parts of a take) stay quiet.
    auto s = measured();
    s.analysis.voiceRmsDb += 2.5; s.analysis.noiseFloorDb += 4.0; s.analysis.sibilanceDb += 2.0;
    for (auto& v : s.analysis.bandDb) v += 1.5;
    CHECK (findNewNeeds (r.analysis, r.beat, s.analysis, s.beat).empty());

    // Nothing heard says nothing.
    CHECK (findNewNeeds (r.analysis, r.beat, VocalAnalysis {}, BeatAnalysis {}).empty());
}

TEST_CASE ("Auto Analyze: picks up each kind of change, each with a plan", "[autoanalyze]")
{
    const auto was = measured();
    std::vector<AnalyzeNeed> all;
    auto check = [&] (AutoEditResult now, const char* key, std::vector<Module> mods, bool fit = false)
    {
        INFO (key);
        const auto needs = findNewNeeds (was.analysis, was.beat, now.analysis, now.beat);
        const auto* n = find (needs, key);
        REQUIRE (n != nullptr);
        for (auto m : mods) CHECK (retunes (*n, m));
        CHECK (n->fitLevel == fit);
        all.insert (all.end(), needs.begin(), needs.end());
    };

    auto now = measured(); now.analysis.clippedRuns = 3;                        check (now, "clip", {});
    now = measured(); now.analysis.voiceRmsDb += 6.0;                           check (now, "level", { Module::cleanup, Module::rider, Module::comp });
    now = measured(); now.analysis.voiceRmsDb -= 6.0;                           check (now, "level", { Module::comp });
    now = measured(); now.analysis.noiseFloorDb = -55.0;                        check (now, "noise", { Module::cleanup });
    now = measured(); now.analysis.rumbleDb = -25.0;                            check (now, "low", { Module::cleanup });
    now = measured(); now.analysis.f0Low = 80.0;                                check (now, "low", { Module::cleanup });
    now = measured(); now.analysis.sibilanceDb = -5.0;                          check (now, "sib", { Module::deEsser });
    now = measured(); now.analysis.sibilanceDb = -16.0;                         check (now, "sib", { Module::deEsser });
    now = measured(); now.analysis.microDynDb = 9.5;                            check (now, "dynamics", { Module::rider, Module::comp });
    now = measured();
    {
        const auto& bands = analysisBands();
        for (size_t k = 0; k < bands.size(); ++k)
            if (bands[k] >= 3000.0 && bands[k] <= 6000.0) now.analysis.bandDb[k] = 5.0;
    }
    check (now, "tone", { Module::eq, Module::dynEq });

    // The beat: arrives, gets louder, or turns out to be a full mix.
    now = measured(); now.beat.heard = true;                                   check (now, "beatnew", { Module::eq, Module::comp }, true);
    now = measured(); now.beat.heard = true; now.beat.hasYourVocal = true;      check (now, "yourvocal", {});
    {
        auto withBeat = measured();
        withBeat.beat.heard = true; withBeat.beat.vocalOverBeatDb = -3.0;
        auto louder = withBeat;
        louder.beat.vocalOverBeatDb = -6.0;
        const auto needs = findNewNeeds (withBeat.analysis, withBeat.beat, louder.analysis, louder.beat);
        const auto* n = find (needs, "beatlevel");
        REQUIRE (n != nullptr);
        CHECK (n->fitLevel);
        CHECK (n->retune.empty());
        CHECK (n->note.find ("LOUDER") != std::string::npos);
        all.insert (all.end(), needs.begin(), needs.end());
        // ... but not for a small change.
        louder.beat.vocalOverBeatDb = -4.0;
        CHECK (find (findNewNeeds (withBeat.analysis, withBeat.beat, louder.analysis, louder.beat), "beatlevel") == nullptr);
    }
    checkNotes (all);
}

TEST_CASE ("Auto Analyze: readings an older report doesn't have are skipped", "[autoanalyze]")
{
    VocalAnalysis old;   // -120 / 0 everywhere: a report saved before these were kept
    old.voiceRmsDb = -24.0;
    auto now = measured();
    now.analysis.voiceRmsDb = -24.5;
    CHECK (findNewNeeds (old, {}, now.analysis, now.beat).empty());
}

TEST_CASE ("Auto Analyze: on real analysis, a louder, noisier take is picked up; the same take isn't", "[autoanalyze]")
{
    const auto first = analyseVocal ({ testsig::vocal (kSr, 14.0, -66.0, -14.0, 3) }, kSr);

    // Another stretch of the same take (other words, the same singer, mic and room).
    const auto again = analyseVocal ({ testsig::vocal (kSr, 14.0, -66.0, -14.0, 11) }, kSr);
    const auto same = findNewNeeds (first, {}, again, {});
    for (const auto& n : same) INFO (n.note);
    CHECK (same.empty());

    // A new take: 7 dB hotter, with a fan running.
    const auto hot = analyseVocal ({ scaled (testsig::vocal (kSr, 14.0, -50.0, -14.0, 5), 7.0) }, kSr);
    const auto needs = findNewNeeds (first, {}, hot, {});
    CHECK (find (needs, "level") != nullptr);
    CHECK (find (needs, "noise") != nullptr);
    checkNotes (needs);
}
