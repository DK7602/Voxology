#include "Signals.h"
#include "vox/HoneyTune.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using namespace vox::honey;
using Catch::Approx;

namespace {
constexpr double kSr = 44100.0;

struct Sung { double midi, seconds, gap, vibratoCents = 0.0, driftCents = 0.0; };

/** A sung line: harmonic voice with a formant, notes separated by short breaths. */
std::vector<float> sing (const std::vector<Sung>& line)
{
    std::vector<float> x;
    Biquad formant; design::apply (formant, design::bell (700.0, 9.0, 2.0, kSr));
    double ph = 0.0;
    for (const auto& s : line)
    {
        const auto n = static_cast<size_t> (s.seconds * kSr);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / kSr;
            const double env = std::min ({ 1.0, t / 0.02, (s.seconds - t) / 0.02 });
            const double cents = s.vibratoCents * std::sin (2 * std::numbers::pi * 5.5 * t) + s.driftCents * (t / s.seconds - 0.5) * 2.0;
            const double f = 440.0 * std::pow (2.0, (s.midi - 69.0 + cents / 100.0) / 12.0);
            ph += f / kSr;
            double v = 0.0;
            for (int h = 1; h * f < 8000.0; ++h) v += std::sin (2 * std::numbers::pi * h * ph) / h;
            x.push_back (static_cast<float> (0.1 * env * formant.process (v)));
        }
        for (size_t i = 0; i < static_cast<size_t> (s.gap * kSr); ++i) x.push_back (static_cast<float> (formant.process (0.0)));
    }
    return x;
}

double hzAt (const std::vector<float>& x, double fromSec, double lenSec)
{
    const auto from = static_cast<size_t> (fromSec * kSr), len = static_cast<size_t> (lenSec * kSr);
    const int minLag = static_cast<int> (kSr / 1000), maxLag = static_cast<int> (kSr / 70);
    std::vector<double> r (static_cast<size_t> (maxLag + 2), 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = from; i < from + len; ++i) { const double a = x[i], b = x[i + static_cast<size_t> (lag)]; xy += a * b; xx += a * a; yy += b * b; }
        r[static_cast<size_t> (lag)] = xy / std::sqrt (xx * yy + 1e-30);
    }
    double best = -1; int bl = minLag;
    for (int lag = minLag + 1; lag <= maxLag; ++lag) if (r[static_cast<size_t> (lag)] > best) { best = r[static_cast<size_t> (lag)]; bl = lag; }
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (r[static_cast<size_t> (lag)] > 0.97 * best && r[static_cast<size_t> (lag)] >= r[static_cast<size_t> (lag - 1)] && r[static_cast<size_t> (lag)] >= r[static_cast<size_t> (lag + 1)]) { bl = lag; break; }
    const double a = r[static_cast<size_t> (bl - 1)], b = r[static_cast<size_t> (bl)], c = r[static_cast<size_t> (bl + 1)], d = a - 2 * b + c;
    return kSr / (bl + (std::abs (d) > 1e-12 ? 0.5 * (a - c) / d : 0.0));
}
double midiOf (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

const std::vector<Sung> kLine { { 57, 0.45, 0.08 }, { 60, 0.45, 0.08 }, { 62.3, 0.45, 0.08 }, { 64, 0.45, 0.08 } };   // A3 C4 D4(+30c) E4
double noteMid (int k) { return k * 0.53 + 0.225; }   // centre of note k (s)
}

TEST_CASE ("Honey Tune finds the sung notes and their pitch", "[honey]")
{
    const auto x = sing (kLine);
    const auto t = analyse (x, kSr);
    const auto notes = findNotes (t);
    REQUIRE (notes.size() == 4);
    const double want[] = { 57, 60, 62.3, 64 };
    for (size_t k = 0; k < 4; ++k)
    {
        INFO ("note " << k << " found " << notes[k].pitch);
        CHECK (notes[k].pitch == Approx (want[k]).margin (0.08));
        CHECK (notes[k].start / kSr == Approx (noteMid (static_cast<int> (k)) - 0.225).margin (0.06));
    }
    CHECK (centsOff (notes[2], 9, 2) == Approx (30.0).margin (8.0));   // D4 sung 30 cents sharp in A minor
}

