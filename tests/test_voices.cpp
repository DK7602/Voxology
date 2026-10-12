#include "Signals.h"
#include "vox/Fft.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <random>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

/** A sung A3 (220 Hz, vowel formants) in 1.2 s notes with 0.8 s gaps, over -80 dB room noise. */
std::vector<float> sungA (double seconds)
{
    std::vector<float> x (static_cast<size_t> (seconds * kSr));
    Biquad f1, f2;
    design::apply (f1, design::bell (700.0, 12.0, 3.0, kSr));
    design::apply (f2, design::bell (1800.0, 10.0, 3.0, kSr));
    std::mt19937 rng (9);
    std::normal_distribution<double> w (0.0, 1.0e-4);
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = std::fmod (static_cast<double> (i) / kSr, 2.0);
        const double env = t < 1.2 ? std::min ({ 1.0, t / 0.01, (1.2 - t) / 0.03 }) : 0.0;
        ph += 220.0 / kSr;
        double s = 0.0;
        for (int h = 1; h < 40; ++h) s += std::sin (2.0 * std::numbers::pi * h * ph) / h;
        x[i] = static_cast<float> (0.08 * env * f2.process (f1.process (s)) + w (rng));
    }
    return x;
}

double pitchHz (const std::vector<double>& x, size_t a)
{
    const size_t N = 65536;
    std::vector<double> w (N);
    for (size_t i = 0; i < N; ++i) w[i] = (a + i < x.size() ? x[a + i] : 0.0) * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / N));
    const auto p = powerSpectrum (w.data(), N);
    const double bin = kSr / N;
    double best = -1.0e300, bf = 0.0;
    for (double f = 60.0; f < 1000.0; f += 0.25)
    {
        double h = 0.0;
        for (int k = 1; k <= 5; ++k) { const auto b = static_cast<size_t> (std::lround (k * f / bin)); h += std::log (p[b - 1] + p[b] + p[b + 1] + 1.0e-30); }
        if (h > best) { best = h; bf = f; }
    }
    return bf;
}

double rmsDb (const std::vector<double>& x, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += x[i] * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1.0e-30);
}

/** The voices alone: the chain with them minus the chain without (the lead path is identical). */
std::vector<double> voicesOnly (const std::vector<float>& x, ChainParams with)
{
    ChainParams without = with;
    without.voices.interval = { 0, 0 };
    without.voices.stack = 0;
    without.voices.midi = false;
    const auto a = VocalChain::render ({ x }, kSr, with), b = VocalChain::render ({ x }, kSr, without);
    std::vector<double> d (a[0].size());
    const size_t nch = a.size();
    for (size_t i = 0; i < d.size(); ++i)
    {
        double s = 0.0;
        for (size_t c = 0; c < nch; ++c) s += a[c][i] - b[c][i];
        d[i] = s / static_cast<double> (nch);
    }
    return d;
}

std::vector<double> mono (const std::vector<std::vector<float>>& y)
{
    std::vector<double> m (y[0].size());
    for (size_t i = 0; i < m.size(); ++i)
    {
        double s = 0.0;
        for (const auto& c : y) s += c[i];
        m[i] = s / static_cast<double> (y.size());
    }
    return m;
}

ChainParams harmonyIn (int a, int b)
{
    ChainParams p;
    p.pitch.key = 0;      // C major
    p.pitch.scale = 1;
    p.voices.interval = { a, b };
    p.voices.level = 50.0;
    return p;
}
}

TEST_CASE ("Voices: harmonies land on the right notes, in key", "[voices]")
{
    const auto x = sungA (6.0);
    struct Case { int interval; double hz; const char* name; };
    for (const auto& c : { Case { 1, 261.63, "3rd up: C4" }, Case { 2, 329.63, "5th up: E4" }, Case { 4, 174.61, "3rd down: F3" },
                           Case { 6, 146.83, "5th down: D3" }, Case { 7, 110.0, "octave down: A2" } })
    {
        INFO (c.name);
        const auto v = voicesOnly (x, harmonyIn (c.interval, 0));
        CHECK (pitchHz (v, static_cast<size_t> (2.1 * kSr)) == Approx (c.hz).epsilon (0.01));
    }
}

