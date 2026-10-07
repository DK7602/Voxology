#include "Signals.h"
#include "vox/BeatKey.h"
#include "vox/Unmask.h"

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
}

TEST_CASE ("Beat key: a minor trap loop, in tune and detuned", "[beatkey]")
{
    // Am - F - C - G, 2 s each, 3 loops (24 s): the notes of C major, home A (minor).
    const std::vector<std::pair<int, bool>> prog { { 45, true }, { 41, false }, { 48, false }, { 43, false } };
    for (double detune : { 0.0, -30.0, 22.0 })
    {
        const auto r = listen (beat (prog, 2.0, 3, detune));
        INFO ("detune " << detune << ": notes of " << kNoteNames[static_cast<size_t> (r.setRoot)] << " major, home " << kNoteNames[static_cast<size_t> (r.tonic())]
              << " " << modeName (r.tonicOffset) << ", conf " << r.confidence << " tune " << r.tuneCents);
        REQUIRE (r.ready);
        CHECK (r.setRoot == 0);
        CHECK ((r.tonic() == 9 || r.tonic() == 0));   // vi - IV - I - V: heard as A minor or C major (same notes)
        CHECK (r.confidence >= 0.8);
        CHECK (std::abs (r.tuneCents - detune) < 6.0);
    }
}

TEST_CASE ("Beat key: C minor (i - VI - VII - i), E mixolydian (E - D - A - E), drums alone", "[beatkey]")
{
    const std::vector<std::pair<int, bool>> cm { { 48, true }, { 44, false }, { 46, false }, { 48, true } };
    const auto r = listen (beat (cm, 2.0, 3, 0.0));
    INFO ("notes of " << kNoteNames[static_cast<size_t> (r.setRoot)] << " major, home " << kNoteNames[static_cast<size_t> (r.tonic())] << " " << modeName (r.tonicOffset));
    REQUIRE (r.ready);
    CHECK (r.setRoot == 3);   // E flat major's notes
    CHECK (r.tonic() == 0);   // home C: C minor
    CHECK (r.confidence >= 0.8);

    const std::vector<std::pair<int, bool>> mix { { 40, false }, { 38, false }, { 45, false }, { 40, false } };
    const auto m = listen (beat (mix, 2.0, 3, 0.0));
    INFO ("mix: notes of " << kNoteNames[static_cast<size_t> (m.setRoot)] << " major, home " << kNoteNames[static_cast<size_t> (m.tonic())] << " " << modeName (m.tonicOffset));
    REQUIRE (m.ready);
    CHECK (m.setRoot == 9);   // A major's notes (D, not D#) ...
    CHECK (m.tonic() == 4);   // ... at home on E: E mixolydian

    // Drums only: no notes to go on -> never sure.
    const auto d = listen (beat (cm, 2.0, 3, 0.0, true));
    INFO ("drums: ready " << d.ready << " conf " << d.confidence);
    CHECK ((! d.ready || d.confidence < 0.5));
}

TEST_CASE ("Beat key: following keeps the beat's notes in any scale flavour", "[beatkey]")
{
    BeatKey::Result b; b.ready = true; b.setRoot = 2; b.tonicOffset = 9;   // B minor (D major's notes)
    int key = -1, scale = -1;
    followBeatKey (b, 0, key, scale);   // Chromatic -> the beat's own: B minor
    CHECK ((key == 11 && scale == 2));
    followBeatKey (b, 1, key, scale);   // Major -> D major (same notes)
    CHECK ((key == 2 && scale == 1));
    followBeatKey (b, 4, key, scale);   // Minor Pentatonic on B
    CHECK ((key == 11 && scale == 4));
    followBeatKey (b, 6, key, scale);   // Dorian -> E dorian (same notes)
    CHECK ((key == 4 && scale == 6));
    b.setRoot = 9; b.tonicOffset = 7;   // E mixolydian
    followBeatKey (b, 0, key, scale);
    CHECK ((key == 4 && scale == 8));
    followBeatKey (b, 2, key, scale);   // Minor -> F# minor (same notes)
    CHECK ((key == 6 && scale == 2));
}

