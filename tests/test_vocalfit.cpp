// Vocal Fit: Auto-Edit hears the beat on the side-chain (v0.32), nudges Tone EQ and Compressor for it and offers
// FIT LEVEL (v0.33), and reports how the finished vocal sits against it.
#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numbers>
#include <random>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

/** A stand-in beat at 140 BPM: a decaying 55 Hz kick / 808 on every beat, a chord pad (harmonics up to ~4 kHz) underneath,
    hi-hats (noise over 6 kHz) on the 8ths at hatDb, and optionally a loud 3.15 kHz lead line at leadDb. levelDb scales it all. */
std::vector<float> beat (double seconds, double hatDb, double levelDb = 0.0, double leadDb = -120.0, unsigned seed = 7)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> white (0.0, 1.0);
    const size_t n = static_cast<size_t> (kSr * seconds);
    const double beatS = 60.0 / 140.0;
    Biquad h1, h2;
    design::apply (h1, design::butterworth (true, 6000.0, kSr));
    design::apply (h2, design::butterworth (true, 6000.0, kSr));
    std::vector<float> out (n);
    const double hat = std::pow (10.0, hatDb / 20.0), lead = std::pow (10.0, leadDb / 20.0), g = std::pow (10.0, levelDb / 20.0);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        const double tb = std::fmod (t, beatS), t8 = std::fmod (t, beatS / 2.0);
        double x = 0.5 * std::exp (-tb / 0.25) * std::sin (2.0 * std::numbers::pi * 55.0 * tb);
        double pad = 0.0;
        for (double f : { 220.0, 261.6, 329.6 })
            for (int h = 1; h * f < 4000.0; ++h) pad += std::sin (2.0 * std::numbers::pi * h * f * t) / h;
        x += 0.02 * pad;
        x += hat * std::exp (-t8 / 0.03) * h2.process (h1.process (white (rng)));
        x += lead * std::sin (2.0 * std::numbers::pi * 3150.0 * t);
        out[i] = static_cast<float> (g * x);
    }
    return out;
}

/** A steady, wall-of-sound beat: a sustained chord pad and constant noise, no drums (its level barely moves). */
std::vector<float> steadyBeat (double seconds, unsigned seed = 9)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> white (0.0, 1.0);
    std::vector<float> out (static_cast<size_t> (kSr * seconds));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        double pad = 0.0;
        for (double f : { 55.0, 110.0, 220.0, 261.6, 329.6 })
            for (int h = 1; h * f < 4000.0; ++h) pad += std::sin (2.0 * std::numbers::pi * h * f * t) / h;
        out[i] = static_cast<float> (0.03 * pad + 0.01 * white (rng));
    }
    return out;
}

bool sameChain (const AutoEditResult& a, const AutoEditResult& b, const std::vector<float>& x)
{
    const std::vector<std::vector<float>> slice { std::vector<float> (x.begin(), x.begin() + static_cast<std::ptrdiff_t> (3.0 * kSr)) };
    const auto ya = VocalChain::render (slice, kSr, a.params), yb = VocalChain::render (slice, kSr, b.params);
    if (ya[0].size() != yb[0].size() || a.params.outputDb != b.params.outputDb) return false;
    for (size_t i = 0; i < ya[0].size(); ++i) if (ya[0][i] != yb[0][i]) return false;
    return true;
}

const AutoEditReason* fitLine (const AutoEditResult& r, const std::string& control)
{
    for (const auto& f : r.fit)
        if (f.control == control) return &f;
    return nullptr;
}
}

