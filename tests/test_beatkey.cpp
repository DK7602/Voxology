#include "Signals.h"
#include "vox/BeatKey.h"

#include <catch2/catch_test_macros.hpp>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

/** A trap-style loop: a chord pad (saw-ish, 3 notes), an 808 on the chord root, kick / snare / hats.
    chords: MIDI roots + minor (true) / major; detune in cents. */
std::vector<double> beat (const std::vector<std::pair<int, bool>>& chords, double secondsPerChord, int loops, double detuneCents, bool drumsOnly = false)
{
    std::mt19937 rng (11);
    std::normal_distribution<double> g (0.0, 1.0);
    const auto per = static_cast<size_t> (secondsPerChord * kSr);
    std::vector<double> x (per * chords.size() * static_cast<size_t> (loops), 0.0);
    const double tune = std::pow (2.0, detuneCents / 1200.0);
    auto hz = [&] (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) * tune; };
    std::array<double, 3> ph {};
    double ph808 = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto& [root, minor] = chords[(i / per) % chords.size()];
        const double tIn = static_cast<double> (i % per) / kSr;
        double s = 0.0;
        if (! drumsOnly)
        {
            const std::array<int, 3> notes { root + 12, root + 12 + (minor ? 3 : 4), root + 19 };
            for (size_t k = 0; k < 3; ++k)
            {
                ph[k] += hz (notes[k] + 12) / kSr;
                for (int h = 1; h <= 6; ++h) s += 0.03 * std::sin (2 * std::numbers::pi * h * ph[k]) / h;
            }
            ph808 += hz (root) / kSr;
            const double env = std::exp (-tIn * 1.5);
            s += 0.25 * env * (std::sin (2 * std::numbers::pi * ph808) + 0.3 * std::sin (4 * std::numbers::pi * ph808));
        }
        // Drums on a 0.5 s grid: kick on beats, snare on the off-beats, hats every 1/8.
        const double tb = std::fmod (static_cast<double> (i) / kSr, 0.5);
        const double tk = std::fmod (static_cast<double> (i) / kSr, 1.0);
        if (tk < 0.15) s += 0.5 * std::exp (-tk * 30.0) * std::sin (2 * std::numbers::pi * (50.0 * tk + 80.0 * (1.0 - std::exp (-tk * 40.0)) / 40.0));
        if (tk >= 0.5 && tk < 0.65) s += 0.2 * std::exp (-(tk - 0.5) * 25.0) * g (rng);
        if (std::fmod (tb, 0.25) < 0.03) s += 0.05 * g (rng);
        x[i] = s;
    }
    return x;
}

BeatKey::Result listen (const std::vector<double>& x)
{
    BeatKey bk;
    bk.prepare (kSr);
    for (size_t s = 0; s < x.size(); s += 512) bk.process (x.data() + s, static_cast<int> (std::min<size_t> (512, x.size() - s)));
    return bk.result();
}
bool sameNotes (const KeyGuess& g, int key, bool minor)
{
    // The relative major / minor has the same notes (A minor = C major).
    const int rel = minor ? (key + 3) % 12 : (key + 9) % 12;
    return (g.key == key && g.minor == minor) || (g.key == rel && g.minor != minor);
}
}

TEST_CASE ("Beat key: a minor trap loop, in tune and detuned", "[beatkey]")
{
    // Am - F - C - G, 2 s each, 3 loops (24 s).
    const std::vector<std::pair<int, bool>> prog { { 45, true }, { 41, false }, { 48, false }, { 43, false } };
    for (double detune : { 0.0, -30.0, 22.0 })
    {
        const auto r = listen (beat (prog, 2.0, 3, detune));
        INFO ("detune " << detune << ": key " << kNoteNames[static_cast<size_t> (r.key.key)] << (r.key.minor ? " minor" : " major")
              << " conf " << r.key.confidence << " notes conf " << r.key.notesConfidence << " tune " << r.tuneCents);
        REQUIRE (r.ready);
        CHECK (sameNotes (r.key, 9, true));
        CHECK (r.key.notesConfidence >= 0.6);
        CHECK (std::abs (r.tuneCents - detune) < 6.0);
    }
}

