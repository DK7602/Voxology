#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <set>
#include <thread>

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

TEST_CASE ("Auto-Edit's De-Esser keeps the s on target at the end of the inserts (after the compressor)", "[autoedit][deess]")
{
    // The rider and compressor bring the quieter "s" up; Auto-Edit checks again after them.
    for (double sibDb : { -10.0, -6.0, -2.0 })
    {
        const auto x = testsig::vocal (kSr, 14.0, -70.0, sibDb, 5);
        const auto r = autoEdit ({ x }, kSr, { 1, 1, 0.0 });   // Rap: the "s" may sit up to -4 dB vs the voice
        REQUIRE (r.ok);
        ChainParams p = r.params;
        p.doubler.amount = 0.0; p.delay.mix = 0.0; p.reverb.mix = 0.0;
        const auto y = VocalChain::render ({ x }, kSr, p);
        const double end = analyseVocal ({ y[0] }, kSr).sibilanceDb;
        INFO ("s at " << sibDb << " dB in: de-esser " << p.deEsser.amount << " %, s at the end " << end << " dB");
        CHECK (end <= -4.0 + 1.5);
        checkNotes (r);
    }
}

TEST_CASE ("Auto-Edit doesn't flatten a vocal that's already compressed (a second pass keeps the punch)", "[autoedit][comp]")
{
    // First pass: a raw take, compressed to the style's amount. Second pass on that result: the compressor must
    // back off instead of squeezing it again (punch floor: finished pro vocals' bottom quarter).
    const auto x = testsig::vocal (kSr, 14.0, -70.0, -14.0, 9);
    auto once = [] (const std::vector<float>& in)
    {
        const auto r = autoEdit ({ in }, kSr, { 1, 1, 0.0 });
        ChainParams p = r.params;
        p.doubler.amount = 0.0; p.delay.mix = 0.0; p.reverb.mix = 0.0;
        return VocalChain::render ({ in }, kSr, p)[0];
    };
    const auto first = once (x);
    const auto second = once (first);
    const double p1 = analyseVocal ({ first }, kSr).microDynDb, p2 = analyseVocal ({ second }, kSr).microDynDb;
    INFO ("punch: raw " << analyseVocal ({ x }, kSr).microDynDb << ", first pass " << p1 << ", second pass " << p2 << " dB");
    CHECK (p1 > 2.2);   // v0.19.3 flattened this take to 1.7 dB on the first pass
    CHECK (p2 > 2.2);
    CHECK (p2 > p1 - 0.6);
}

TEST_CASE ("Auto-Edit stops early when the plug-in closes (the cancel flag)", "[autoedit]")
{
    // Closing a project waited on a running Auto-Edit (seconds of work, more at high sample rates).
    const auto x = testsig::vocal (kSr, 14.0, -58.0, -1.0);
    std::atomic<bool> stop { true };
    AutoEditSettings s { 0, 1, 140.0 };
    s.cancel = &stop;
    auto t0 = std::chrono::steady_clock::now();
    const auto early = autoEdit ({ x }, kSr, s);
    CHECK (! early.ok);
    CHECK (std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() < 2.0);

    // Set while it works: it returns soon after.
    stop = false;
    std::thread closer ([&stop] { std::this_thread::sleep_for (std::chrono::milliseconds (400)); stop = true; });
    t0 = std::chrono::steady_clock::now();
    const auto mid = autoEdit ({ x }, kSr, s);
    const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    closer.join();
    CHECK (! mid.ok);
    CHECK (took < 6.0);

    // A reference stops too.
    stop = true;
    const auto ref = analyseReference ({ x }, kSr, "Ref", &stop);
    CHECK (! ref.ok);
}

