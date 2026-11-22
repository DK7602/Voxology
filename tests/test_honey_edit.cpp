// Honey Tune, Beta 41: a note's level and mute, the notes switched into / out of the key and the tuning (KeyGrid), the
// "notes to check" finder, and the beat's tuning from Voxology on the track.
#include "HoneyView.h"
#include "Signals.h"
#include "vox/HoneyLink.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <new>

using namespace honeyui;
using Catch::Approx;

namespace {
constexpr double kSr = 44100.0;

struct Sung { double midi, seconds, gap, driftCents = 0.0; };

/** A sung line (a harmonic voice with a formant); driftCents: the note rises this far from its start to its end. */
std::vector<float> sing (const std::vector<Sung>& line)
{
    std::vector<float> x;
    vox::Biquad formant;
    vox::design::apply (formant, vox::design::bell (700.0, 9.0, 2.0, kSr));
    double ph = 0.0;
    for (const auto& s : line)
    {
        const auto n = static_cast<size_t> (s.seconds * kSr);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSr;
            const double env = std::min ({ 1.0, t / 0.02, (s.seconds - t) / 0.02 });
            const double f = 440.0 * std::pow (2.0, (s.midi - 69.0 + s.driftCents * (t / s.seconds - 0.5) / 100.0) / 12.0);
            ph += f / kSr;
            double v = 0.0;
            for (int h = 1; h * f < 8000.0; ++h) v += std::sin (2 * std::numbers::pi * h * ph) / h;
            x.push_back (static_cast<float> (0.1 * env * formant.process (v)));
        }
        for (size_t i = 0; i < static_cast<size_t> (s.gap * kSr); ++i) x.push_back (static_cast<float> (formant.process (0.0)));
    }
    return x;
}

double rms (const std::vector<float>& x, double from, double to)
{
    double e = 0.0;
    const auto a = static_cast<size_t> (from), b = std::min (x.size(), static_cast<size_t> (to));
    for (size_t i = a; i < b; ++i) e += static_cast<double> (x[i]) * x[i];
    return std::sqrt (e / static_cast<double> (std::max<size_t> (1, b - a)));
}

double biggestStep (const std::vector<float>& x, double from, double to)
{
    double m = 0.0;
    for (auto i = static_cast<size_t> (std::max (1.0, from)); i < std::min (x.size(), static_cast<size_t> (to)); ++i)
        m = std::max (m, std::abs (static_cast<double> (x[i]) - x[i - 1]));
    return m;
}

vox::honey::Note note (double pitch, double startSeconds, double seconds = 0.4)
{
    vox::honey::Note n;
    n.pitch = n.target = pitch;
    n.start = startSeconds * 48000.0;
    n.end = n.start + seconds * 48000.0;
    return n;
}

NoteView viewOf (const vox::honey::Note& n)
{
    NoteView v;
    v.note = n;
    return v;
}

struct Fresh
{
    Fresh() : mem (::operator new (vox::honeylink::Link::bytes(), std::align_val_t { 64 })) { std::memset (mem, 0, vox::honeylink::Link::bytes()); link = std::make_unique<vox::honeylink::Link> (mem); }
    ~Fresh() { link.reset(); ::operator delete (mem, std::align_val_t { 64 }); }
    void* mem;
    std::unique_ptr<vox::honeylink::Link> link;
};
}

