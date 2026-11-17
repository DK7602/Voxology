// The Compressor's characters (Clean, FET, Opto) and Saturation's Air: each does what its name promises, measured.
#include "Signals.h"
#include "vox/Modules.h"
#include "vox/Saturation.h"
#include "vox/Fft.h"

#include <cmath>
#include <random>

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

#include "vox/Space.h"

namespace {
/** The Reverb's wet output (stereo, mix 100 %) for `x`. */
std::array<std::vector<double>, 2> reverbOf (int type, const std::vector<double>& x, double decayS = 1.6)
{
    Reverb r;
    r.prepare (kSr);
    ReverbParams p;
    p.type = type;
    p.decayS = decayS;
    p.predelayMs = 0.0;
    p.mix = 100.0;
    p.toneHz = 16000.0;
    p.duck = 0.0;
    r.setParams (p);
    std::array<std::vector<double>, 2> out { std::vector<double> (x.size(), 0.0), std::vector<double> (x.size(), 0.0) };
    for (size_t s = 0; s < x.size(); s += 256)
    {
        const int n = static_cast<int> (std::min<size_t> (256, x.size() - s));
        double* o[2] { out[0].data() + s, out[1].data() + s };
        r.process (x.data() + s, o, 2, n);
    }
    return out;
}

double energyDb (const std::array<std::vector<double>, 2>& y, double from, double to)
{
    double e = 0.0;
    for (const auto& ch : y)
        for (size_t i = static_cast<size_t> (from * kSr); i < static_cast<size_t> (to * kSr) && i < ch.size(); ++i) e += ch[i] * ch[i];
    return 10.0 * std::log10 (e + 1.0e-30);
}

/** The Delay's output (mono in, stereo out, mix 100 %, the dry not included). */
std::vector<double> delayOf (int character, const std::vector<double>& x, double feedback = 0.0)
{
    EchoDelay d;
    d.prepare (kSr);
    DelayParams p;
    p.character = character;
    p.mix = 100.0;
    p.feedback = feedback;
    p.duck = 0.0;
    p.toneHz = 16000.0;
    p.bpm = 120.0;   // 1/4 = 0.5 s
    d.setParams (p);
    std::vector<double> l (x.size(), 0.0), r (x.size(), 0.0);
    for (size_t s = 0; s < x.size(); s += 256)
    {
        const int n = static_cast<int> (std::min<size_t> (256, x.size() - s));
        double* o[2] { l.data() + s, r.data() + s };
        d.process (x.data() + s, o, 2, n);
    }
    return l;
}
}

TEST_CASE ("Reverb types: each keeps the Decay you set, Room and Chamber add early reflections", "[space][character]")
{
    std::vector<double> imp (static_cast<size_t> (3.0 * kSr), 0.0);
    imp[100] = 1.0;
    std::array<double, kReverbTypes> early {};
    for (int t = 0; t < kReverbTypes; ++t)
    {
        INFO (kReverbTypeNames[static_cast<size_t> (t)]);
        const auto y = reverbOf (t, imp);
        // RT60 from the slope between 0.4 and 1.2 s: within a third of the 1.6 s set.
        const double slope = (energyDb (y, 1.1, 1.3) - energyDb (y, 0.4, 0.6)) / 0.7;   // dB per second
        const double rt60 = -60.0 / slope;
        CHECK (rt60 > 1.6 * 0.67);
        CHECK (rt60 < 1.6 * 1.33);
        early[static_cast<size_t> (t)] = energyDb (y, 0.0, 0.05) - energyDb (y, 0.0, 1.5);
    }
    CHECK (early[1] > early[0] + 1.0);   // Room: more of its energy in the first 50 ms than the Plate
    CHECK (early[3] > early[0] + 0.5);   // Chamber too
    CHECK (early[2] < early[1]);         // Hall: a slower build-up than the Room
}

TEST_CASE ("Reverb: changing the type while it rings fades over, no jump", "[space][character]")
{
    const auto v = testsig::vocal (kSr, 3.0, -70.0, -20.0);
    const std::vector<double> x (v.begin(), v.end());
    Reverb r;
    r.prepare (kSr);
    ReverbParams p;
    p.mix = 40.0;
    r.setParams (p);
    std::vector<double> l (x.size(), 0.0), rr (x.size(), 0.0);
    double worst = 0.0, prev = 0.0;
    for (size_t s = 0; s < x.size(); s += 256)
    {
        if (s == static_cast<size_t> (kSr)) { p.type = 2; r.setParams (p); }
        if (s == static_cast<size_t> (2.0 * kSr)) { p.type = 1; r.setParams (p); }
        const int n = static_cast<int> (std::min<size_t> (256, x.size() - s));
        double* o[2] { l.data() + s, rr.data() + s };
        r.process (x.data() + s, o, 2, n);
        for (int i = 0; i < n; ++i) { REQUIRE (std::isfinite (l[s + static_cast<size_t> (i)])); worst = std::max (worst, std::abs (l[s + static_cast<size_t> (i)] - prev)); prev = l[s + static_cast<size_t> (i)]; }
    }
    CHECK (worst < 0.2);   // no step (the input's own sample-to-sample moves are about this size)
}

TEST_CASE ("Delay characters: Tape wavers, Analog is darker, Clean is exact", "[space][character]")
{
    // A steady 1 kHz tone: the echo's period (zero crossings) stays put when Clean, wavers on Tape.
    std::vector<double> tone (static_cast<size_t> (4.0 * kSr));
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.3 * std::sin (2.0 * std::numbers::pi * 1000.0 * static_cast<double> (i) / kSr);
    const auto spread = [] (const std::vector<double>& y)
    {
        std::vector<double> t;
        for (size_t i = static_cast<size_t> (1.0 * kSr); i < y.size(); ++i)
            if (y[i - 1] < 0.0 && y[i] >= 0.0) t.push_back (static_cast<double> (i) - 1.0 + (-y[i - 1]) / (y[i] - y[i - 1]));
        double lo = 1e9, hi = 0.0;
        for (size_t k = 1; k < t.size(); ++k) { lo = std::min (lo, t[k] - t[k - 1]); hi = std::max (hi, t[k] - t[k - 1]); }
        return hi - lo;   // samples
    };
    CHECK (spread (delayOf (0, tone)) < 0.01);
    CHECK (spread (delayOf (1, tone)) > 0.05);   // the pitch wavers (wow and flutter)

    // White noise in: the first echo's top (above 5 kHz) is well down on Analog, a little on Tape.
    std::vector<double> noise (static_cast<size_t> (2.0 * kSr), 0.0);
    std::mt19937 rng (3);
    std::normal_distribution<double> w (0.0, 0.1);
    for (size_t i = 0; i < static_cast<size_t> (0.3 * kSr); ++i) noise[i] = w (rng);
    const auto top = [] (const std::vector<double>& y) { return bandDb (y, 5000.0, 15000.0) - bandDb (y, 200.0, 2000.0); };
    const double clean = top (delayOf (0, noise)), tape = top (delayOf (1, noise)), analog = top (delayOf (2, noise));
    CHECK (analog < clean - 6.0);
    CHECK (tape < clean - 1.0);
    CHECK (tape > analog);
}
