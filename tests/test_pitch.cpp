#include "Signals.h"
#include "vox/PitchCorrector.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

std::vector<double> run (const PitchParams& p, const std::vector<double>& x, int block = 256, int* latency = nullptr)
{
    PitchCorrector pc;
    pc.setParams (p);
    pc.prepare (kSr, 1);
    if (latency) *latency = pc.latencySamples();
    std::vector<double> y = x;
    for (size_t s = 0; s < y.size(); s += static_cast<size_t> (block))
    {
        double* ptr = y.data() + s;
        pc.process (&ptr, 1, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), y.size() - s)));
    }
    return y;
}

/** A buzzy voice-like tone (harmonics with a formant) at f0 Hz (constant or a glide). */
std::vector<double> voice (double f0a, double f0b, double seconds, double vibratoCents = 0.0)
{
    const size_t n = static_cast<size_t> (kSr * seconds);
    std::vector<double> x (n);
    double ph = 0.0;
    Biquad formant; design::apply (formant, design::bell (700.0, 9.0, 2.0, kSr));
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        const double f = f0a * std::pow (f0b / f0a, t / seconds) * std::pow (2.0, vibratoCents / 1200.0 * std::sin (2 * std::numbers::pi * 5.5 * t));
        ph += f / kSr;
        double s = 0.0;
        for (int h = 1; h * f < 8000.0; ++h) s += std::sin (2 * std::numbers::pi * h * ph) / h;
        x[i] = 0.1 * formant.process (s);
    }
    return x;
}

/** f0 of x[from, from+len) by normalised autocorrelation with parabolic interpolation. */
double measureHz (const std::vector<double>& x, size_t from, size_t len = 4800)
{
    const int minLag = static_cast<int> (kSr / 1000), maxLag = static_cast<int> (kSr / 70);
    std::vector<double> r (static_cast<size_t> (maxLag + 2), 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = from; i < from + len; ++i) { xy += x[i] * x[i + static_cast<size_t> (lag)]; xx += x[i] * x[i]; yy += x[i + static_cast<size_t> (lag)] * x[i + static_cast<size_t> (lag)]; }
        r[static_cast<size_t> (lag)] = xy / std::sqrt (xx * yy + 1e-30);
    }
    double best = -1; int bl = minLag;
    for (int lag = minLag + 1; lag <= maxLag; ++lag) if (r[static_cast<size_t> (lag)] > best) { best = r[static_cast<size_t> (lag)]; bl = lag; }
    // prefer the shortest lag nearly as good (avoid octave-down)
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (r[static_cast<size_t> (lag)] > 0.97 * best && r[static_cast<size_t> (lag)] >= r[static_cast<size_t> (lag - 1)] && r[static_cast<size_t> (lag)] >= r[static_cast<size_t> (lag + 1)]) { bl = lag; break; }
    const double a = r[static_cast<size_t> (bl - 1)], b = r[static_cast<size_t> (bl)], c = r[static_cast<size_t> (bl + 1)];
    const double d = a - 2 * b + c;
    return kSr / (bl + (std::abs (d) > 1e-12 ? 0.5 * (a - c) / d : 0.0));
}
double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

/** The same voice following any pitch curve: hz (t seconds). */
template <typename F>
std::vector<double> voiceCurve (F hz, double seconds)
{
    const size_t n = static_cast<size_t> (kSr * seconds);
    std::vector<double> x (n);
    double ph = 0.0;
    Biquad formant; design::apply (formant, design::bell (700.0, 9.0, 2.0, kSr));
    for (size_t i = 0; i < n; ++i)
    {
        ph += hz (static_cast<double> (i) / kSr) / kSr;
        double s = 0.0;
        for (int h = 1; h * hz (static_cast<double> (i) / kSr) < 8000.0; ++h) s += std::sin (2 * std::numbers::pi * h * ph) / h;
        x[i] = 0.1 * formant.process (s);
    }
    return x;
}