TEST_CASE ("Honey Tune: untouched notes come out unchanged", "[honey]")
{
    const auto x = sing (kLine);
    const auto t = analyse (x, kSr);
    const auto notes = findNotes (t);
    const auto y = render (x, kSr, t, notes);
    REQUIRE (y.size() == x.size());
    double err = 0, sig = 0;
    for (size_t i = 0; i < x.size(); ++i) { const double d = y[i] - x[i]; err += d * d; sig += static_cast<double> (x[i]) * x[i]; }
    CHECK (10 * std::log10 (err / sig + 1e-30) < -60.0);
}

TEST_CASE ("Honey Tune: snap to key fixes the sharp note and leaves the others", "[honey]")
{
    const auto x = sing (kLine);
    const auto t = analyse (x, kSr);
    auto notes = findNotes (t);
    snapToKey (notes, 9, 2);   // A minor
    const auto y = render (x, kSr, t, notes);
    const double want[] = { 57, 60, 62, 64 };
    for (int k = 0; k < 4; ++k)
    {
        const double got = midiOf (hzAt (y, noteMid (k) - 0.08, 0.16));
        INFO ("note " << k << " -> " << got);
        CHECK (got == Approx (want[k]).margin (0.04));   // within 4 cents
    }
}

TEST_CASE ("Honey Tune: drag a note two semitones; vibrato and drift controls", "[honey]")
{
    {
        const auto x = sing (kLine);
        const auto t = analyse (x, kSr);
        auto notes = findNotes (t);
        notes[1].target = notes[1].pitch + 2.0;
        const auto y = render (x, kSr, t, notes);
        CHECK (midiOf (hzAt (y, noteMid (1) - 0.08, 0.16)) == Approx (62.0).margin (0.06));
        CHECK (midiOf (hzAt (y, noteMid (0) - 0.08, 0.16)) == Approx (57.0).margin (0.04));
    }
    {
        // A long note with +-40 cent vibrato: vibrato 0 flattens it.
        const auto x = sing ({ { 60, 1.5, 0.1, 40.0 } });
        const auto t = analyse (x, kSr);
        auto notes = findNotes (t);
        REQUIRE (notes.size() == 1);
        notes[0].vibrato = 0.0;
        const auto y = render (x, kSr, t, notes);
        auto spread = [&] (const std::vector<float>& v)
        {
            double lo = 1e9, hi = -1e9;
            for (double s = 0.3; s < 1.2; s += 0.03) { const double m = midiOf (hzAt (v, s, 0.03)); lo = std::min (lo, m); hi = std::max (hi, m); }
            return (hi - lo) * 100.0;
        };
        const double sIn = spread (x), sOut = spread (y);
        INFO ("vibrato spread in " << sIn << " out " << sOut);
        CHECK (sOut < 0.35 * sIn);
    }
    {
        // A note that drifts up 60 cents across its length: drift 0 straightens it.
        const auto x = sing ({ { 60, 1.2, 0.1, 0.0, 30.0 } });
        const auto t = analyse (x, kSr);
        auto notes = findNotes (t);
        REQUIRE (notes.size() == 1);
        notes[0].drift = 0.0;
        const auto y = render (x, kSr, t, notes);
        const double a = midiOf (hzAt (y, 0.2, 0.08)), b = midiOf (hzAt (y, 0.95, 0.08));
        const double a0 = midiOf (hzAt (x, 0.2, 0.08)), b0 = midiOf (hzAt (x, 0.95, 0.08));
        INFO ("drift in " << (b0 - a0) * 100 << " cents, out " << (b - a) * 100);
        CHECK (std::abs (b - a) < 0.35 * std::abs (b0 - a0));
    }
}