TEST_CASE ("Vocal Fit: hears the beat and reports it", "[vocalfit]")
{
    const auto x = testsig::vocal (kSr, 14.0);
    const auto bright = beat (14.0, -12.0), dark = beat (14.0, -60.0);
    AutoEditSettings s { 0, 1, 140.0 };
    const auto plain = autoEdit ({ x }, kSr, s);
    s.beat = &bright;
    const auto withBright = autoEdit ({ x }, kSr, s);
    s.beat = &dark;
    const auto withDark = autoEdit ({ x }, kSr, s);
    REQUIRE (plain.ok);
    REQUIRE (withBright.ok);
    REQUIRE (withDark.ok);

    // It heard the beat under the voice, and tells a bright beat from a dark one.
    CHECK (! plain.beat.heard);
    CHECK (plain.fit.empty());
    CHECK (plain.fitLevelDb == 0.0);
    CHECK (withBright.beat.heard);
    CHECK (withBright.beat.underVoiceSeconds >= kBeatMinSeconds);
    CHECK (withBright.beat.brightDb > withDark.beat.brightDb + 10.0);
    CHECK (withBright.beat.busyDb > 3.0);
    for (const char* c : { "Beat heard", "Vocal vs beat", "Top end (hi-hats)", "Low end (808 / kick)", "Movement", "Tone for this beat",
                           "Compressor for this beat", "Covers your voice" })
    {
        INFO (c);
        const auto* f = fitLine (withBright, c);
        REQUIRE (f != nullptr);
        CHECK (! f->value.empty());
        CHECK (! f->why.empty());
    }
    CHECK (fitLine (withBright, "Top end (hi-hats)")->value.rfind ("bright", 0) == 0);
    CHECK (fitLine (withDark, "Top end (hi-hats)")->value.rfind ("dark", 0) == 0);
}

TEST_CASE ("Vocal Fit: nothing heard = exactly the chain without a beat", "[vocalfit]")
{
    const auto x = testsig::vocal (kSr, 14.0);
    std::vector<float> tooShort (x.size(), 0.0f);
    const auto b = beat (2.0, -12.0);
    std::copy (b.begin(), b.end(), tooShort.begin());   // 2 s of beat: under kBeatMinSeconds
    AutoEditSettings s { 0, 1, 140.0 };
    const auto none = autoEdit ({ x }, kSr, s);
    s.beat = &tooShort;
    const auto r = autoEdit ({ x }, kSr, s);
    REQUIRE (r.ok);
    CHECK (! r.beat.heard);
    CHECK (r.fitPunchDb == 0.0);
    CHECK (r.fitToneDb == std::array<double, 3> {});
    CHECK (sameChain (none, r, x));
}

TEST_CASE ("Vocal Fit: Tone EQ target follows the beat's presence, a little", "[vocalfit]")
{
    const auto x = testsig::vocal (kSr, 14.0);
    const auto plain = beat (14.0, -30.0), lead = beat (14.0, -30.0, 0.0, -20.0);
    AutoEditSettings s { 0, 1, 140.0 };
    s.beat = &plain;
    const auto rp = autoEdit ({ x }, kSr, s);
    s.beat = &lead;
    const auto rl = autoEdit ({ x }, kSr, s);
    REQUIRE (rl.beat.heard);
    INFO ("presence target move: " << rp.fitToneDb[1] << " -> " << rl.fitToneDb[1]);
    CHECK (rl.fitToneDb[1] > rp.fitToneDb[1] + 0.5);   // more presence in the beat: a little more in the vocal, not less
    for (const auto* r : { &rp, &rl })
        for (double d : r->fitToneDb) CHECK (std::abs (d) <= 1.5);
}

TEST_CASE ("Vocal Fit: punchy beat keeps punch, steady beat smooths more, never the wrong way", "[vocalfit]")
{
    const auto x = testsig::vocal (kSr, 14.0);
    const auto punchy = beat (14.0, -12.0), steady = steadyBeat (14.0);
    AutoEditSettings s { 0, 1, 140.0 };
    const auto none = autoEdit ({ x }, kSr, s);
    s.beat = &punchy;
    const auto rp = autoEdit ({ x }, kSr, s);
    s.beat = &steady;
    const auto rs = autoEdit ({ x }, kSr, s);
    REQUIRE (rp.beat.heard);
    REQUIRE (rs.beat.heard);
    INFO ("busy " << rp.beat.busyDb << " / " << rs.beat.busyDb << ", punch moves " << rp.fitPunchDb << " / " << rs.fitPunchDb
                  << ", leveler " << none.params.comp.thrDb << " / " << rp.params.comp.thrDb << " / " << rs.params.comp.thrDb);
    CHECK (rp.beat.busyDb > 9.0);
    CHECK (rs.beat.busyDb < 4.9);
    CHECK (rp.fitPunchDb >= 0.0);
    CHECK (rp.fitPunchDb <= 1.0 + 0.3);
    CHECK (rs.fitPunchDb <= 0.0);
    CHECK (rs.fitPunchDb >= -0.75 - 0.3);
}