/** Cents vs ref in 25 ms windows from sample a to b: { mean, spread (max - min) }. */
std::pair<double, double> track (const std::vector<double>& y, double ref, size_t a, size_t b)
{
    double lo = 1e9, hi = -1e9, sum = 0; int n = 0;
    for (size_t s = a; s + 2000 < b; s += 600) { const double c = cents (measureHz (y, s, 1200), ref); lo = std::min (lo, c); hi = std::max (hi, c); sum += c; ++n; }
    return { sum / n, hi - lo };
}
double rmsDb (const std::vector<double>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += x[i] * x[i]; return 10 * std::log10 (e / static_cast<double> (b - a) + 1e-30); }
}

TEST_CASE ("Pitch: off / Amount 0 = exact delayed pass-through", "[pitch]")
{
    const auto x = voice (220.0, 230.0, 1.0);
    int lat = 0;
    PitchParams p;   // amount 0
    const auto y = run (p, x, 256, &lat);
    CHECK (lat >= 1500);
    for (size_t i = static_cast<size_t> (lat); i < x.size(); ++i) REQUIRE (y[i] == x[i - static_cast<size_t> (lat)]);
}

TEST_CASE ("Pitch: chromatic, instant retune pulls a sharp note onto the note", "[pitch]")
{
    for (double f : { 110.0 * std::pow (2.0, 0.35 / 12), 233.08 * std::pow (2.0, -0.4 / 12), 440.0 * std::pow (2.0, 0.3 / 12) })
    {
        const auto x = voice (f, f, 1.2);
        PitchParams p; p.amount = 100.0; p.speedMs = 0.0;
        int lat = 0;
        const auto y = run (p, x, 256, &lat);
        const double target = 440.0 * std::pow (2.0, std::round (12.0 * std::log2 (f / 440.0)) / 12.0);
        const double got = measureHz (y, 30000);
        INFO ("in " << f << " Hz, target " << target << ", got " << got);
        CHECK (std::abs (cents (got, target)) < 3.0);
        // Level and tone kept: within 1 dB.
        CHECK (rmsDb (y, 30000, 50000) == Approx (rmsDb (x, 30000 - static_cast<size_t> (lat), 50000 - static_cast<size_t> (lat))).margin (1.0));
    }
}

TEST_CASE ("Pitch: scale snapping and hysteresis", "[pitch]")
{
    // 60.55 = C4 + 55 cents: C# is not in C major, so C (55 cents away) wins over D (145).
    CHECK (PitchCorrector::targetNote (60.55, 0, 1, -1) == 60);
    CHECK (PitchCorrector::targetNote (61.6, 0, 1, -1) == 62);
    // Chromatic: nearest, but the current note is kept until another is 0.3 semitone closer.
    CHECK (PitchCorrector::targetNote (60.6, 0, 0, -1) == 61);
    CHECK (PitchCorrector::targetNote (60.6, 0, 0, 60) == 60);
    CHECK (PitchCorrector::targetNote (60.85, 0, 0, 60) == 61);
    // A minor pentatonic (A C D E G): G#4 (68) goes to G or A.
    const int t = PitchCorrector::targetNote (68.0, 9, 4, -1);
    CHECK ((t == 67 || t == 69));
}

TEST_CASE ("Pitch: slow retune keeps vibrato, fast retune flattens it", "[pitch]")
{
    const auto x = voice (196.0, 196.0, 2.0, 40.0);   // G3 with +-40 cent vibrato
    auto spread = [&] (const std::vector<double>& y)
    {
        double lo = 1e9, hi = -1e9;
        for (size_t s = 40000; s + 2000 < 90000; s += 1200) { const double c = cents (measureHz (y, s, 1200), 196.0); lo = std::min (lo, c); hi = std::max (hi, c); }
        return hi - lo;
    };
    PitchParams fast; fast.amount = 100.0; fast.speedMs = 0.0;
    PitchParams slow; slow.amount = 100.0; slow.speedMs = 400.0;
    const double sIn = spread (x), sFast = spread (run (fast, x)), sSlow = spread (run (slow, x));
    INFO ("vibrato spread in " << sIn << " fast " << sFast << " slow " << sSlow);
    CHECK (sFast < 0.35 * sIn);
    CHECK (sSlow > 0.6 * sIn);
}

