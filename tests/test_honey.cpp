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