TEST_CASE ("Honey Tune: an edit re-renders only around its note, same result as a full render", "[honey]")
{
    // Phrases with breaths between them (A3, C4, E4, D4; 0.4 s each, 0.25 s gaps).
    const auto x = sing ({ { 57, 0.4, 0.25 }, { 60, 0.4, 0.25 }, { 64, 0.4, 0.25 }, { 62, 0.4, 0.25 } });
    auto mid = [] (int k) { return 0.2 + 0.65 * k; };
    const auto t = analyse (x, kSr);
    const auto before = findNotes (t);
    REQUIRE (before.size() >= 3);
    auto prev = render (x, kSr, t, before);
    auto after = before;
    after[1].target = after[1].pitch + 2.0;
    double from = 0.0, to = 0.0;
    REQUIRE (changedRegion (before, after, static_cast<double> (x.size()), kSr, from, to));
    INFO ("region " << from / kSr << " - " << to / kSr << " s (note 1: " << after[1].start / kSr << " - " << after[1].end / kSr << ")");
    CHECK (from <= after[1].start);
    CHECK (to >= after[1].end);
    CHECK (to - from < 0.6 * static_cast<double> (x.size()));   // much less than the whole clip
    const auto old = prev;
    renderPart (x, kSr, t, after, prev, from, to);
    const auto full = render (x, kSr, t, after);
    // The edited note moved; outside the region nothing changed at all.
    CHECK (midiOf (hzAt (prev, mid (1) - 0.08, 0.16)) == Approx (62.0).margin (0.06));
    for (size_t i = 0; i < x.size(); ++i)
        if (static_cast<double> (i) < from - 0.011 * kSr || static_cast<double> (i) > to + 0.011 * kSr) REQUIRE (prev[i] == old[i]);
    // And it sounds like a full render of the edit: the same notes, the same level, no clicks at the joins
    // (they sit in the quiet gaps; sample by sample they differ by the shifter's tiny timing offsets).
    for (int k = 0; k < 4; ++k)
        CHECK (midiOf (hzAt (prev, mid (k) - 0.08, 0.16)) == Approx (midiOf (hzAt (full, mid (k) - 0.08, 0.16))).margin (0.03));
    auto rms = [&] (const std::vector<float>& v, double a, double b)
    {
        double e = 0; for (auto i = static_cast<size_t> (a); i < static_cast<size_t> (b); ++i) e += static_cast<double> (v[i]) * v[i];
        return 10 * std::log10 (e / (b - a) + 1e-30);
    };
    INFO ("note 1 level: partial " << rms (prev, after[1].start, after[1].end) << " full " << rms (full, after[1].start, after[1].end) << " input " << rms (std::vector<float> (x), after[1].start, after[1].end)
          << "; first 50 ms: partial " << rms (prev, after[1].start, after[1].start + 0.05 * kSr) << " full " << rms (full, after[1].start, after[1].start + 0.05 * kSr));
    CHECK (rms (prev, from, to) == Approx (rms (full, from, to)).margin (1.5));   // (a 2-semitone move varies +-1 dB with the shifter's start)
    double jx = 0, jy = 0;
    for (size_t i = 1; i < x.size(); ++i) { jx = std::max (jx, std::abs (static_cast<double> (full[i]) - full[i - 1])); jy = std::max (jy, std::abs (static_cast<double> (prev[i]) - prev[i - 1])); }
    CHECK (jy < 1.2 * jx);
    // Nothing changed -> an empty region.
    CHECK (changedRegion (after, after, static_cast<double> (x.size()), kSr, from, to));
    CHECK (from == to);
}

namespace {
/** Where the sound is: the 10 ms-window RMS envelope above -40 dB (relative to the loudest) in [a, b) seconds. */
std::pair<double, double> soundSpan (const std::vector<float>& y, double a, double b)
{
    const auto w = static_cast<size_t> (0.01 * kSr);
    std::vector<double> e;
    for (auto s = static_cast<size_t> (a * kSr); s + w < static_cast<size_t> (b * kSr); s += w)
    {
        double q = 0; for (size_t i = s; i < s + w; ++i) q += static_cast<double> (y[i]) * y[i];
        e.push_back (q / static_cast<double> (w));
    }
    const double peak = *std::max_element (e.begin(), e.end());
    double first = -1, last = -1;
    for (size_t k = 0; k < e.size(); ++k)
        if (e[k] > peak * 1e-4) { if (first < 0) first = a + 0.01 * static_cast<double> (k); last = a + 0.01 * static_cast<double> (k + 1); }
    return { first, last };
}
double centroid (const std::vector<float>& y, double a, double len)
{
    // Spectral centroid by a direct DFT of a Hann-windowed frame (coarse but fine for a comparison).
    const auto n = static_cast<size_t> (len * kSr), s0 = static_cast<size_t> (a * kSr);
    double num = 0, den = 0;
    for (int k = 2; k < 400; k += 2)
    {
        const double f = k * kSr / static_cast<double> (n);
        if (f > 6000.0) break;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n));
            const double ph = 2 * std::numbers::pi * k * static_cast<double> (i) / static_cast<double> (n);
            re += w * y[s0 + i] * std::cos (ph); im -= w * y[s0 + i] * std::sin (ph);
        }
        const double m = std::sqrt (re * re + im * im);
        num += f * m; den += m;
    }
    return num / (den + 1e-30);
}
}