TEST_CASE ("Pitch: unvoiced noise passes through, any block size gives the same output", "[pitch]")
{
    std::mt19937 rng (3);
    std::normal_distribution<double> w (0.0, 0.05);
    std::vector<double> x (48000);
    for (auto& v : x) v = w (rng);
    PitchParams p; p.amount = 100.0; p.speedMs = 0.0;
    int lat = 0;
    const auto y = run (p, x, 256, &lat);
    double err = 0, sig = 0;
    for (size_t i = 10000; i < x.size(); ++i) { const double d = y[i] - x[i - static_cast<size_t> (lat)]; err += d * d; sig += x[i] * x[i]; }
    CHECK (10 * std::log10 (err / sig) < -40.0);

    const auto v = voice (180.0, 260.0, 1.0);
    const auto a = run (p, v, 512);
    for (int b : { 1, 37, 4096 })
    {
        const auto c = run (p, v, b);
        double e = 0; for (size_t i = 0; i < v.size(); ++i) e = std::max (e, std::abs (a[i] - c[i]));
        INFO ("block " << b);
        CHECK (e < 1e-9);
    }
}

TEST_CASE ("Pitch: a glide becomes clean steps, no clicks", "[pitch]")
{
    const auto x = voice (196.0, 262.0, 2.0);   // G3 -> C4 slide
    PitchParams p; p.amount = 100.0; p.speedMs = 0.0;   // chromatic hard tune
    const auto y = run (p, x);
    int onNote = 0, total = 0;
    for (size_t s = 6000; s + 3000 < y.size(); s += 1500)
    {
        const double m = 69 + 12 * std::log2 (measureHz (y, s, 1500) / 440.0);
        ++total;
        if (std::abs (m - std::round (m)) < 0.12) ++onNote;
    }
    INFO (onNote << " of " << total << " windows on a note");
    CHECK (onNote >= total * 8 / 10);
    // No clicks: the biggest sample-to-sample jump stays within what the input itself has.
    double jx = 0, jy = 0;
    for (size_t i = 1; i < x.size(); ++i) { jx = std::max (jx, std::abs (x[i] - x[i - 1])); jy = std::max (jy, std::abs (y[i] - y[i - 1])); }
    CHECK (jy < 1.6 * jx);
}

TEST_CASE ("Key detection from sung notes", "[pitch]")
{
    // A minor melody (A C D E G, with some B and F), slightly out of tune.
    std::vector<double> notes;
    const int mel[] = { 57, 60, 62, 64, 62, 60, 57, 55, 57, 64, 67, 64, 62, 60, 59, 57, 65, 64, 62, 60, 57, 57, 64, 60 };
    for (int rep = 0; rep < 6; ++rep)
        for (int n : mel) for (int k = 0; k < 10; ++k) notes.push_back (n + 0.15 * std::sin (rep + k));
    const auto g = detectKey (notes);
    INFO ("key " << kNoteNames[static_cast<size_t> (g.key)] << (g.minor ? " minor" : " major") << " conf " << g.confidence << " off " << g.offCents);
    CHECK ((g.key == 9 && g.minor));
    CHECK (g.offCents < 12.0);
}

TEST_CASE ("Pitch: Natural fixes each note's centre and keeps the vibrato", "[pitch]")
{
    // G3 sung 35 cents sharp with a +-40 cent vibrato (a 5.5 Hz wobble).
    const double g3 = 196.0;
    const auto x = voiceCurve ([&] (double t) { return g3 * std::pow (2.0, (35.0 + 40.0 * std::sin (2 * std::numbers::pi * 5.5 * t)) / 1200.0); }, 2.5);
    const auto in = track (x, g3, 40000, 110000);
    PitchParams nat; nat.mode = kPitchNatural; nat.amount = 100.0; nat.speedMs = 50.0;
    const auto y = run (nat, x);
    const auto out = track (y, g3, 40000, 110000);
    INFO ("in: mean " << in.first << " spread " << in.second << " / natural: mean " << out.first << " spread " << out.second);
    CHECK (std::abs (out.first) < 6.0);              // centred on G3
    CHECK (out.second > 0.8 * in.second);            // vibrato kept
    CHECK (out.second < 1.25 * in.second);

    PitchParams flat = nat; flat.vibrato = -100.0;   // Vibrato -100 %: laid flat on the note
    const auto f = track (run (flat, x), g3, 40000, 110000);
    PitchParams deep = nat; deep.vibrato = 100.0;    // +100 %: twice as deep
    const auto d = track (run (deep, x), g3, 40000, 110000);
    INFO ("flat: mean " << f.first << " spread " << f.second << " / deep: spread " << d.second);
    CHECK (std::abs (f.first) < 6.0);
    CHECK (f.second < 0.35 * in.second);
    CHECK (d.second > 1.6 * in.second);
}

