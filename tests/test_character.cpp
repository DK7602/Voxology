// The Compressor's characters (Clean, FET, Opto) and Saturation's Air: each does what its name promises, measured.
#include "Signals.h"
#include "vox/Modules.h"
#include "vox/Saturation.h"
#include "vox/Fft.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

/** A 1 kHz tone at `lowDb` with a burst at `highDb` from 0.5 s to 1.5 s, 3 s in all. */
std::vector<double> burst (double lowDb, double highDb)
{
    std::vector<double> x (static_cast<size_t> (3.0 * kSr));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        const double db = t >= 0.5 && t < 1.5 ? highDb : lowDb;
        x[i] = std::pow (10.0, db / 20.0) * std::sin (2.0 * std::numbers::pi * 1000.0 * t);
    }
    return x;
}

/** The compressor's gain (dB) every 0.5 ms: output against input, over each 0.5 ms slice. */
std::vector<double> gainCurve (int style, const std::vector<double>& in)
{
    VocalCompressor c;
    c.prepare (kSr, 1);
    CompParams p;
    p.thrDb = -30.0;
    p.ratio = 4.0;
    p.style = style;
    c.setParams (p);
    c.reset();
    std::vector<double> y = in;
    for (size_t s = 0; s < y.size(); s += 256)
    {
        double* ptr = y.data() + s;
        c.process (&ptr, 1, static_cast<int> (std::min<size_t> (256, y.size() - s)));
    }
    std::vector<double> g;
    const size_t slice = static_cast<size_t> (0.0005 * kSr);
    for (size_t s = 0; s + slice <= y.size(); s += slice)
    {
        double ei = 0.0, eo = 0.0;
        for (size_t i = s; i < s + slice; ++i) { ei += in[i] * in[i]; eo += y[i] * y[i]; }
        g.push_back (10.0 * std::log10 ((eo + 1.0e-30) / (ei + 1.0e-30)));
    }
    return g;
}

double at (const std::vector<double>& g, double seconds) { return g[static_cast<size_t> (seconds / 0.0005)]; }

/** Seconds after `from` until the gain is back within `withinDb` of 0. */
double recovery (const std::vector<double>& g, double from, double withinDb)
{
    for (size_t i = static_cast<size_t> (from / 0.0005); i < g.size(); ++i)
        if (g[i] > -withinDb) return static_cast<double> (i) * 0.0005 - from;
    return 99.0;
}

/** Energy (dB) of x between two frequencies (Hann-windowed FFT over the middle of it). */
double bandDb (const std::vector<double>& x, double lo, double hi)
{
    const size_t n = 32768;
    std::vector<std::complex<double>> s (n);
    const size_t off = x.size() / 2 - n / 2;
    for (size_t i = 0; i < n; ++i)
        s[i] = { x[off + i] * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / n)), 0.0 };
    fft (s);
    double e = 0.0;
    for (size_t k = static_cast<size_t> (lo * n / kSr); k <= static_cast<size_t> (hi * n / kSr) && k < n / 2; ++k) e += std::norm (s[k]);
    return 10.0 * std::log10 (e + 1.0e-30);
}

std::vector<double> saturate (const SaturationParams& p, const std::vector<double>& in)
{
    Saturation s;
    s.prepare (kSr, 1);
    s.setParams (p);
    s.reset();
    std::vector<double> y = in;
    double* ptr = y.data();
    s.process (&ptr, 1, static_cast<int> (y.size()));
    return y;
}
}

TEST_CASE ("Compressor styles: the same reduction when held, each with its own timing", "[modules][comp][character]")
{
    const auto x = burst (-40.0, -10.0);   // 20 dB over a -30 dB threshold at 4:1: 15 dB down when held
    const auto clean = gainCurve (static_cast<int> (CompStyle::clean), x);
    const auto fet = gainCurve (static_cast<int> (CompStyle::fet), x);
    const auto opto = gainCurve (static_cast<int> (CompStyle::opto), x);

    // Held (end of the burst): all three about 15 dB down.
    for (const auto* g : { &clean, &fet, &opto })
        CHECK (at (*g, 1.45) == Approx (-15.0).margin (1.5));

    // FET: grabs the front of the burst (2 ms in it is most of the way down); Clean is still coming in.
    CHECK (at (fet, 0.502) < -10.0);
    CHECK (at (clean, 0.502) > -6.0);

    // Opto: half lets go fast, the rest slowly. Within 3 dB of 0: Clean quickly, Opto takes several times as long.
    CHECK (recovery (opto, 1.5, 8.5) < 0.2);
    CHECK (recovery (opto, 1.5, 3.0) > 3.0 * recovery (clean, 1.5, 3.0));
    // FET lets go faster than Opto.
    CHECK (recovery (fet, 1.5, 3.0) < recovery (opto, 1.5, 3.0));
}