TEST_CASE ("Honey Tune: a note made louder / quieter / muted, eased in and out; the other notes exactly as they were", "[honey][level]")
{
    const auto x = sing ({ { 60.0, 0.5, 0.3 }, { 64.0, 0.5, 0.3 }, { 67.0, 0.5, 0.2 } });
    const auto t = vox::honey::analyse (x, kSr, false);
    const auto notes = vox::honey::findNotes (t);
    REQUIRE (notes.size() == 3);
    const auto base = vox::honey::render (x, kSr, t, notes);
    const auto& n = notes[1];
    const double mid0 = n.start + 0.3 * (n.end - n.start), mid1 = n.end - 0.3 * (n.end - n.start);

    auto louder = notes;
    louder[1].gainDb = 6.0;
    const auto up = vox::honey::render (x, kSr, t, louder);
    CHECK (20.0 * std::log10 (rms (up, mid0, mid1) / rms (base, mid0, mid1)) == Approx (6.0).margin (0.05));
    // The notes around it: untouched, sample for sample (the ease stays in the gaps).
    for (size_t i = 0; i < x.size(); ++i)
        if (static_cast<double> (i) < n.start - 0.012 * kSr || static_cast<double> (i) > n.end + 0.012 * kSr)
            REQUIRE (up[i] == base[i]);
    // Eased over 10 ms: no step bigger than the voice's own at the note's ends.
    CHECK (biggestStep (up, n.start - 0.015 * kSr, n.start + 0.005 * kSr) <= 2.05 * biggestStep (base, n.start - 0.015 * kSr, n.start + 0.005 * kSr) + 1.0e-6);

    auto quiet = notes;
    quiet[1].gainDb = -12.0;
    const auto down = vox::honey::render (x, kSr, t, quiet);
    CHECK (20.0 * std::log10 (rms (down, mid0, mid1) / rms (base, mid0, mid1)) == Approx (-12.0).margin (0.05));

    auto muted = notes;
    muted[1].muted = true;
    const auto off = vox::honey::render (x, kSr, t, muted);
    CHECK (rms (off, n.start + 0.001 * kSr, n.end - 0.001 * kSr) < 1.0e-6);
    CHECK (rms (off, notes[0].start, notes[0].end) == Approx (rms (base, notes[0].start, notes[0].end)));
    CHECK (biggestStep (off, n.end - 0.005 * kSr, n.end + 0.02 * kSr) <= biggestStep (base, n.end - 0.005 * kSr, n.end + 0.02 * kSr) + 1.0e-6);

    // An edit that only changes a level re-renders just around that note, the same as rendering it all.
    double from = 0.0, to = 0.0;
    REQUIRE (vox::honey::changedRegion (notes, louder, static_cast<double> (x.size()), kSr, from, to));
    CHECK (to > from);
    CHECK (from <= n.start - 0.01 * kSr);
    CHECK (to >= n.end + 0.01 * kSr);
    auto part = base;
    vox::honey::renderPart (x, kSr, t, louder, part, from, to);
    double worst = 0.0;
    for (size_t i = 0; i < x.size(); ++i) worst = std::max (worst, std::abs (static_cast<double> (part[i]) - up[i]));
    CHECK (worst < 1.0e-4);
}

TEST_CASE ("Honey Tune: level, mute and checked in the edits (made safe, saved apart from what you hear)", "[honey][level]")
{
    NoteEdit e;
    CHECK (e.isDefault());
    CHECK (e.isBlank());
    e.checked = true;
    CHECK (e.isDefault());     // nothing you'd hear
    CHECK_FALSE (e.isBlank()); // ... but saved
    e = {};
    e.gainDb = 3.0;
    CHECK_FALSE (e.isDefault());
    e = {};
    e.muted = true;
    CHECK_FALSE (e.isDefault());

    NoteEdit wild;
    wild.gainDb = std::numeric_limits<double>::infinity();
    CHECK (sanitised (wild).gainDb == 0.0);
    wild.gainDb = 80.0;
    CHECK (sanitised (wild).gainDb == kMaxGainDb);
    wild.gainDb = -200.0;
    CHECK (sanitised (wild).gainDb == kMinGainDb);

    // The note as it will sound carries them.
    NoteEdit loud;
    loud.gainDb = -4.5;
    loud.muted = true;
    const auto out = applyEdits ({ note (60.0, 0.0) }, { loud }, Settings {}, 0, 1);
    CHECK (out[0].gainDb == -4.5);
    CHECK (out[0].muted);

    Settings s;
    s.added = 0x1fff;
    s.removed = 0x0f0f;
    s.a4 = 1000.0;
    const auto safe = sanitised (s);
    CHECK (safe.added == 0xfff);
    CHECK (safe.removed == 0);   // (a note can't be in and out)
    CHECK (safe.a4 == 466.0);
    s.a4 = std::numeric_limits<double>::quiet_NaN();
    CHECK (sanitised (s).a4 == 0.0);
}