TEST_CASE ("Honey Tune: move a note later, stretch a note, per-note formant", "[honey]")
{
    // A3, C4, E4: 0.4 s each with 0.3 s breaths.
    const auto x = sing ({ { 57, 0.4, 0.3 }, { 60, 0.4, 0.3 }, { 64, 0.4, 0.3 } });
    const auto t = analyse (x, kSr);
    const auto notes = findNotes (t);
    REQUIRE (notes.size() == 3);
    const auto in = soundSpan (x, 0.55, 1.25);   // note 2 as sung (~0.7 - 1.1 s)

    SECTION ("move 100 ms later: same pitch, starts and ends 100 ms later")
    {
        auto n = notes;
        n[1].outStart = n[1].start + 0.1 * kSr;
        n[1].outEnd = n[1].end + 0.1 * kSr;
        const auto y = render (x, kSr, t, n);
        const auto out = soundSpan (y, 0.55, 1.35);
        INFO ("sung " << in.first << " - " << in.second << " s, moved " << out.first << " - " << out.second << " s");
        CHECK (out.first - in.first == Approx (0.1).margin (0.025));
        CHECK (out.second - in.second == Approx (0.1).margin (0.025));
        CHECK (midiOf (hzAt (y, 0.95, 0.12)) == Approx (60.0).margin (0.05));
        CHECK (midiOf (hzAt (y, 0.15, 0.12)) == Approx (57.0).margin (0.05));   // the others untouched
        CHECK (midiOf (hzAt (y, 1.55, 0.12)) == Approx (64.0).margin (0.05));
    }
    SECTION ("stretch to 1.5x: lasts 50 % longer, same pitch")
    {
        auto n = notes;
        n[1].outStart = n[1].start;
        n[1].outEnd = n[1].start + 1.5 * (n[1].end - n[1].start);
        const auto y = render (x, kSr, t, n);
        const auto out = soundSpan (y, 0.55, 1.35);
        INFO ("sung " << in.second - in.first << " s, stretched " << out.second - out.first << " s");
        CHECK ((out.second - out.first) / (in.second - in.first) == Approx (1.5).margin (0.12));
        CHECK (midiOf (hzAt (y, 1.0, 0.15)) == Approx (60.0).margin (0.05));
    }
    SECTION ("formant +4 on the middle note only: brighter there, notes unchanged")
    {
        auto n = notes;
        n[1].formant = 4.0;
        const auto y = render (x, kSr, t, n);
        const double cIn = centroid (x, 0.8, 0.1), cOut = centroid (y, 0.8, 0.1);
        INFO ("centroid of the middle note " << cIn << " -> " << cOut << " Hz; first note " << centroid (x, 0.1, 0.1) << " -> " << centroid (y, 0.1, 0.1));
        CHECK (cOut > 1.07 * cIn);   // (this coarse centroid moves ~10 % for +4 st on this test voice)
        CHECK (midiOf (hzAt (y, 0.8, 0.12)) == Approx (60.0).margin (0.05));
        CHECK (centroid (y, 0.1, 0.1) == Approx (centroid (x, 0.1, 0.1)).epsilon (0.03));
    }
    SECTION ("a timing edit re-renders only around it, like a full render")
    {
        auto prev = render (x, kSr, t, notes);
        auto n = notes;
        n[1].outStart = n[1].start + 0.08 * kSr;
        n[1].outEnd = n[1].end + 0.08 * kSr;
        double from = 0, to = 0;
        REQUIRE (changedRegion (notes, n, static_cast<double> (x.size()), kSr, from, to));
        renderPart (x, kSr, t, n, prev, from, to);
        const auto full = render (x, kSr, t, n);
        const auto a = soundSpan (prev, 0.55, 1.35), b = soundSpan (full, 0.55, 1.35);
        CHECK (a.first == Approx (b.first).margin (0.011));
        CHECK (a.second == Approx (b.second).margin (0.011));
        CHECK (midiOf (hzAt (prev, 0.95, 0.12)) == Approx (60.0).margin (0.05));
    }
}