TEST_CASE ("Beat key: C minor (i - VI - VII - i) and drums alone", "[beatkey]")
{
    const std::vector<std::pair<int, bool>> cm { { 48, true }, { 44, false }, { 46, false }, { 48, true } };
    const auto r = listen (beat (cm, 2.0, 3, 0.0));
    INFO ("key " << kNoteNames[static_cast<size_t> (r.key.key)] << (r.key.minor ? " minor" : " major") << " notes conf " << r.key.notesConfidence);
    REQUIRE (r.ready);
    CHECK (sameNotes (r.key, 0, true));
    CHECK (r.key.notesConfidence >= 0.6);

    // Drums only: no notes to go on -> never sure.
    const auto d = listen (beat (cm, 2.0, 3, 0.0, true));
    INFO ("drums: ready " << d.ready << " notes conf " << d.key.notesConfidence);
    CHECK ((! d.ready || d.key.notesConfidence < 0.5));
}

TEST_CASE ("Beat key: following keeps your scale's flavour", "[beatkey]")
{
    KeyGuess cMajor; cMajor.key = 0; cMajor.minor = false;   // = A minor's notes
    int key = -1, scale = -1;
    followBeatKey (cMajor, 0, key, scale);   // Chromatic -> the beat's own
    CHECK ((key == 0 && scale == 1));
    followBeatKey (cMajor, 4, key, scale);   // Minor Pentatonic -> on A
    CHECK ((key == 9 && scale == 4));
    followBeatKey (cMajor, 8, key, scale);   // Mixolydian -> on C
    CHECK ((key == 0 && scale == 8));
    KeyGuess fMinor; fMinor.key = 5; fMinor.minor = true;
    followBeatKey (fMinor, 1, key, scale);   // Major -> A flat major (same notes as F minor)
    CHECK ((key == 8 && scale == 1));
    followBeatKey (fMinor, 9, key, scale);   // Blues -> on F
    CHECK ((key == 5 && scale == 9));
}

#include "vox/Unmask.h"

TEST_CASE ("Beat key: through the link, a detuned beat tunes the vocal to it", "[beatkey]")
{
    auto& link = UnmaskLink::instance();
    link.resetForTests();
    const int beatSlot = link.claim(), vocalSlot = link.claim();
    CHECK (! link.readBeatKey (vocalSlot).present);

    // The beat (Am - F - C - G, 30 cents flat) is heard and published.
    const std::vector<std::pair<int, bool>> prog { { 45, true }, { 41, false }, { 48, false }, { 43, false } };
    const auto r = listen (beat (prog, 2.0, 2, -30.0));
    link.publishKey (beatSlot, { true, r.ready, r.key, r.tuneCents, r.heardSeconds });
    const auto bk = link.readBeatKey (vocalSlot);
    REQUIRE (bk.present);
    REQUIRE (bk.ready);
    CHECK (std::abs (bk.tuneCents + 30.0) < 6.0);

    // A vocal sung on a true (A440) E3 is pulled to the beat's E3, 30 cents lower.
    PitchParams p; p.amount = 100.0; p.speedMs = 0.0;
    followBeatKey (bk.key, 2, p.key, p.scale);
    p.tuneCents = bk.tuneCents;
    CHECK ((p.key == 9 && p.scale == 2));
    const double e3 = 164.81;
    std::vector<double> v (static_cast<size_t> (1.2 * kSr));
    double ph = 0.0;
    for (auto& x : v) { ph += e3 / kSr; double s = 0.0; for (int h = 1; h * e3 < 6000.0; ++h) s += std::sin (2 * std::numbers::pi * h * ph) / h; x = 0.1 * s; }
    PitchCorrector pc; pc.setParams (p); pc.prepare (kSr, 1);
    for (size_t s = 0; s < v.size(); s += 256) { double* q = v.data() + s; pc.process (&q, 1, static_cast<int> (std::min<size_t> (256, v.size() - s))); }
    // f0 of the output by zero-crossing count over 0.5 s (clean harmonic tone: one upward crossing of the fundamental-dominated wave per cycle).
    const size_t a = 30000, b = 54000;
    double xs = 0.0, firstX = -1.0, lastX = -1.0;
    for (size_t i = a + 1; i < b; ++i)
        if (v[i - 1] < 0.0 && v[i] >= 0.0)
        {
            const double t = static_cast<double> (i - 1) + v[i - 1] / (v[i - 1] - v[i]);
            if (firstX < 0.0) firstX = t;
            lastX = t; xs += 1.0;
        }
    const double got = (xs - 1.0) / ((lastX - firstX) / kSr);
    INFO ("got " << got << " Hz, want " << e3 * std::pow (2.0, -30.0 / 1200.0));
    CHECK (std::abs (1200.0 * std::log2 (got / (e3 * std::pow (2.0, -30.0 / 1200.0)))) < 5.0);

    // The beat's Voxology goes: the key goes with it.
    link.release (beatSlot);
    CHECK (! link.readBeatKey (vocalSlot).present);
    link.release (vocalSlot);
}
