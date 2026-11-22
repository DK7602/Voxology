// Honey Tune: notes you split or join by hand (vox::honey::Layout, reshape) and the edits that go with them; a note moved
// in time takes its consonants along.
#include "HoneyView.h"
#include "Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox::honey;
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

bool sameNotes (const std::vector<Note>& a, const std::vector<Note>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].firstReading != b[i].firstReading || a[i].lastReading != b[i].lastReading || std::abs (a[i].pitch - b[i].pitch) > 1.0e-9)
            return false;
    return true;
}
}

TEST_CASE ("Honey Tune: split a note in two (each part its own pitch), join notes into one; each undoes the other", "[honey][layout]")
{
    // A rising C4 (60 cents from start to end: one note to findNotes), a breath, then E4 and G4.
    const auto x = sing ({ { 60.0, 0.8, 0.25, 60.0 }, { 64.0, 0.4, 0.25 }, { 67.0, 0.4, 0.2 } });
    const auto t = analyse (x, kSr, false);
    const auto found = findNotes (t);
    REQUIRE (found.size() == 3);
    const double mid = 0.5 * (found[0].start + found[0].end) / kSr;

    // Split C4 in the middle: two notes, readings back to back, each part's pitch from its own half.
    Layout layout;
    REQUIRE (addSplit (layout, t, found, 0, mid));
    CHECK (layout.splits.size() == 1);
    const auto split = reshape (t, found, layout);
    REQUIRE (split.size() == 4);
    CHECK (split[0].firstReading == found[0].firstReading);
    CHECK (split[1].lastReading == found[0].lastReading);
    CHECK (split[1].firstReading == split[0].lastReading + 1);
    CHECK (split[0].end < split[1].start);
    CHECK (split[1].start / kSr == Approx (mid).margin (0.01));
    INFO ("parts " << split[0].pitch << " / " << split[1].pitch << " (whole " << found[0].pitch << ")");
    CHECK (split[1].pitch - split[0].pitch > 0.2);   // the lower half and the higher half: each snaps on its own
    CHECK (sameNotes ({ split[2], split[3] }, { found[1], found[2] }));

    // Joining the two parts takes the split away again: the notes as found.
    REQUIRE (addJoin (layout, t, split, 0, 1));
    CHECK (layout.empty());
    CHECK (sameNotes (reshape (t, found, layout), found));

    // Join E4 and G4 (across their gap): one note over both.
    REQUIRE (addJoin (layout, t, found, 1, 2));
    CHECK (layout.joins.size() == 1);
    const auto joined = reshape (t, found, layout);
    REQUIRE (joined.size() == 2);
    CHECK (joined[1].firstReading == found[1].firstReading);
    CHECK (joined[1].lastReading == found[2].lastReading);
    CHECK (joined[1].start == found[1].start);
    CHECK (joined[1].end == found[2].end);
    CHECK (joined[1].pitch > 63.5);
    CHECK (joined[1].pitch < 67.5);
    // A split where they were joined brings the two notes back (not a third boundary).
    REQUIRE (addSplit (layout, t, joined, 1, boundarySeconds (t, found[1], found[2])));
    CHECK (layout.empty());

    // Splits and joins together; the same layout on the same analysis always gives the same notes (a project reopened).
    REQUIRE (addSplit (layout, t, found, 0, mid));
    REQUIRE (addJoin (layout, t, reshape (t, found, layout), 2, 3));
    const auto both = reshape (t, found, layout);
    CHECK (both.size() == 3);
    CHECK (sameNotes (reshape (t, found, layout), both));
}

