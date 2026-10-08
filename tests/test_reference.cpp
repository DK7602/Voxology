#include "Signals.h"
#include "vox/AutoEdit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

std::vector<float> through (const std::vector<float>& x, double bodyDb, double presDb)
{
    Biquad lo, pr;
    design::apply (lo, design::lowShelf (200.0, bodyDb, kSr));
    design::apply (pr, design::bell (3500.0, presDb, 0.8, kSr));
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float> (pr.process (lo.process (x[i])));
    return y;
}

/** Distance between overall tone shapes (each third-octave averaged with its neighbours as energy). */
double toneDistance (const std::vector<double>& ra, const std::vector<double>& rb)
{
    auto smooth = [] (const std::vector<double>& t)
    {
        std::vector<double> o (t.size());
        for (size_t k = 0; k < t.size(); ++k)
        {
            double e = 2.0 * std::pow (10.0, t[k] / 10.0), w = 2.0;
            if (k > 0) { e += std::pow (10.0, t[k - 1] / 10.0); w += 1.0; }
            if (k + 1 < t.size()) { e += std::pow (10.0, t[k + 1] / 10.0); w += 1.0; }
            o[k] = 10.0 * std::log10 (e / w);
        }
        return o;
    };
    const auto a = smooth (ra), b = smooth (rb);
    const auto& bands = analysisBands();
    double s = 0.0; int n = 0;
    for (size_t k = 0; k < bands.size(); ++k)
        if (bands[k] >= 160.0 && bands[k] <= 10000.0) { s += (a[k] - b[k]) * (a[k] - b[k]); ++n; }
    return std::sqrt (s / n);
}
}

TEST_CASE ("Reference Match: a brighter, thinner reference pulls the EQ that way and lands closer", "[reference]")
{
    const auto mine = testsig::vocal (kSr, 14.0, -80.0, -14.0, 3, 140.0);
    const auto refAudio = through (testsig::vocal (kSr, 14.0, -80.0, -14.0, 8, 160.0), -6.0, 6.0);
    const auto ref = analyseReference ({ refAudio }, kSr, "Bright Ref");
    REQUIRE (ref.ok);
    CHECK (ref.warning.empty());

    AutoEditSettings plain { 0, 1, 0.0, nullptr }, matched = plain;
    matched.reference = &ref;
    const auto a = autoEdit ({ mine }, kSr, plain), b = autoEdit ({ mine }, kSr, matched);
    REQUIRE (a.ok);
    REQUIRE (b.ok);
    CHECK (b.params.eq.gainDb[3] > a.params.eq.gainDb[3]);   // more presence
    CHECK (b.params.eq.gainDb[0] < a.params.eq.gainDb[0] + 0.01);   // no more body than the style gives
    CHECK (b.summary.find ("Bright Ref") != std::string::npos);

    auto dryTone = [&] (ChainParams p)
    {
        p.reverb.mix = 0.0; p.delay.mix = 0.0; p.doubler.amount = 0.0;
        return analyseVocal (VocalChain::render ({ mine }, kSr, p), kSr).bandDb;
    };
    const double before = toneDistance (dryTone (a.params), ref.bandDb), after = toneDistance (dryTone (b.params), ref.bandDb);
    INFO ("style " << before << " dB, matched " << after << " dB");
    CHECK (after < 0.8 * before);
    for (const auto& r : b.reasons) CHECK (! r.why.empty());
}

TEST_CASE ("Reference Match: a full song gets a warning; too little voice is refused", "[reference]")
{
    auto song = testsig::vocal (kSr, 14.0, -80.0, -14.0);
    for (size_t i = 0; i < song.size(); ++i)   // an 808 under it
        song[i] += static_cast<float> (0.4 * std::sin (2.0 * std::numbers::pi * 50.0 * static_cast<double> (i) / kSr));
    const auto full = analyseReference ({ song }, kSr, "Song");
    CHECK (full.ok);
    CHECK_FALSE (full.warning.empty());
    const auto r = autoEdit ({ testsig::vocal (kSr, 14.0) }, kSr, { 0, 1, 0.0, &full });
    bool note = false;
    for (const auto& n : r.notes) note |= n.rfind ("REFERENCE IS A FULL SONG", 0) == 0;
    CHECK (note);

    const auto tiny = analyseReference ({ testsig::vocal (kSr, 2.0) }, kSr, "Tiny");
    CHECK_FALSE (tiny.ok);
    CHECK_FALSE (tiny.problem.empty());
}