TEST_CASE ("Auto-Edit: a note that has you change the vocal's level says to run Auto-Edit again", "[autoedit]")
{
    // The gate, Rider and Compressor thresholds are levels: after the advised clip-gain cut (TOO HOT) the gate chopped
    // words and the compressor stopped working. Every such note ends with "press AUTO-EDIT again".
    auto hasNote = [] (const AutoEditResult& r, const char* title)
    {
        for (const auto& n : r.notes) if (n.rfind (title, 0) == 0) return n;
        return std::string();
    };
    auto checkAgain = [] (const AutoEditResult& r)
    {
        for (const auto& n : r.notes)
        {
            INFO (n);
            const bool changesLevel = n.find ("clip gain") != std::string::npos || n.find ("gain knob") != std::string::npos
                                      || n.find ("interface gain") != std::string::npos || n.find ("{daw:toMono}") != std::string::npos;
            if (changesLevel) CHECK (n.find ("press AUTO-EDIT again") != std::string::npos);
        }
    };

    auto hot = testsig::vocal (kSr, 14.0, -70.0, -14.0, 5);
    for (auto& v : hot) v *= 4.0f;   // peaks over full scale (a float mix doesn't clip)
    const auto rh = autoEdit ({ hot }, kSr, { 0, 1, 0.0 });
    REQUIRE (rh.ok);
    CHECK (rh.analysis.peakDb > 0.0);
    CHECK (hasNote (rh, "TOO HOT").find ("press AUTO-EDIT again") != std::string::npos);
    checkAgain (rh);
    checkNotes (rh);

    // Quiet: Output keeps the level it came in at, so the note mustn't promise the level back.
    auto quiet = testsig::vocal (kSr, 14.0, -95.0, -14.0, 5);
    for (auto& v : quiet) v *= 0.05f;
    const auto rq = autoEdit ({ quiet }, kSr, { 0, 1, 0.0 });
    REQUIRE (rq.ok);
    CHECK (rq.analysis.peakDb < -24.0);
    CHECK (rq.outputLufs == Approx (rq.analysis.inputLufs).margin (1.0));
    const auto qn = hasNote (rq, "QUIET RECORDING");
    REQUIRE (! qn.empty());
    CHECK (qn.find ("adds the level back") == std::string::npos);
    CHECK (qn.find ("press AUTO-EDIT again") != std::string::npos);
    checkAgain (rq);
    checkNotes (rq);

    // One-sided: folding to mono changes the level too.
    const auto x = testsig::vocal (kSr, 14.0, -70.0, -14.0, 5);
    const auto ro = autoEdit ({ x, std::vector<float> (x.size(), 0.0f) }, kSr, { 0, 1, 0.0 });
    REQUIRE (ro.ok);
    CHECK (hasNote (ro, "ONE-SIDED").find ("press AUTO-EDIT again") != std::string::npos);
    checkAgain (ro);
    checkNotes (ro);
}

TEST_CASE ("Auto-Edit: the too-little-voice tip names the real minimum; uneven stereo channels are fine", "[autoedit]")
{
    const auto r = autoEdit ({ testsig::vocal (kSr, 2.0) }, kSr, {});
    REQUIRE_FALSE (r.ok);
    CHECK (r.tip.find ("at least 3 seconds") != std::string::npos);

    // A right channel shorter than the left (v0.51 read past its end).
    const auto x = testsig::vocal (kSr, 6.0);
    const std::vector<float> shorter (x.begin(), x.begin() + static_cast<std::ptrdiff_t> (x.size() / 2));
    const auto a = analyseVocal ({ x, shorter }, kSr);
    CHECK (a.stereo);
    CHECK (std::isfinite (a.voiceRmsDb));
}

TEST_CASE ("Auto-Edit: digital silence after phrases reads as dry, not unknown", "[autoedit]")
{
    // Gaps of exact silence (a gated or edited vocal): the tail read -216 dB and counted as "unknown".
    const auto x = testsig::vocal (kSr, 14.0, -400.0, -14.0, 5);
    const auto a = analyseVocal ({ x }, kSr);
    CHECK (a.tailDb == Approx (-80.0));
    const auto r = autoEdit ({ x }, kSr, { 1, 1, 0.0 });
    REQUIRE (r.ok);
    for (const auto& reason : r.reasons)
        if (reason.module == "cleanup" && reason.control == "Room")
        {
            INFO (reason.why);
            CHECK (reason.why.find ("almost nothing rings on") != std::string::npos);
        }
}