TEST_CASE ("Pitch: Natural fixes slow drift and doesn't overshoot a scoop", "[pitch]")
{
    // A4 drifting 30 cents flat -> 30 sharp over 2 s.
    const auto x = voiceCurve ([] (double t) { return 440.0 * std::pow (2.0, (-30.0 + 30.0 * t) / 1200.0); }, 2.0);
    PitchParams nat; nat.mode = kPitchNatural; nat.amount = 100.0; nat.speedMs = 30.0;
    const auto y = run (nat, x);
    for (size_t s = 24000; s + 6000 < y.size(); s += 6000)
    {
        INFO ("at " << s << " in " << cents (measureHz (x, s - 1536, 1200), 440.0) << " out " << cents (measureHz (y, s, 1200), 440.0));
        CHECK (std::abs (cents (measureHz (y, s, 1200), 440.0)) < 10.0);
    }

    // A scoop: a semitone below C4 rising onto it over 100 ms, then held. Natural keeps the scoop's
    // shape and never goes past the note.
    const double c4 = 261.63;
    const auto sc = voiceCurve ([&] (double t) { const double u = std::clamp ((t - 0.3) / 0.1, 0.0, 1.0); return c4 * std::pow (2.0, (-1.0 + u) / 12.0); }, 1.2);
    const auto z = run (nat, sc);
    double worst = -1e9;
    for (size_t s = static_cast<size_t> (0.4 * kSr) + 1536; s + 2000 < static_cast<size_t> (0.9 * kSr); s += 240)
        worst = std::max (worst, cents (measureHz (z, s, 1200), c4));
    INFO ("highest point after the scoop " << worst << " cents");
    CHECK (worst < 12.0);
}

TEST_CASE ("Pitch: Robot is hard and flat whatever the Retune knob says", "[pitch]")
{
    const double g3 = 196.0;
    // 15 cents sharp with a +-30 cent vibrato (its peaks stay on G3's side of the halfway point).
    const auto x = voiceCurve ([&] (double t) { return g3 * std::pow (2.0, (15.0 + 30.0 * std::sin (2 * std::numbers::pi * 5.5 * t)) / 1200.0); }, 2.0);
    PitchParams rb; rb.mode = kPitchRobot; rb.amount = 100.0; rb.speedMs = 400.0; rb.humanize = 100.0;
    const auto in = track (x, g3, 40000, 90000);
    const auto out = track (run (rb, x), g3, 40000, 90000);
    INFO ("robot: mean " << out.first << " spread " << out.second << " (in " << in.second << ")");
    CHECK (std::abs (out.first) < 4.0);
    CHECK (out.second < 0.3 * in.second);
}

TEST_CASE ("Pitch: the new scales", "[pitch]")
{
    // D dorian (D E F G A B C): F# (66) isn't in it -> F or G.
    const int t = PitchCorrector::targetNote (66.0, 2, 6, -1);
    CHECK ((t == 65 || t == 67));
    // A blues (A C D D# E G): D# (63) is allowed, B (71) isn't.
    CHECK (PitchCorrector::targetNote (63.1, 9, 9, -1) == 63);
    CHECK (PitchCorrector::targetNote (71.0, 9, 9, -1) != 71);
}