TEST_CASE ("Voices: level follows the lead, start in time with it, and stay out of the gaps", "[voices]")
{
    const auto x = sungA (8.0);
    auto p = harmonyIn (1, 6);   // 3rd up + 5th down
    const auto v = voicesOnly (x, p);
    ChainParams leadOnly = p;
    leadOnly.voices.interval = { 0, 0 };
    const auto lead = mono (VocalChain::render ({ x }, kSr, leadOnly));
    for (int note = 1; note < 4; ++note)
    {
        const auto a = static_cast<size_t> ((note * 2.0 + 0.3) * kSr), b = static_cast<size_t> ((note * 2.0 + 1.0) * kSr);
        INFO ("note " << note);
        // Two voices at 50 % each, panned apart: together about 7 dB under the lead (in mono).
        CHECK (rmsDb (v, a, b) - rmsDb (lead, a, b) == Approx (-7.5).margin (2.5));   // a backing level (summed to mono)
        // In the gap after the note (from 150 ms after it ends): nothing (no room noise pumped up).
        const auto g0 = static_cast<size_t> ((note * 2.0 + 1.35) * kSr), g1 = static_cast<size_t> ((note * 2.0 + 1.95) * kSr);
        CHECK (rmsDb (v, g0, g1) < -70.0);
        // Onset: the voices reach half their level within 15 ms of the lead doing so.
        auto onset = [&] (const std::vector<double>& s)
        {
            const auto n0 = static_cast<size_t> (note * 2.0 * kSr) - 2400;
            const double ref = std::pow (10.0, rmsDb (s, a, b) / 20.0) * 0.5;
            double e = 0.0;
            for (size_t i = n0; i < n0 + 9600; ++i)
            {
                e += (s[i] * s[i] - e) * 0.02;
                if (std::sqrt (e) > ref) return static_cast<double> (i - n0) / kSr;
            }
            return 1.0;
        };
        CHECK (std::abs (onset (v) - onset (lead)) < 0.015);
    }
}

TEST_CASE ("Formant: the lead's tone moves, its note doesn't", "[voices]")
{
    const auto x = sungA (4.0);
    ChainParams up, plain;
    up.pitch.formant = 4.0;
    const auto a = mono (VocalChain::render ({ x }, kSr, plain)), b = mono (VocalChain::render ({ x }, kSr, up));
    CHECK (pitchHz (b, static_cast<size_t> (0.1 * kSr)) == Approx (220.0).epsilon (0.01));
    auto centroid = [] (const std::vector<double>& s)
    {
        const auto p = powerSpectrum (s.data() + static_cast<size_t> (0.3 * kSr), 4096);
        double num = 0.0, den = 0.0;
        for (size_t k = 1; k < p.size(); ++k) { const double f = static_cast<double> (k) * kSr / 4096.0; if (f > 6000.0) break; num += f * p[k]; den += p[k]; }
        return num / den;
    };
    CHECK (centroid (b) > 1.15 * centroid (a));
    const auto out = VocalChain::render ({ x }, kSr, up);
    for (float v : out[0]) REQUIRE (std::isfinite (v));
}