TEST_CASE ("Vocal Fit: the level reading follows the beat's level, its character doesn't", "[vocalfit]")
{
    const auto x = testsig::vocal (kSr, 14.0);
    const auto loud = beat (14.0, -12.0, 0.0), quiet = beat (14.0, -12.0, -6.0);
    AutoEditSettings s { 0, 1, 140.0 };
    s.beat = &loud;
    const auto rl = autoEdit ({ x }, kSr, s);
    s.beat = &quiet;
    const auto rq = autoEdit ({ x }, kSr, s);
    REQUIRE (rl.beat.heard);
    REQUIRE (rq.beat.heard);
    CHECK (rq.beat.vocalOverBeatDb - rl.beat.vocalOverBeatDb == Approx (6.0).margin (0.3));
    // FIT LEVEL: the Output move to sit 2.1 dB under the beat (pro median), at most 6 dB, in 0.5 dB steps.
    for (const auto* r : { &rl, &rq })
    {
        const double full = std::round (2.0 * (-2.1 - r->beat.vocalOverBeatDb)) / 2.0;
        CHECK (r->fitLevelFullDb == Approx (std::abs (full) >= 1.0 ? full : 0.0));
        CHECK (r->fitLevelDb == Approx (std::clamp (r->fitLevelFullDb, -kFitLevelMaxDb, kFitLevelMaxDb)));
    }
    CHECK (rq.fitLevelFullDb < rl.fitLevelFullDb);   // a quieter beat: the vocal comes down more
    CHECK (rq.beat.brightDb == Approx (rl.beat.brightDb).margin (0.2));
    CHECK (rq.beat.lowDb == Approx (rl.beat.lowDb).margin (0.2));
    CHECK (rq.beat.busyDb == Approx (rl.beat.busyDb).margin (0.3));
    for (size_t k = 0; k < rl.beat.overDb.size(); ++k)
        CHECK (rq.beat.overDb[k] == Approx (rl.beat.overDb[k]).margin (0.2));
}

TEST_CASE ("Vocal Fit: finds where the beat covers the voice", "[vocalfit]")
{
    const auto x = testsig::vocal (kSr, 14.0);
    const auto plain = beat (14.0, -30.0), lead = beat (14.0, -30.0, 0.0, -14.0);
    AutoEditSettings s { 0, 1, 140.0 };
    s.beat = &plain;
    const auto rp = autoEdit ({ x }, kSr, s);
    s.beat = &lead;
    const auto rl = autoEdit ({ x }, kSr, s);
    REQUIRE (rl.beat.heard);
    const auto* f = fitLine (rl, "Covers your voice");
    REQUIRE (f != nullptr);
    INFO (f->value);
    CHECK ((f->value == "around 3.1 kHz" || f->value == "around 3.2 kHz" || f->value == "around 2.5 kHz" || f->value == "around 4.0 kHz"));
    CHECK (fitLine (rp, "Covers your voice")->value != f->value);
}

TEST_CASE ("Vocal Fit: a silent side-chain gets a plan; no side-chain gets a suggestion", "[vocalfit]")
{
    const auto x = testsig::vocal (kSr, 14.0);
    const std::vector<float> silent (x.size(), 0.0f);
    AutoEditSettings s { 0, 1, 140.0 };
    const auto none = autoEdit ({ x }, kSr, s);
    s.beat = &silent;
    const auto r = autoEdit ({ x }, kSr, s);
    REQUIRE (r.ok);
    CHECK (r.fit.empty());
    bool noted = false;
    for (const auto& n : r.notes)
        if (n.rfind ("BEAT SILENT: ", 0) == 0)
        {
            noted = true;
            CHECK (n.find ("\nNEED: Optional") != std::string::npos);   // a calm card: some hosts switch the side-chain on with nothing routed
            CHECK (n.find ("\nSTEP: ") != std::string::npos);
        }
    CHECK (noted);
    bool suggested = false;
    for (const auto& t : none.suggestions) suggested = suggested || t.find ("side-chain") != std::string::npos;
    CHECK (suggested);
}