TEST_CASE ("Auto-Edit Pitch: rap stays untuned (pitched but nothing held), singing gets tuned, Robot keeps its effect", "[autoedit][pitch]")
{
    // Real rap reads 42 - 95 % pitched but only 9 - 28 % held notes: v0.51 tuned it by the pitched share (Rap style:
    // Natural 70 %, Trap Lead: Classic 10 ms / 100 %).
    auto pitchWhy = [] (const AutoEditResult& r, const char* control)
    {
        for (const auto& x : r.reasons) if (x.module == "pitch" && x.control == control) return x.why;
        return std::string();
    };
    auto note = [] (const AutoEditResult& r, const char* title)
    {
        for (const auto& n : r.notes) if (n.rfind (title, 0) == 0) return true;
        return false;
    };
    const auto rap = testsig::rapped (kSr, 14.0);
    const auto a = analyseVocal ({ rap }, kSr);
    INFO ("rap: pitched " << a.pitchedShare << " %, held " << a.heldShare << " %");
    REQUIRE (a.pitchedShare > 50.0);
    REQUIRE (a.heldShare < kRapHeldShare);
    for (int style : { 0, 1, 2 })
    {
        const auto r = autoEdit ({ rap }, kSr, { style, 1, 0.0 });
        REQUIRE (r.ok);
        INFO (kStyleNames[static_cast<size_t> (style)]);
        CHECK (r.params.pitch.amount == 0.0);
        CHECK (pitchWhy (r, "Amount").find ("held notes") != std::string::npos);
        CHECK (note (r, "PITCH OFF FOR RAP") == (style == 0));   // Trap Lead: how to get the robotic sound anyway
        checkNotes (r);
    }
    CHECK (autoEdit ({ rap }, kSr, { 3, 1, 0.0 }).params.pitch.amount == 100.0);   // Robot: the effect is the point

    const auto sung = testsig::sung (kSr, 14.0, 150.0);   // D3 + 37 cents, held notes
    const auto s = analyseVocal ({ sung }, kSr);
    REQUIRE (s.heldShare > 50.0);
    const auto trap = autoEdit ({ sung }, kSr, { 0, 1, 0.0 });
    CHECK (trap.params.pitch.amount == 100.0);
    CHECK (trap.params.pitch.mode == kPitchClassic);
    CHECK_FALSE (note (trap, "PITCH OFF FOR RAP"));
    CHECK (autoEdit ({ sung }, kSr, { 1, 1, 0.0 }).params.pitch.amount > 0.0);

    // "You sing N cents away" is measured from the notes Pitch will use: the beat's scale, or all 12 (Chromatic).
    for (int scale : { 2, 0 })
    {
        AutoEditSettings st { 4, 1, 0.0 };
        st.beatKeyKnown = true;
        st.beatKeyNote = 2;
        st.beatScale = scale;
        st.beatKeyName = scale == 2 ? "D minor" : "D chromatic";
        const auto r = autoEdit ({ sung }, kSr, st);
        REQUIRE (r.params.pitch.scale == scale);
        double off = 0.0;
        for (double m : r.analysis.pitchMidi) off += std::abs (m - PitchCorrector::targetNote (m, 2, scale, -1));
        off = 100.0 * off / static_cast<double> (r.analysis.pitchMidi.size());
        const auto why = pitchWhy (r, "Amount");
        INFO (why << " (expected " << off << " cents)");
        CHECK (why.find ("You sing on average " + std::to_string (std::lround (off)) + " cents away") != std::string::npos);
        CHECK (why.find (scale == 2 ? "the nearest note of D minor" : "the nearest of the 12 notes") != std::string::npos);
    }
}