TEST_CASE ("Compressor: changing style while it works doesn't jump", "[modules][comp][character]")
{
    VocalCompressor c;
    c.prepare (kSr, 1);
    CompParams p;
    p.thrDb = -30.0;
    p.ratio = 4.0;
    c.setParams (p);
    c.reset();
    const auto x = burst (-10.0, -10.0);
    std::vector<double> y = x;
    double worst = 0.0, prev = 0.0;
    for (size_t s = 0; s < y.size(); s += 256)
    {
        if (s == static_cast<size_t> (kSr)) { p.style = 2; c.setParams (p); }
        if (s == static_cast<size_t> (2.0 * kSr)) { p.style = 1; c.setParams (p); }
        double* ptr = y.data() + s;
        c.process (&ptr, 1, static_cast<int> (std::min<size_t> (256, y.size() - s)));
    }
    // The gain (output / input at the tone's peaks) never steps more than a fraction of a dB per cycle.
    for (size_t i = static_cast<size_t> (0.5 * kSr); i < y.size(); i += 48)
    {
        const double g = std::abs (y[i + 12]) / (std::abs (x[i + 12]) + 1.0e-12);   // (a peak of the 1 kHz tone)
        if (prev > 0.0) worst = std::max (worst, std::abs (20.0 * std::log10 (g / prev)));
        prev = g;
    }
    CHECK (worst < 1.0);
}

TEST_CASE ("Air: adds sparkle above 5 kHz, from the vocal's own top, without folding back", "[modules][saturation][character]")
{
    const auto v = testsig::vocal (kSr, 3.0, -70.0, -30.0);
    const std::vector<double> x (v.begin(), v.end());

    SaturationParams off;
    off.driveDb = 0.0;
    SaturationParams on = off;
    on.air = 60.0;
    CHECK_FALSE (Saturation::isNeutral (on));   // Air works with Drive at 0
    CHECK (Saturation::effectiveMix (on) == 0.0);

    const auto y0 = saturate (off, x), y1 = saturate (on, x);
    // More 10 - 16 kHz (the "air" range); the body and the presence range (where harshness lives) untouched.
    CHECK (bandDb (y1, 10000.0, 16000.0) - bandDb (y0, 10000.0, 16000.0) > 2.0);
    CHECK (std::abs (bandDb (y1, 100.0, 2000.0) - bandDb (y0, 100.0, 2000.0)) < 0.05);
    CHECK (std::abs (bandDb (y1, 2000.0, 6000.0) - bandDb (y0, 2000.0, 6000.0)) < 0.5);

    // A 9 kHz tone: its 2nd harmonic (18 kHz) is made; the 4th (36 kHz) is filtered, not folded to 12 kHz.
    std::vector<double> t (static_cast<size_t> (2.0 * kSr));
    for (size_t i = 0; i < t.size(); ++i) t[i] = 0.25 * std::sin (2.0 * std::numbers::pi * 9000.0 * static_cast<double> (i) / kSr);
    SaturationParams full = off;
    full.air = 100.0;
    const auto z = saturate (full, t);
    const double h2 = bandDb (z, 17800.0, 18200.0), alias = std::max (bandDb (z, 11800.0, 12200.0), bandDb (z, 20800.0, 21200.0));
    CHECK (h2 - bandDb (t, 17800.0, 18200.0) > 30.0);
    CHECK (h2 - alias > 50.0);

    // Air 0 and Drive 0: the exact (delayed) vocal.
    for (size_t i = Saturation::kLatency; i < x.size(); ++i) REQUIRE (y0[i] == x[i - Saturation::kLatency]);
}