TEST_CASE ("Honey Tune: where a split can't go; a boundary that no longer finds its notes does nothing", "[honey][layout]")
{
    const auto x = sing ({ { 60.0, 0.5, 0.25 }, { 64.0, 0.5, 0.2 } });
    const auto t = analyse (x, kSr, false);
    const auto found = findNotes (t);
    REQUIRE (found.size() == 2);
    Layout layout;
    // Too near a note's ends (each part must last kMinPartSeconds), outside it, nonsense.
    CHECK_FALSE (addSplit (layout, t, found, 0, found[0].start / kSr + 0.01));
    CHECK_FALSE (addSplit (layout, t, found, 0, found[0].end / kSr - 0.01));
    CHECK_FALSE (addSplit (layout, t, found, 0, found[1].start / kSr + 0.1));
    CHECK_FALSE (addSplit (layout, t, found, 5, 0.3));
    CHECK_FALSE (addSplit (layout, t, found, 0, std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE (addJoin (layout, t, found, 1, 1));
    CHECK_FALSE (addJoin (layout, t, found, 1, 7));
    CHECK (layout.empty());
    // Saved boundaries that don't fit this analysis (another take, a re-analysis that heard it differently).
    layout.splits = { 0.001, 50.0 };
    layout.joins = { 0.1, 99.0 };
    CHECK (sameNotes (reshape (t, found, layout), found));
}

TEST_CASE ("Honey Tune: edits carried over a split / join - the parts sound as the note did, moves are steps from each part's pitch", "[honey][layout]")
{
    const auto x = sing ({ { 60.0, 0.8, 0.6, 60.0 }, { 64.0, 0.4, 0.25 }, { 67.0, 0.4, 0.2 } });
    const auto t = analyse (x, kSr, false);
    const auto found = findNotes (t);
    REQUIRE (found.size() == 3);
    // C4 moved up 2 semitones, 0.1 s later and 1.3x longer (room for it before E4).
    std::vector<honeyui::NoteEdit> edits (found.size());
    edits[0].moved = true;
    edits[0].target = found[0].pitch + 2.0;
    edits[0].shift = 0.1;
    edits[0].length = 1.3;
    Layout layout;
    REQUIRE (addSplit (layout, t, found, 0, 0.5 * (found[0].start + found[0].end) / kSr));
    const auto split = reshape (t, found, layout);
    const auto carried = honeyui::carryEdits (found, edits, split, kSr);
    REQUIRE (carried.size() == 4);
    CHECK (carried[0].target == Approx (split[0].pitch + 2.0));
    CHECK (carried[1].target == Approx (split[1].pitch + 2.0));
    CHECK (carried[2].isDefault());
    CHECK (carried[3].isDefault());
    // Where they sound: the first part from where the note sounded, the second on straight after it, as stretched.
    honeyui::Settings s;
    s.snap = 0.0;
    const auto whole = honeyui::applyEdits (found, edits, s, 0, 0, kSr, static_cast<double> (x.size()));
    const auto parts = honeyui::applyEdits (split, carried, s, 0, 0, kSr, static_cast<double> (x.size()));
    REQUIRE (whole[0].soundStart() == Approx (found[0].start + 0.1 * kSr).margin (1.0));   // (it fits as asked)
    CHECK (parts[0].soundStart() == Approx (whole[0].soundStart()).margin (1.0));
    CHECK (parts[1].soundEnd() == Approx (whole[0].soundEnd()).margin (0.01 * kSr));
    CHECK (parts[1].soundStart() >= parts[0].soundEnd());
    CHECK (parts[1].soundStart() - parts[0].soundEnd() < 0.01 * kSr);

    // Joined: the note keeps the edit of its first edited part (here the second), as a step from its own pitch.
    std::vector<honeyui::NoteEdit> e2 (found.size());
    e2[2].moved = true;
    e2[2].target = found[2].pitch - 1.0;
    e2[2].formant = 1.5;
    Layout l2;
    REQUIRE (addJoin (l2, t, found, 1, 2));
    const auto joined = reshape (t, found, l2);
    const auto c2 = honeyui::carryEdits (found, e2, joined, kSr);
    REQUIRE (c2.size() == 2);
    CHECK (c2[0].isDefault());
    CHECK (c2[1].target == Approx (joined[1].pitch - 1.0));
    CHECK (c2[1].formant == Approx (1.5));
}

TEST_CASE ("Honey Tune: a split re-renders only around its note, the same as a full render", "[honey][layout]")
{
    const auto x = sing ({ { 57.0, 0.4, 0.25 }, { 60.0, 0.8, 0.25, 60.0 }, { 64.0, 0.4, 0.25 }, { 62.0, 0.4, 0.25 } });
    const auto t = analyse (x, kSr, false);
    const auto before = findNotes (t);
    REQUIRE (before.size() == 4);
    auto prev = render (x, kSr, t, before);
    const auto old = prev;
    Layout layout;
    REQUIRE (addSplit (layout, t, before, 1, 0.5 * (before[1].start + before[1].end) / kSr));
    auto after = reshape (t, before, layout);
    REQUIRE (after.size() == 5);
    after[2].target = after[2].pitch + 2.0;   // the second half up
    double from = 0.0, to = 0.0;
    REQUIRE (changedRegion (before, after, static_cast<double> (x.size()), kSr, from, to));
    INFO ("region " << from / kSr << " - " << to / kSr << " s");
    CHECK (from <= after[1].start);
    CHECK (to >= after[2].end);
    CHECK (from > before[0].end);           // the notes before and after it stay as they were
    CHECK (to < before[2].start);
    renderPart (x, kSr, t, after, prev, from, to);
    const auto full = render (x, kSr, t, after);
    for (size_t i = 0; i < x.size(); ++i)
        if (static_cast<double> (i) < from - 0.011 * kSr || static_cast<double> (i) > to + 0.011 * kSr) REQUIRE (prev[i] == old[i]);
    // Inside: as a full render of the split - the moved half moved, the first half as it was, the same loudness
    // (sample by sample they differ by the shifter's tiny timing offsets).
    const auto pitchAt = [] (const std::vector<float>& v, double centre)
    {
        auto tr = analyse (std::vector<float> (v.begin() + static_cast<long> ((centre - 0.08) * kSr), v.begin() + static_cast<long> ((centre + 0.08) * kSr)), kSr, false);
        std::vector<double> m;
        for (double q : tr.midi) if (q > 0.0) m.push_back (q);
        std::sort (m.begin(), m.end());
        return m.empty() ? 0.0 : m[m.size() / 2];
    };
    const double c1 = 0.5 * (after[1].start + after[1].end) / kSr, c2 = 0.5 * (after[2].start + after[2].end) / kSr;
    INFO ("first half " << pitchAt (prev, c1) << " (full " << pitchAt (full, c1) << "), second " << pitchAt (prev, c2) << " (full " << pitchAt (full, c2) << ")");
    CHECK (pitchAt (prev, c1) == Approx (pitchAt (full, c1)).margin (0.05));
    CHECK (pitchAt (prev, c2) == Approx (pitchAt (full, c2)).margin (0.05));
    CHECK (pitchAt (prev, c2) - pitchAt (old, c2) == Approx (2.0).margin (0.1));
    const auto db = [] (const std::vector<float>& v, double a, double b)
    {
        double e = 0.0;
        for (auto i = static_cast<size_t> (a); i < static_cast<size_t> (b); ++i) e += static_cast<double> (v[i]) * v[i];
        return 10.0 * std::log10 (e / (b - a) + 1e-30);
    };
    CHECK (db (prev, from, to) == Approx (db (full, from, to)).margin (1.0));
    // Joined back: back to the notes as found, again only around them.
    REQUIRE (addJoin (layout, t, after, 1, 2));
    CHECK (layout.empty());
    CHECK (changedRegion (after, reshape (t, before, layout), static_cast<double> (x.size()), kSr, from, to));
    CHECK (from > before[0].end);
    CHECK (to < before[2].start);
}

TEST_CASE ("Honey Tune: a note moved in time takes its consonant along whole; the note after it starts on time", "[honey][layout]")
{
    // "A  s-t B   C": A3, 50 ms silence, an "s" (90 ms of hiss), a 30 ms closure, a "t" burst, then B4 sung, a gap, C5.
    std::vector<float> x;
    const auto add = [&] (const std::vector<float>& part) { x.insert (x.end(), part.begin(), part.end()); };
    add (sing ({ { 57.0, 0.4, 0.05 } }));
    testsig::Rng rng (7);
    const size_t sFrom = x.size();
    for (int i = 0; i < static_cast<int> (0.09 * kSr); ++i)
    {
        const double env = std::min ({ 1.0, i / (0.01 * kSr), (0.09 * kSr - i) / (0.01 * kSr) });
        x.push_back (static_cast<float> (0.04 * env * rng.normal()));
    }
    const size_t sTo = x.size();
    x.insert (x.end(), static_cast<size_t> (0.03 * kSr), 0.0f);
    for (int i = 0; i < static_cast<int> (0.006 * kSr); ++i) x.push_back (static_cast<float> (0.15 * std::exp (-i / (0.0015 * kSr)) * rng.normal()));
    add (sing ({ { 59.0, 0.5, 0.3 }, { 60.0, 0.4, 0.3 } }));
    const auto t = analyse (x, kSr, false);
    const auto notes = findNotes (t);
    REQUIRE (notes.size() >= 3);
    size_t b = 0;
    while (b < notes.size() && notes[b].start < static_cast<double> (sTo)) ++b;
    REQUIRE (b + 1 < notes.size());
    REQUIRE (std::abs (notes[b].pitch - 59.0) < 0.5);

    // How long the hiss lasts (above 12 dB under its peak, 2 ms steps) and where it starts, in a stretch of audio.
    const auto hiss = [] (const std::vector<float>& y, double from, double to)
    {
        vox::Biquad hp1, hp2;
        vox::design::apply (hp1, vox::design::butterworth (true, 4000.0, kSr));
        vox::design::apply (hp2, vox::design::butterworth (true, 4000.0, kSr));
        std::vector<double> e;
        double sum = 0.0;
        const auto step = static_cast<size_t> (0.002 * kSr);
        for (auto i = static_cast<size_t> (std::max (0.0, from)); i < static_cast<size_t> (to) && i < y.size(); ++i)
        {
            const double v = hp2.process (hp1.process (y[i]));
            sum += v * v;
            if ((i + 1) % step == 0) { e.push_back (sum); sum = 0.0; }
        }
        const double peak = *std::max_element (e.begin(), e.end());
        int first = -1, last = -1;
        for (int k = 0; k < static_cast<int> (e.size()); ++k)
            if (e[static_cast<size_t> (k)] > peak * 0.063) { if (first < 0) first = k; last = k; }
        return std::pair<double, double> { from / kSr + first * 0.002, (last - first + 1) * 0.002 };
    };
    const auto [sAt, sLen] = hiss (x, static_cast<double> (sFrom) - 0.03 * kSr, static_cast<double> (sTo) + 0.02 * kSr);

    for (double move : { 0.08, 0.15 })
    {
        auto moved = notes;
        moved[b].outStart = moved[b].start + move * kSr;
        moved[b].outEnd = moved[b].end + move * kSr;
        const auto y = render (x, kSr, t, moved);
        const auto [outAt, outLen] = hiss (y, static_cast<double> (sFrom) - 0.03 * kSr + move * kSr, static_cast<double> (sTo) + 0.02 * kSr + move * kSr);
        INFO ("moved " << move << " s: the s at " << outAt << " s for " << outLen << " s (sung at " << sAt << " for " << sLen << ")");
        CHECK (outLen == Approx (sLen).margin (0.008));          // whole (spread over the gap, it lasted 50 - 100 % longer)
        CHECK (outAt - sAt == Approx (move).margin (0.006));     // and with its note
        // The note after it: as sung (a squeeze over the breath before it used to make its attack come in late).
        const auto& c = notes[b + 1];
        double e = 0.0, d = 0.0;
        for (auto i = static_cast<size_t> (c.start); i < static_cast<size_t> (c.start + 0.05 * kSr); ++i)
        {
            e += static_cast<double> (x[i]) * x[i];
            d += (static_cast<double> (y[i]) - x[i]) * (static_cast<double> (y[i]) - x[i]);
        }
        INFO ("the next note's first 50 ms: " << 10.0 * std::log10 (d / e + 1e-30) << " dB off");
        CHECK (d < 0.01 * e);
    }
}