TEST_CASE ("Voices: Unison = a second voice on the lead's own notes, a few cents off and a little later", "[voices]")
{
    REQUIRE (std::string (kHarmonyIntervals[static_cast<size_t> (kHarmonyUnison)].name) == "Unison");
    const auto x = sungA (6.0);
    const double a3 = 220.0;
    // Voice 1 alone sits 9 cents above the note, voice 2 alone 9 cents below: the lead's note, not a harmony.
    const auto v1 = voicesOnly (x, harmonyIn (kHarmonyUnison, 0)), v2 = voicesOnly (x, harmonyIn (0, kHarmonyUnison));
    const double f1 = pitchHz (v1, static_cast<size_t> (2.1 * kSr)), f2 = pitchHz (v2, static_cast<size_t> (2.1 * kSr));
    INFO ("voice 1 " << f1 << " Hz, voice 2 " << f2 << " Hz");
    CHECK (f1 == Approx (a3 * std::pow (2.0, 9.0 / 1200.0)).margin (0.6));
    CHECK (f2 == Approx (a3 * std::pow (2.0, -9.0 / 1200.0)).margin (0.6));
    CHECK (f1 > f2 + 1.0);
    // Both: heard at a backing level, starting ~20 - 30 ms after the lead (a double, not a copy on top of it).
    auto p = harmonyIn (kHarmonyUnison, kHarmonyUnison);
    const auto v = voicesOnly (x, p);
    ChainParams leadOnly = p;
    leadOnly.voices.interval = { 0, 0 };
    const auto lead = mono (VocalChain::render ({ x }, kSr, leadOnly));
    const auto a = static_cast<size_t> (2.3 * kSr), b = static_cast<size_t> (3.0 * kSr);
    CHECK (rmsDb (v, a, b) - rmsDb (lead, a, b) == Approx (-7.5).margin (3.5));
    auto onset = [&] (const std::vector<double>& s)
    {
        const auto n0 = static_cast<size_t> (2.0 * kSr) - 2400;
        const double ref = std::pow (10.0, rmsDb (s, a, b) / 20.0) * 0.5;
        double e = 0.0;
        for (size_t i = n0; i < n0 + 9600; ++i)
        {
            e += (s[i] * s[i] - e) * 0.02;
            if (std::sqrt (e) > ref) return static_cast<double> (i - n0) / kSr;
        }
        return 1.0;
    };
    const double lag = onset (v) - onset (lead);
    INFO ("lag " << lag * 1000.0 << " ms");
    CHECK (lag > 0.012);
    CHECK (lag < 0.040);
}

namespace {
/** The chain run live (256-sample blocks, like a host): `at (seconds)` gives the params from that point of the input on.
    Lined up with the input (latency removed). */
std::vector<std::vector<double>> live (const std::vector<std::vector<float>>& in, const std::function<ChainParams (double)>& at)
{
    const int nch = static_cast<int> (in.size()), n = static_cast<int> (in[0].size());
    VocalChain chain;
    chain.setParams (at (0.0));
    chain.prepare (kSr, nch);
    const int lat = chain.latencySamples();
    std::vector<std::vector<double>> out (in.size(), std::vector<double> (in[0].size(), 0.0));
    std::vector<std::vector<double>> buf (in.size(), std::vector<double> (256));
    std::array<double*, 2> ptr { buf[0].data(), buf[static_cast<size_t> (nch - 1)].data() };
    for (int start = 0; start < n + lat; start += 256)
    {
        const int len = std::min (256, n + lat - start);
        chain.setParams (at (start / kSr));
        for (size_t c = 0; c < in.size(); ++c)
            for (int i = 0; i < len; ++i) buf[c][static_cast<size_t> (i)] = start + i < n ? in[c][static_cast<size_t> (start + i)] : 0.0;
        chain.process (ptr.data(), nch, len);
        for (size_t c = 0; c < in.size(); ++c)
            for (int i = 0; i < len; ++i)
                if (const int o = start + i - lat; o >= 0 && o < n) out[c][static_cast<size_t> (o)] = buf[c][static_cast<size_t> (i)];
    }
    return out;
}

/** Each channel: the voices alone (with them minus without; the lead's path doesn't depend on them). */
std::vector<std::vector<double>> voicesLive (const std::vector<std::vector<float>>& in, const std::function<ChainParams (double)>& at)
{
    auto a = live (in, at);
    const auto b = live (in, [&] (double t) { auto p = at (t); p.voices.interval = { 0, 0 }; p.voices.stack = 0; p.voices.midi = false; return p; });
    for (size_t c = 0; c < a.size(); ++c)
        for (size_t i = 0; i < a[c].size(); ++i) a[c][i] -= b[c][i];
    return a;
}

/** Level (dB) of the partials within 4 Hz of f, in 0.68 s from `from`. */
double toneDb (const std::vector<double>& x, size_t from, double f)
{
    const size_t N = 32768;
    std::vector<double> w (N);
    for (size_t i = 0; i < N; ++i) w[i] = x[from + i] * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / N));
    const auto p = powerSpectrum (w.data(), N);
    const double bin = kSr / N;
    double e = 0.0;
    for (auto k = static_cast<size_t> ((f - 4.0) / bin); k <= static_cast<size_t> ((f + 4.0) / bin); ++k) e += p[k];
    return 10.0 * std::log10 (e + 1.0e-30);
}