TEST_CASE ("Beat key: through the link, a detuned beat tunes the vocal to it", "[beatkey]")
{
    auto& link = UnmaskLink::instance();
    link.resetForTests();
    const int beatSlot = link.claim(), vocalSlot = link.claim();
    CHECK (! link.readBeatKey (vocalSlot).present);

    // The beat (Am - F - C - G, 30 cents flat) is heard and published.
    const std::vector<std::pair<int, bool>> prog { { 45, true }, { 41, false }, { 48, false }, { 43, false } };
    const auto r = listen (beat (prog, 2.0, 2, -30.0));
    link.publishKey (beatSlot, { true, r });
    const auto bk = link.readBeatKey (vocalSlot);
    REQUIRE (bk.present);
    REQUIRE (bk.beat.ready);
    CHECK (bk.beat.setRoot == 0);
    CHECK (std::abs (bk.beat.tuneCents + 30.0) < 6.0);

    // A vocal sung on a true (A440) E3 is pulled to the beat's E3, 30 cents lower.
    PitchParams p; p.amount = 100.0; p.speedMs = 0.0;
    followBeatKey (bk.beat, 2, p.key, p.scale);
    p.tuneCents = bk.beat.tuneCents;
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

TEST_CASE ("Beat key: a note the beat leaves open is allowed both ways", "[beatkey]")
{
    // F#m - E - A - E: notes F# G# A B C# E, never D or D# (like the user's Schaf beat).
    const std::vector<std::pair<int, bool>> prog { { 42, true }, { 40, false }, { 45, false }, { 40, false } };
    const auto r = listen (beat (prog, 2.0, 3, 0.0));
    INFO ("notes of " << kNoteNames[static_cast<size_t> (r.setRoot)] << " major, home " << kNoteNames[static_cast<size_t> (r.tonic())]
          << " " << modeName (r.tonicOffset) << ", unclear " << r.unclear << ", open " << r.openNote);
    REQUIRE (r.ready);
    CHECK ((r.setRoot == 9 || r.setRoot == 4));   // A major's notes (D) or E major's (D#)
    CHECK (r.unclear);
    CHECK ((r.openNote == 2 || r.openNote == 3));
    int key = 0, scale = 0, extra = 0;
    followBeatKey (r, 2, key, scale, &extra);
    CHECK (extra == ((1 << 2) | (1 << 3)) - (1 << (r.setRoot == 9 ? 2 : 3)));   // the other one of D / D#
    // Both D and D# are kept as sung (each is its own nearest allowed note).
    CHECK (PitchCorrector::targetNote (62.1, key, scale, -1, extra) == 62);
    CHECK (PitchCorrector::targetNote (62.9, key, scale, -1, extra) == 63);
    followBeatKey (r, 4, key, scale, &extra);   // pentatonic: no open note added
    CHECK (extra == 0);
}

TEST_CASE ("Voice key: a sung D minor melody, one held note, rap glides", "[beatkey]")
{
    // Readings every 10 ms, as Pitch gives them: held notes with a 5.5 Hz vibrato (+-0.3 st), sung
    // 20 cents sharp, with short gaps between notes.
    const std::array<int, 12> melody { 62, 65, 69, 67, 65, 64, 62, 60, 62, 70, 69, 65 };   // D F A G F E D C D Bb A F
    VoiceKey vk;
    vk.reset();
    double t = 0.0;
    for (int rep = 0; rep < 6; ++rep)
        for (int note : melody)
        {
            for (int i = 0; i < 45; ++i, t += 0.01)
                vk.add (true, note + 0.2 + 0.3 * std::sin (2.0 * std::numbers::pi * 5.5 * t), 0.9, 0.01);
            for (int i = 0; i < 8; ++i, t += 0.01) vk.add (false, 0.0, 0.0, 0.01);
        }
    const auto r = vk.result();
    REQUIRE (r.ready);
    CHECK (r.setRoot == 5);              // the notes of F major = D minor
    CHECK (r.confidence >= 0.5);
    int key = 0, scale = 0, extra = 0;
    followBeatKey (r, 2, key, scale, &extra);   // Minor -> D minor
    CHECK (key == 2);
    CHECK (scale == 2);

    // One note held for a long time: lots of keys fit, so it isn't sure.
    VoiceKey one;
    one.reset();
    for (int i = 0; i < 3000; ++i) one.add (true, 67.0 + 0.2 * std::sin (0.3 * i), 0.9, 0.01);
    CHECK (one.result().confidence < 0.5);

    // Rap: the pitch keeps sliding, nothing is held: it doesn't learn from that.
    VoiceKey rap;
    rap.reset();
    for (int i = 0; i < 3000; ++i) rap.add (true, 55.0 + 4.0 * std::sin (2.0 * std::numbers::pi * 3.0 * i * 0.01), 0.9, 0.01);
    CHECK (rap.result().heardSeconds < 3.0);
    CHECK_FALSE (rap.result().ready);
}