TEST_CASE ("Honey Tune: notes switched into / out of the key, and the tuning, move where notes land", "[honey][keygrid]")
{
    Settings s;
    auto g = keyGrid (s, 0, 1);   // C major
    CHECK (g.note (66.1) == 67.0);   // F# isn't in C major: G
    CHECK (g.step (60.0, 1) == 62.0);
    CHECK (g.step (60.0, -1) == 59.0);

    s.added = 1 << 6;   // F# in
    g = keyGrid (s, 0, 1);
    CHECK (g.has (66));
    CHECK (g.note (66.1) == 66.0);
    CHECK (g.step (65.0, 1) == 66.0);

    s.added = 0;
    s.removed = 1 << 4;   // E out
    g = keyGrid (s, 0, 1);
    CHECK_FALSE (g.has (64));
    CHECK (g.note (64.2) == 65.0);

    // Everything switched out: the key's notes stay (a key can't be empty).
    s.removed = 0xfff;
    g = keyGrid (s, 0, 1);
    CHECK (g.mask() != 0);

    // A4 = 443 Hz: every note 12 cents up; Auto: the beat's tuning (cents) from Voxology.
    s = {};
    s.a4 = 443.0;
    g = keyGrid (s, 0, 1);
    CHECK (g.tune == Approx (12.0 * std::log2 (443.0 / 440.0)));
    CHECK (g.note (60.05) == Approx (60.0 + g.tune));
    s.a4 = 0.0;
    g = keyGrid (s, 0, 1, -8);
    CHECK (g.tune == Approx (-0.08));
    CHECK (g.step (60.0 - 0.08, 1) == Approx (62.0 - 0.08));

    // A singer in the beat's tuning (12 cents sharp of 440) isn't off-key, and Snap leaves the note where it is.
    const auto snap = makeSnapshot (nullptr, { note (60.12, 0.0) }, std::vector<NoteEdit> (1), {}, Settings {}, {}, 12);
    REQUIRE (snap.notes.size() == 1);
    CHECK_FALSE (snap.notes[0].wasOff);
    CHECK (snap.notes[0].note.target == Approx (60.12));
    CHECK (snap.grid.tune == Approx (0.12));
    const auto plain = makeSnapshot (nullptr, { note (60.12, 0.0) }, std::vector<NoteEdit> (1), {}, Settings {}, {}, 0);
    CHECK (plain.notes[0].note.target == Approx (60.0));
}

TEST_CASE ("Honey Tune: notes to check - one note cut in two, sung out of the key, between two notes; D's fix", "[honey][checks]")
{
    Settings s;   // Snap 100 %
    // A note and a 50 ms scrap at its pitch 10 ms after it: one note cut in two (D joins them).
    {
        const auto a = note (60.0, 0.0, 0.4), b = note (60.2, 0.41, 0.05);
        std::vector<NoteView> v { viewOf (a), viewOf (b) };
        const auto c = findChecks (nullptr, v, keyGrid (s, 0, 1), s, 48000.0);
        CHECK (c[1].why == NoteCheck::Why::split);
        CHECK (c[1].joinWith == 0);
        CHECK (c[0].why == NoteCheck::Why::split);   // (so close and at one pitch: either joins them)
        // A real next note (a third up) isn't.
        std::vector<NoteView> w { viewOf (a), viewOf (note (64.0, 0.41, 0.05)) };
        CHECK_FALSE (findChecks (nullptr, w, keyGrid (s, 0, 1), s, 48000.0)[1].any());
    }
    // F# sung in C major: Snap moves it a semitone. D keeps F#; switching F# into the key clears it.
    {
        std::vector<NoteView> v { viewOf (note (66.1, 0.0)) };
        auto c = findChecks (nullptr, v, keyGrid (s, 0, 1), s, 48000.0);
        CHECK (c[0].why == NoteCheck::Why::outOfKey);
        CHECK (c[0].other == 66.0);
        v[0].check = c[0];
        const auto fix = checkFix (v[0]);
        CHECK (fix.moved);
        CHECK (fix.target == 66.0);
        auto withFs = s;
        withFs.added = 1 << 6;
        CHECK_FALSE (findChecks (nullptr, v, keyGrid (withFs, 0, 1), withFs, 48000.0)[0].any());
        // Snap at 0: nothing pulls it, nothing to check.
        auto noSnap = s;
        noSnap.snap = 0.0;
        CHECK_FALSE (findChecks (nullptr, v, keyGrid (noSnap, 0, 1), noSnap, 48000.0)[0].any());
    }
    // Sung 48 cents over C (Chromatic): Snap may pick the wrong one. D takes C#.
    {
        std::vector<NoteView> v { viewOf (note (60.48, 0.0)) };
        const auto c = findChecks (nullptr, v, keyGrid (s, 0, 0), s, 48000.0);
        CHECK (c[0].why == NoteCheck::Why::between);
        CHECK (c[0].other == 61.0);
        // 20 cents over: clear.
        std::vector<NoteView> w { viewOf (note (60.2, 0.0)) };
        CHECK_FALSE (findChecks (nullptr, w, keyGrid (s, 0, 0), s, 48000.0)[0].any());
    }
    // Notes you changed or checked are left alone.
    {
        std::vector<NoteView> v { viewOf (note (66.1, 0.0)), viewOf (note (66.1, 1.0)) };
        v[0].edit.gainDb = 2.0;
        v[1].edit.checked = true;
        const auto c = findChecks (nullptr, v, keyGrid (s, 0, 1), s, 48000.0);
        CHECK_FALSE (c[0].any());
        CHECK_FALSE (c[1].any());
    }
}