TEST_CASE ("Reference Match keeps each band on its job (no stacking, nothing under the voice or in the s zone)", "[reference]")
{
    // A reference far from the take (dark, boomy, a deeper singer): the fit must still use sane moves.
    const auto mine = testsig::vocal (kSr, 14.0, -80.0, -14.0, 3, 180.0);
    const auto refAudio = through (testsig::vocal (kSr, 14.0, -80.0, -14.0, 8, 110.0), 9.0, -8.0);
    const auto ref = analyseReference ({ refAudio }, kSr, "Dark Ref");
    REQUIRE (ref.ok);
    CHECK (ref.f0Median > 0.0);
    AutoEditSettings s { 1, 1, 0.0, &ref };
    const auto r = autoEdit ({ mine }, kSr, s);
    REQUIRE (r.ok);
    const auto& e = r.params.eq;
    double total = 0.0;
    for (double g : e.gainDb) { CHECK (std::abs (g) <= 8.0); total += std::abs (g); }
    INFO ("EQ " << e.gainDb[0] << "@" << e.freqHz[0] << " " << e.gainDb[1] << "@" << e.freqHz[1] << " " << e.gainDb[2] << "@" << e.freqHz[2] << " "
                << e.gainDb[3] << "@" << e.freqHz[3] << " " << e.gainDb[4] << "@" << e.freqHz[4] << ", low cut " << r.params.cleanup.lowCutHz);
    CHECK (total < 26.0);
    if (e.gainDb[0] != 0.0) CHECK (e.freqHz[0] >= std::max (1.3 * r.params.cleanup.lowCutHz, r.analysis.f0Low) - 1.0);
    if (e.gainDb[3] != 0.0) CHECK (e.freqHz[3] <= 5000.0);
    if (e.gainDb[4] != 0.0) CHECK (e.freqHz[4] >= 8000.0);
    CHECK (r.params.cleanup.lowCutHz <= 0.86 * r.analysis.f0Low + 5.0);
}

TEST_CASE ("Built-in references: six pro groups + a trap vocal, usable by Auto-Edit", "[reference]")
{
    const auto& lib = builtinReferences();
    REQUIRE (lib.size() == 7);
    const auto mine = testsig::vocal (kSr, 14.0, -80.0, -14.0, 3, 150.0);
    for (const auto& ref : lib)
    {
        INFO (ref.name);
        CHECK (ref.ok);
        CHECK (ref.builtin);
        CHECK (! ref.about.empty());
        CHECK (ref.bandDb.size() == analysisBands().size());
        CHECK (ref.f0Median > 100.0);
        CHECK (ref.sibilanceDb > -15.0);
        CHECK ((ref.microDynDb > 2.0 || ref.microDynDb == 0.0));
        AutoEditSettings s { 1, 1, 0.0, &ref };
        const auto r = autoEdit ({ mine }, kSr, s);
        REQUIRE (r.ok);
        CHECK (r.summary.find (ref.name) != std::string::npos);
        for (double g : r.params.eq.gainDb) CHECK (std::abs (g) <= 8.0);
    }
}

TEST_CASE ("A wet reference (echoes baked in) gives its tone, not 60 % reverb and no compression", "[reference]")
{
    // The reference: a vocal with loud echoes (every 300 ms, -4 dB per repeat) filling its gaps, like most online acapellas.
    auto wet = testsig::vocal (kSr, 14.0, -80.0, -14.0, 8, 150.0);
    const auto d = static_cast<size_t> (0.3 * kSr);
    for (size_t i = d; i < wet.size(); ++i) wet[i] += 0.63f * wet[i - d];
    const auto ref = analyseReference ({ wet }, kSr, "Wet Ref");
    REQUIRE (ref.ok);
    const auto mine = testsig::vocal (kSr, 14.0, -80.0, -14.0, 3, 140.0);
    const auto plain = autoEdit ({ mine }, kSr, { 1, 1, 0.0, nullptr });
    const auto r = autoEdit ({ mine }, kSr, { 1, 1, 0.0, &ref });
    REQUIRE (r.ok);
    INFO ("ref tail " << ref.tailDb << " punch " << ref.microDynDb << " s " << ref.sibilanceDb << "; reverb " << r.params.reverb.mix << " % (style " << plain.params.reverb.mix
          << " %), comp threshold " << r.params.comp.thrDb << " (style " << plain.params.comp.thrDb << ")");
    CHECK (r.params.reverb.mix <= 40.0);
    if (ref.tailDb > -18.0 || ref.microDynDb > 5.5)
    {
        CHECK (r.params.reverb.mix == Catch::Approx (plain.params.reverb.mix).margin (0.5));   // space follows the style
        CHECK (r.params.comp.thrDb == Catch::Approx (plain.params.comp.thrDb).margin (0.5));
        bool note = false;
        for (const auto& n : r.notes) note |= n.rfind ("WET REFERENCE", 0) == 0;
        CHECK (note);
    }
}