/** Clicks: the biggest jump in the top end (over 5 kHz) between a and b. */
double topPeak (const std::vector<double>& x, size_t a, size_t b)
{
    Biquad h1, h2;
    design::apply (h1, design::butterworth (true, 5000.0, kSr));
    design::apply (h2, design::butterworth (true, 5000.0, kSr));
    double peak = 0.0;
    for (size_t i = a - 4800; i < b; ++i)
    {
        const double y = h2.process (h1.process (x[i]));
        if (i >= a) peak = std::max (peak, std::abs (y));
    }
    return peak;
}

ChainParams chord (std::initializer_list<int> notes)
{
    auto p = harmonyIn (0, 0);
    p.voices.midi = true;
    size_t k = 0;
    for (int n : notes) p.voices.notes[k++] = n;
    return p;
}
}

TEST_CASE ("Voices: Stack = 2 / 4 / 6 more singers on the lead's notes, both sides", "[voices]")
{
    VoicesParams v;
    v.stack = 1;
    CHECK (HarmonyVoices::activeVoices (v) == 2);
    v.stack = 3;
    CHECK (HarmonyVoices::activeVoices (v) == 6);
    v.interval = { 1, kHarmonyUnison };
    CHECK (HarmonyVoices::activeVoices (v) == kMaxVoices);
    v.midi = true;
    v.notes = { 67, 60, 64, 60, -1, -1, -1, -1 };   // (C twice: one voice)
    CHECK (HarmonyVoices::activeVoices (v) == 3);
    v.notes.fill (-1);
    CHECK (HarmonyVoices::isNeutral (v));

    const auto x = sungA (6.0);
    const std::vector<std::vector<float>> st { x, x };
    auto leadP = harmonyIn (0, 0);
    const auto lead = live (st, [&] (double) { return leadP; });
    const auto a = static_cast<size_t> (2.3 * kSr), b = static_cast<size_t> (3.0 * kSr);
    std::array<double, 4> level {};
    for (int stack = 1; stack <= 3; ++stack)
    {
        INFO ("stack " << HarmonyVoices::kStackCents.size() << " index " << stack);
        auto p = harmonyIn (0, 0);
        p.voices.stack = stack;
        const auto v2 = voicesLive (st, [&] (double) { return p; });
        std::vector<double> m (v2[0].size());
        for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5 * (v2[0][i] + v2[1][i]);
        CHECK (pitchHz (m, static_cast<size_t> (2.1 * kSr)) == Approx (220.0).epsilon (0.015));   // the lead's note
        CHECK (std::abs (rmsDb (v2[0], a, b) - rmsDb (v2[1], a, b)) < 1.5);                       // as much left as right
        level[static_cast<size_t> (stack)] = rmsDb (m, a, b) - rmsDb (lead[0], a, b);
        CHECK (level[static_cast<size_t> (stack)] > -14.0);
        CHECK (level[static_cast<size_t> (stack)] < 0.0);
        CHECK (rmsDb (m, static_cast<size_t> (3.55 * kSr), static_cast<size_t> (3.95 * kSr)) < -70.0);   // nothing in the gaps
    }
    // More singers: bigger, not much louder.
    CHECK (level[3] > level[1] - 1.0);
    CHECK (level[3] < level[1] + 5.0);
}

TEST_CASE ("Voices MIDI: the voices sing the notes held, whatever is sung; none held = untouched", "[voices]")
{
    const auto x = sungA (6.0);
    const auto v = voicesOnly (x, chord ({ 60, 64, 67 }));   // C major over a sung A
    const auto at = static_cast<size_t> (2.2 * kSr);
    const double sungA3 = toneDb (v, at, 220.0);
    for (const double f : { 261.63, 329.63, 392.0 })
    {
        INFO (f << " Hz: " << toneDb (v, at, f) << " dB, A3 " << sungA3 << " dB");
        CHECK (toneDb (v, at, f) > sungA3 + 15.0);
    }
    // MIDI on and nothing held: not a sample changes.
    const auto off = VocalChain::render ({ x, x }, kSr, harmonyIn (0, 0)), none = VocalChain::render ({ x, x }, kSr, chord ({}));
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < off[c].size(); ++i) REQUIRE (none[c][i] == off[c][i]);
}