TEST_CASE ("Honey Tune: a long note that wanders off pitch is a note to check (its vibrato isn't); D holds it steady", "[honey][checks]")
{
    // A C4 rising 140 cents over 0.9 s (a sagging, wandering note), then a steady E4 with vibrato-free singing.
    const auto x = sing ({ { 60.0, 0.9, 0.3, 140.0 }, { 64.0, 0.9, 0.2 } });
    auto t = std::make_shared<vox::honey::Track> (vox::honey::analyse (x, kSr, false));
    const auto notes = vox::honey::findNotes (*t);
    REQUIRE (notes.size() == 2);
    Settings s;
    s.snap = 0.0;   // (just the wander)
    auto snap = makeSnapshot (t, notes, std::vector<NoteEdit> (2), {}, s);
    CHECK (snap.notes[0].check.why == NoteCheck::Why::wobbly);
    CHECK_FALSE (snap.notes[1].check.any());
    CHECK (snap.checks == 1);
    const auto fix = checkFix (snap.notes[0]);
    CHECK (fix.drift == 0.0);
    CHECK (fix.vibrato < 0.0);   // its vibrato follows the settings, as before
    CHECK (checkText (snap.notes[0]).find ("D holds it steady") != std::string::npos);
    // Held steady: no longer to check.
    snap = makeSnapshot (t, notes, { fix, NoteEdit {} }, {}, s);
    CHECK (snap.checks == 0);
}

TEST_CASE ("Honey Tune link: Voxology tells Honey Tune the beat's tuning it uses", "[honey][link]")
{
    using namespace vox::honeylink;
    Fresh f;
    auto& link = *f.link;
    const auto lead = suite::nameHash ("Lead Vox");
    const std::uint64_t now = 100000;
    VoxInfo sent { lead, Pitch::resting, 7, 1, KeyFrom::beat };
    sent.tuneCents = 13;
    link.publishVox (21, sent, now);
    VoxInfo got;
    REQUIRE (link.voxologyOn (lead, got, now));
    CHECK (got.tuneCents == 13);
    auto key = trackKeyFrom (got);
    CHECK (key.tuned);
    CHECK (key.tuneCents == 13);

    sent.tuneCents = 400;   // (out of range: clamped)
    link.publishVox (21, sent, now);
    REQUIRE (link.voxologyOn (lead, got, now));
    CHECK (got.tuneCents == 50);

    // A key Voxology learned from the singing carries no tuning (a voice's own tuning isn't the song's).
    got.keyFrom = KeyFrom::voice;
    key = trackKeyFrom (got);
    CHECK_FALSE (key.tuned);
    CHECK (key.tuneCents == 0);
}

TEST_CASE ("Honey Tune: a note moved in time lands on the beat (finer lines when zoomed in)", "[honey][beat]")
{
    // 120 BPM: a beat every 0.5 s.
    Timeline tl;
    for (int b = 0; b < 16; ++b) tl.lines.push_back ({ 0.5 * b, b / 4 + 1, b % 4 + 1 });
    CHECK (tl.nearestLine (1.12, 40.0) == Approx (1.0));    // 20 px a beat: beats only
    CHECK (tl.nearestLine (1.32, 40.0) == Approx (1.5));
    CHECK (tl.nearestLine (1.32, 120.0) == Approx (1.25));  // 60 px a beat: eighths
    CHECK (tl.nearestLine (1.32, 400.0) == Approx (1.375)); // 200 px a beat: sixteenths
    CHECK (tl.nearestLine (0.0, 400.0) == Approx (0.0));
    CHECK (tl.nearestLine (9.0, 40.0) == Approx (7.5));     // past the last line: the last
    CHECK (Timeline {}.nearestLine (1.0, 100.0) < 0.0);      // no bars and beats from the host: free
}