TEST_CASE ("Pitch: breath and rasp in a note don't turn into a buzz", "[pitch]")
{
    // A voice (harmonics under 3.5 kHz) 40 cents sharp with steady breath noise above 2 kHz. Tuning
    // must not chop the noise into a buzz at the new pitch: the > 4.5 kHz band's envelope should
    // pulse at the output pitch no more than the input's does.
    std::mt19937 rng (7);
    std::normal_distribution<double> g (0.0, 1.0);
    Biquad n1, n2; design::apply (n1, design::butterworth (true, 2000.0, kSr)); design::apply (n2, design::butterworth (true, 2000.0, kSr));
    const double f0 = 246.0 * std::pow (2.0, 40.0 / 1200.0);
    std::vector<double> x (static_cast<size_t> (2.0 * kSr));
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        ph += f0 / kSr;
        double s = 0.0;
        for (int h = 1; h * f0 < 3500.0; ++h) s += std::sin (2 * std::numbers::pi * h * ph) / h;
        x[i] = 0.1 * s + 0.03 * n2.process (n1.process (g (rng)));
    }
    auto pulse = [] (const std::vector<double>& y, double f)
    {
        Biquad h1, h2; design::apply (h1, design::butterworth (true, 4500.0, kSr)); design::apply (h2, design::butterworth (true, 4500.0, kSr));
        const double c = design::onePole (0.0003, kSr);
        std::vector<double> e (y.size());
        double env = 0.0;
        for (size_t i = 0; i < y.size(); ++i) { env += (std::abs (h2.process (h1.process (y[i]))) - env) * c; e[i] = env; }
        const size_t a = 24000, b = 90000;
        double m = 0.0; for (size_t i = a; i < b; ++i) m += e[i]; m /= static_cast<double> (b - a);
        double tot = 0.0;
        for (int k = 1; k <= 3; ++k)
        {
            double re = 0.0, im = 0.0;
            for (size_t i = a; i < b; ++i) { const double w = 2 * std::numbers::pi * k * f * static_cast<double> (i) / kSr; re += (e[i] - m) * std::cos (w); im -= (e[i] - m) * std::sin (w); }
            tot += re * re + im * im;
        }
        return 2.0 * std::sqrt (tot) / static_cast<double> (b - a) / m;
    };
    const double target = 440.0 * std::pow (2.0, (59 - 69) / 12.0);   // B3
    for (int mode : { kPitchNatural, kPitchClassic, kPitchRobot })
    {
        PitchParams p; p.mode = mode; p.amount = 100.0; p.speedMs = 10.0;
        const auto y = run (p, x);
        INFO ("mode " << mode << ": pitch " << measureHz (y, 40000) << " Hz, buzz in " << pulse (x, f0) << " out " << pulse (y, target));
        CHECK (std::abs (cents (measureHz (y, 40000), target)) < 5.0);
        CHECK (pulse (y, target) < 0.03);   // was ~0.15 with the noise inside the grains
    }
}

TEST_CASE ("Pitch: MIDI notes, removed notes and transpose", "[pitch]")
{
    // Only / removed notes pick the target (any octave).
    CHECK (PitchCorrector::targetNote (57.3, 0, 0, -1, 0, 1 << 0, 0) == 60);            // MIDI holds a C: A3 -> C4
    CHECK (PitchCorrector::targetNote (57.3, 0, 0, -1, 0, (1 << 0) | (1 << 7), 0) == 55); // C or G held: G3 is nearer
    CHECK (PitchCorrector::targetNote (57.3, 0, 0, -1, 0, 0, 1 << 9) == 58);            // A switched off: A#
    CHECK (PitchCorrector::targetNote (57.3, 9, 2, -1, 0, 0, 1 << 9) == 59);            // A minor without A: B
    CHECK (PitchCorrector::targetNote (57.3, 0, 0, -1, 0, 0, 0xFFF) == 57);             // all off: nearest anyway

    const double a3 = 220.0 * std::pow (2.0, 30.0 / 1200.0);
    const auto x = voiceCurve ([&] (double) { return a3; }, 1.4);
    PitchParams p; p.mode = kPitchRobot; p.amount = 100.0;
    p.onlyNotes = 1 << 0;   // a MIDI C held
    CHECK (std::abs (cents (measureHz (run (p, x), 40000), 261.63)) < 5.0);
    p.onlyNotes = 0; p.transpose = 3;   // A3 + 3 = C4, transposed after tuning
    CHECK (std::abs (cents (measureHz (run (p, x), 40000), 261.63)) < 5.0);
    PitchParams t; t.transpose = -12; t.mode = kPitchNatural;   // Amount 0: just an octave down, untuned
    const auto y = run (t, x);
    CHECK (std::abs (cents (measureHz (y, 40000), a3 / 2.0)) < 6.0);
    CHECK (! PitchCorrector::isNeutral (t));
}