TEST_CASE ("Voices MIDI: chord changes slide, new notes come in, released ones fade out, without clicks", "[voices]")
{
    const auto x = sungA (6.0);
    const std::vector<std::vector<float>> st { x, x };
    const double change = 2.6;   // in the middle of a sung note (2.0 - 3.2 s)
    const auto held = voicesLive (st, [] (double) { return chord ({ 60, 64, 67 }); });
    const auto steady = topPeak (held[0], static_cast<size_t> (2.2 * kSr), static_cast<size_t> (2.55 * kSr));
    const auto c0 = static_cast<size_t> ((change - 0.05) * kSr), c1 = static_cast<size_t> ((change + 0.1) * kSr);
    auto mid = [] (const std::vector<std::vector<double>>& v)
    {
        std::vector<double> m (v[0].size());
        for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5 * (v[0][i] + v[1][i]);
        return m;
    };

    // C major -> D minor: each voice slides a step, no gap, no jump.
    const auto v = voicesLive (st, [&] (double t) { return t < change ? chord ({ 60, 64, 67 }) : chord ({ 62, 65, 69 }); });
    const auto m = mid (v);
    const auto after = static_cast<size_t> ((change + 0.08) * kSr);
    const double dm = std::min ({ toneDb (m, after, 293.66), toneDb (m, after, 349.23), toneDb (m, after, 440.0) });
    const double cm = std::max ({ toneDb (m, after, 261.63), toneDb (m, after, 329.63), toneDb (m, after, 392.0) });
    INFO ("D minor " << dm << " dB, C major left " << cm << " dB; top end held " << steady << ", at the change " << topPeak (v[0], c0, c1));
    CHECK (dm > cm + 30.0);
    CHECK (topPeak (v[0], c0, c1) < 1.5 * steady);
    CHECK (topPeak (v[1], c0, c1) < 1.5 * steady);
    CHECK (rmsDb (m, c0, c1) > rmsDb (mid (held), c0, c1) - 3.0);   // no gap

    // Released mid-note: fades out, silent after.
    const auto rel = voicesLive (st, [&] (double t) { return t < change ? chord ({ 60, 64, 67 }) : chord ({}); });
    CHECK (topPeak (rel[0], c0, c1) < 1.5 * steady);
    CHECK (rmsDb (rel[0], static_cast<size_t> ((change + 0.15) * kSr), static_cast<size_t> ((change + 0.5) * kSr)) < -70.0);

    // A note added to a held chord, and a chord from nothing mid-note: they come in within 0.1 s at their level, no burst.
    const auto add = voicesLive (st, [&] (double t) { return t < change ? chord ({ 64, 67 }) : chord ({ 64, 60, 67 }); });
    CHECK (topPeak (add[0], c0, c1) < 1.5 * steady);
    CHECK (topPeak (add[1], c0, c1) < 1.5 * steady);
    CHECK (toneDb (mid (add), after, 261.63) > toneDb (mid (held), after, 261.63) - 3.0);
    const auto in = voicesLive (st, [&] (double t) { return t < change ? chord ({}) : chord ({ 60, 64, 67 }); });
    const auto inM = mid (in);
    INFO ("from nothing: top " << topPeak (in[0], c0, c1) << ", level " << rmsDb (inM, after, after + 9600) << " vs held " << rmsDb (mid (held), after, after + 9600));
    CHECK (topPeak (in[0], c0, c1) < 1.5 * steady);
    CHECK (std::abs (rmsDb (inM, after, after + 9600) - rmsDb (mid (held), after, after + 9600)) < 3.0);
    CHECK (rmsDb (inM, static_cast<size_t> (2.2 * kSr), static_cast<size_t> (2.5 * kSr)) < -70.0);
}
