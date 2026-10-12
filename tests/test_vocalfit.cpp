// Vocal Fit (v0.32): Auto-Edit hears the beat on the side-chain and reports how the finished vocal sits against it.
#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

const AutoEditReason* fitLine (const AutoEditResult& r, const std::string& control)
{
    for (const auto& f : r.fit)
        if (f.control == control) return &f;
    return nullptr;
}
}

TEST_CASE ("Vocal Fit: hears the beat and reports it; no setting changes yet", "[vocalfit]")
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
    CHECK (withBright.beat.heard);
    CHECK (withBright.beat.underVoiceSeconds >= kBeatMinSeconds);
    CHECK (withBright.beat.brightDb > withDark.beat.brightDb + 10.0);
    CHECK (withBright.beat.busyDb > 3.0);
    for (const char* c : { "Beat heard", "Vocal vs beat", "Top end (hi-hats)", "Low end (808 / kick)", "Movement", "Covers your voice" })
    {
        INFO (c);
        const auto* f = fitLine (withBright, c);
        REQUIRE (f != nullptr);
        CHECK (! f->value.empty());
        CHECK (! f->why.empty());
    }
    CHECK (fitLine (withBright, "Top end (hi-hats)")->value.rfind ("bright", 0) == 0);
    CHECK (fitLine (withDark, "Top end (hi-hats)")->value.rfind ("dark", 0) == 0);

    // Step 1 only listens: the chain is exactly the one Auto-Edit sets without a beat.
    const std::vector<std::vector<float>> slice { std::vector<float> (x.begin(), x.begin() + static_cast<std::ptrdiff_t> (3.0 * kSr)) };
    const auto a = VocalChain::render (slice, kSr, plain.params), b = VocalChain::render (slice, kSr, withBright.params);
    REQUIRE (a[0].size() == b[0].size());
    for (size_t i = 0; i < a[0].size(); ++i) REQUIRE (a[0][i] == b[0][i]);
    CHECK (plain.params.outputDb == withBright.params.outputDb);
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
