// Honey Tune wobble audit: does KEEP DRIFT 0 + KEEP VIBRATO 0 give a steady note, and what does the render add?
//
// A sung-like line (harmonics, three formants, a little breath) whose notes wobble the way a shaky voice does:
// irregular movement between 2 and 7 Hz (several components, their speed and depth drifting) of 20 - 80 cents, on top
// of a slow drift, some notes sung off the key. Honey Tune's engine (analyse -> findNotes -> edits -> render) runs on
// it, then an independent tracker (normalised cross-correlation, 30 ms windows every 5 ms, searched near the note)
// measures the movement left inside each note against the input's.
//
// Also: a steady note moved +2 / -3 semitones (does the shifter add a warble or a level flutter?), a stereo clip with
// different left / right content (rendered per channel), and Voxology's live Pitch after Honey Tune (the same track).
//
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/honey_wobble_audit.cpp <build>/dsp/libvox_dsp.a -lpthread -o honey_wobble_audit
// Run:   ./honey_wobble_audit [seed]
#include "vox/FilterDesign.h"
#include "vox/HoneyTune.h"
#include "vox/PitchCorrector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <string>
#include <tuple>
#include <vector>

using namespace vox;

namespace {
constexpr double sr = 48000.0;

struct Rng
{
    explicit Rng (unsigned s) : g (s) {}
    double uniform() { return (static_cast<double> (g() & 0xffffffffu) + 0.5) / 4294967296.0; }
    double normal() { const double r = std::sqrt (-2.0 * std::log (uniform())), a = 2.0 * std::numbers::pi * uniform(); return r * std::cos (a); }
    double range (double a, double b) { return a + (b - a) * uniform(); }
    std::mt19937 g;
};

struct SungNote { double start, end, midi; };
struct Line { std::vector<float> audio; std::vector<double> trueMidi; std::vector<SungNote> notes; };

/** wobbleCents: the wobble's peak (one side) for each note is drawn from [lo, hi]. driftCents: the slow drift's size. */
Line wobblyLine (unsigned seed, double lo = 20.0, double hi = 80.0, double driftCents = 25.0, double fLo = 2.0, double fHi = 7.0,
                 bool steady = false, const double* melody = nullptr)
{
    Rng rng (seed);
    static constexpr double cLine[] = { 60.0, 62.3, 64.0, 67.0, 65.2, 64.0, 62.0, 60.0 };
    const double* mids = melody != nullptr ? melody : cLine;
    const double lens[] = { 0.9, 0.7, 1.3, 0.6, 1.1, 0.8, 1.2, 1.0 };
    Line L;
    std::array<Biquad, 3> formants;
    design::apply (formants[0], design::bell (700.0, 14.0, 2.5, sr));
    design::apply (formants[1], design::bell (1200.0, 10.0, 3.0, sr));
    design::apply (formants[2], design::bell (2600.0, 8.0, 4.0, sr));
    Biquad tilt; design::apply (tilt, design::butterworth (false, 4000.0, sr));
    double ph = 0.0;
    auto put = [&] (double v, double m) { double y = tilt.process (v); for (auto& f : formants) y = f.process (y); L.audio.push_back (static_cast<float> (0.12 * y)); L.trueMidi.push_back (m); };
    for (size_t n = 0; n < 8; ++n)
    {
        // A breath before the note.
        const double gap = rng.range (0.12, 0.3);
        for (size_t i = 0; i < static_cast<size_t> (gap * sr); ++i) put (0.004 * rng.normal(), 0.0);
        const auto len = static_cast<size_t> (lens[n] * sr);
        const double start = static_cast<double> (L.audio.size());
        // The wobble: three components between fLo and fHi Hz, their speed and depth wandering.
        struct Comp { double f, a, ph, fmRate, fmPh, amRate, amPh; };
        std::array<Comp, 3> comps;
        for (auto& c : comps) c = { rng.range (fLo, fHi), rng.range (0.5, 1.0), rng.range (0.0, 6.283), rng.range (0.3, 1.2), rng.range (0, 6.283), rng.range (0.4, 1.5), rng.range (0, 6.283) };
        const double depth = steady ? 0.0 : rng.range (lo, hi);
        // Normalise the wobble to `depth` cents peak (measured over the note).
        std::vector<double> w (len, 0.0), d (len, 0.0);
        std::array<double, 3> cph {};
        for (size_t k = 0; k < 3; ++k) cph[k] = comps[k].ph;
        double peak = 1e-9;
        for (size_t i = 0; i < len; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            double v = 0.0;
            for (size_t k = 0; k < 3; ++k)
            {
                const auto& c = comps[k];
                const double f = c.f * (1.0 + 0.18 * std::sin (2 * std::numbers::pi * c.fmRate * t + c.fmPh));
                cph[k] += 2 * std::numbers::pi * f / sr;
                v += c.a * (0.6 + 0.4 * std::sin (2 * std::numbers::pi * c.amRate * t + c.amPh)) * std::sin (cph[k]);
            }
            w[i] = v;
            peak = std::max (peak, std::abs (v));
        }
        // Slow drift: smoothed noise (two one-poles at ~0.6 Hz) plus a sag over the note.
        double s1 = 0.0, s2 = 0.0, dpk = 1e-9;
        const double k1 = 1.0 - std::exp (-2 * std::numbers::pi * 0.6 / sr);
        for (size_t i = 0; i < len; ++i) { s1 += (rng.normal() - s1) * k1; s2 += (s1 - s2) * k1; d[i] = s2; }
        double mean = 0.0; for (double v : d) mean += v; mean /= static_cast<double> (len);
        for (auto& v : d) { v -= mean; dpk = std::max (dpk, std::abs (v)); }
        const double sag = steady ? 0.0 : rng.range (-1.0, 1.0);
        for (size_t i = 0; i < len; ++i)
        {
            const double t = static_cast<double> (i) / static_cast<double> (len);
            const double cents = depth * w[i] / peak + (steady ? 0.0 : driftCents * (0.6 * d[i] / dpk + 0.4 * sag * (t - 0.5) * 2.0));
            const double m = mids[n] + cents / 100.0;
            const double f = 440.0 * std::pow (2.0, (m - 69.0) / 12.0) * (1.0 + 0.002 * rng.normal());   // a little jitter
            ph += f / sr;
            double v = 0.0;
            for (int h = 1; h * f < 9000.0; ++h) v += std::sin (2 * std::numbers::pi * h * ph) / std::pow (h, 0.7);
            const double tt = static_cast<double> (i) / sr;
            const double env = std::min ({ 1.0, tt / 0.03, (lens[n] - tt) / 0.04 });
            put (env * v + 0.01 * rng.normal(), m);
        }
        L.notes.push_back ({ start, start + static_cast<double> (len), mids[n] });
    }
    for (size_t i = 0; i < static_cast<size_t> (0.3 * sr); ++i) put (0.0, 0.0);
    return L;
}

/** Independent tracker: NCC over 30 ms every 5 ms in [a, b) samples, lags within +-4 semitones of guess. MIDI per hop. */
std::vector<double> trackNote (const std::vector<float>& x, double a, double b, double guess)
{
    std::vector<double> out;
    const double hz = 440.0 * std::pow (2.0, (guess - 69.0) / 12.0);
    const int minLag = static_cast<int> (sr / (hz * std::pow (2.0, 4.0 / 12.0))), maxLag = static_cast<int> (sr / (hz * std::pow (2.0, -4.0 / 12.0))) + 1;
    const auto win = static_cast<size_t> (0.03 * sr);
    for (double c = a; c + static_cast<double> (win) + maxLag + 2 < b; c += 0.005 * sr)
    {
        const auto from = static_cast<size_t> (c);
        std::vector<double> r (static_cast<size_t> (maxLag + 2), -1.0);
        for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
        {
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = from; i < from + win; ++i) { const double p = x[i], q = x[i + static_cast<size_t> (lag)]; xy += p * q; xx += p * p; yy += q * q; }
            r[static_cast<size_t> (lag)] = xy / std::sqrt (xx * yy + 1e-30);
        }
        int bl = minLag;
        for (int lag = minLag; lag <= maxLag; ++lag) if (r[static_cast<size_t> (lag)] > r[static_cast<size_t> (bl)]) bl = lag;
        const double p = r[static_cast<size_t> (bl - 1)], q = r[static_cast<size_t> (bl)], s = r[static_cast<size_t> (bl + 1)], den = p - 2 * q + s;
        const double lag = bl + (std::abs (den) > 1e-12 ? 0.5 * (p - s) / den : 0.0);
        out.push_back (69.0 + 12.0 * std::log2 (sr / lag / 440.0));
    }
    return out;
}

struct Wobble { double rms = 0, pp = 0, slow = 0, fast = 0, centre = 0; };
/** Movement inside a note (cents): rms around its median, p95 - p5, the slow part (moving average over 300 ms) and the rest. */
Wobble wobbleOf (const std::vector<double>& m)
{
    Wobble w;
    if (m.size() < 10) return w;
    auto s = m;
    std::sort (s.begin(), s.end());
    w.centre = s[s.size() / 2];
    std::vector<double> d;
    for (double v : m) d.push_back (100.0 * (v - w.centre));
    auto sd = d;
    std::sort (sd.begin(), sd.end());
    w.pp = sd[static_cast<size_t> (0.95 * static_cast<double> (sd.size() - 1))] - sd[static_cast<size_t> (0.05 * static_cast<double> (sd.size() - 1))];
    double e = 0, es = 0, ef = 0;
    const int half = 30;   // +-150 ms of 5 ms hops
    for (size_t i = 0; i < d.size(); ++i)
    {
        double sum = 0; int n = 0;
        for (int j = static_cast<int> (i) - half; j <= static_cast<int> (i) + half; ++j)
            if (j >= 0 && j < static_cast<int> (d.size())) { sum += d[static_cast<size_t> (j)]; ++n; }
        const double slow = sum / n;
        e += d[i] * d[i]; es += slow * slow; ef += (d[i] - slow) * (d[i] - slow);
    }
    w.rms = std::sqrt (e / static_cast<double> (d.size()));
    w.slow = std::sqrt (es / static_cast<double> (d.size()));
    w.fast = std::sqrt (ef / static_cast<double> (d.size()));
    return w;
}

/** Level flutter inside a note: the 10 ms RMS envelope (dB), its rms around a 200 ms average. */
double flutterDb (const std::vector<float>& x, double a, double b)
{
    std::vector<double> env;
    const auto hop = static_cast<size_t> (0.005 * sr), win = static_cast<size_t> (0.01 * sr);
    for (auto c = static_cast<size_t> (a); c + win < static_cast<size_t> (b); c += hop)
    {
        double e = 0; for (size_t i = c; i < c + win; ++i) e += static_cast<double> (x[i]) * x[i];
        env.push_back (10.0 * std::log10 (e / static_cast<double> (win) + 1e-20));
    }
    double ss = 0; int n = 0;
    for (size_t i = 0; i < env.size(); ++i)
    {
        double sum = 0; int k = 0;
        for (int j = static_cast<int> (i) - 20; j <= static_cast<int> (i) + 20; ++j) if (j >= 0 && j < static_cast<int> (env.size())) { sum += env[static_cast<size_t> (j)]; ++k; }
        const double d = env[i] - sum / k;
        ss += d * d; ++n;
    }
    return n ? std::sqrt (ss / n) : 0.0;
}

struct Summary { double rms = 0, pp = 0, slow = 0, fast = 0, flutter = 0, centreErr = 0; int notes = 0; };
/** Each sung note's middle (80 ms in from each end) tracked in `y`; centreErr vs `want` (MIDI per note, < 0: skip). */
Summary measure (const std::vector<float>& y, const Line& L, const std::vector<double>& want = {}, bool print = false, const char* tag = "")
{
    Summary s;
    for (size_t k = 0; k < L.notes.size(); ++k)
    {
        const auto& n = L.notes[k];
        const double guess = want.empty() ? n.midi : want[k];
        const auto m = trackNote (y, n.start + 0.08 * sr, n.end - 0.08 * sr, guess);
        const auto w = wobbleOf (m);
        const double fl = flutterDb (y, n.start + 0.08 * sr, n.end - 0.08 * sr);
        if (print) std::printf ("   %s note %zu: wobble rms %5.1f c, p5-p95 %5.1f c (slow %5.1f, fast %5.1f), centre %6.2f, level flutter %.2f dB\n", tag, k, w.rms, w.pp, w.slow, w.fast, w.centre, fl);
        s.rms += w.rms; s.pp += w.pp; s.slow += w.slow; s.fast += w.fast; s.flutter += fl;
        if (! want.empty()) s.centreErr += std::abs (w.centre - want[k]) * 100.0;
        ++s.notes;
    }
    const double n = std::max (1, s.notes);
    s.rms /= n; s.pp /= n; s.slow /= n; s.fast /= n; s.flutter /= n; s.centreErr /= n;
    return s;
}

void line (const char* name, const Summary& s)
{
    std::printf ("%-46s wobble rms %5.1f c | p5-p95 %5.1f c | slow %5.1f | fast %5.1f | level flutter %.2f dB | centre err %.1f c\n",
                 name, s.rms, s.pp, s.slow, s.fast, s.flutter, s.centreErr);
}

/** The engine's notes (findNotes) that overlap sung note k. */
std::vector<honey::Note*> notesIn (std::vector<honey::Note>& found, const SungNote& n)
{
    std::vector<honey::Note*> out;
    for (auto& f : found) if (f.end > n.start && f.start < n.end) out.push_back (&f);
    return out;
}

/** Voxology's live Pitch on a clip (latency removed). */
std::vector<float> livePitch (const std::vector<float>& x, int mode, double speedMs, double humanize, double amount, int key, int scale)
{
    PitchCorrector pc;
    PitchParams p;
    p.amount = amount; p.mode = mode; p.speedMs = speedMs; p.humanize = humanize; p.key = key; p.scale = scale;
    pc.setParams (p);
    pc.prepare (sr, 1);
    const auto lat = static_cast<size_t> (pc.latencySamples());
    std::vector<double> buf (256);
    std::vector<float> y (x.size(), 0.0f);
    for (size_t s = 0; s < x.size() + lat; s += buf.size())
    {
        const size_t len = std::min (buf.size(), x.size() + lat - s);
        for (size_t i = 0; i < len; ++i) buf[i] = s + i < x.size() ? x[s + i] : 0.0;
        double* ptr = buf.data();
        pc.process (&ptr, 1, static_cast<int> (len));
        for (size_t i = 0; i < len; ++i) if (s + i >= lat && s + i - lat < y.size()) y[s + i - lat] = static_cast<float> (buf[i]);
    }
    return y;
}
}

/** A real vocal (raw mono doubles at 48 kHz): every note >= 0.3 s, snapped to the key, DRIFT 0 + VIBRATO 0; the movement
    left inside each note (independent tracker, guided by the note) vs as sung. Also: notes split in two by their own
    wobble (neighbours with no gap, less than a semitone apart). */
int realVocal (const char* path, int key, int scale)
{
    FILE* f = std::fopen (path, "rb");
    if (f == nullptr) { std::printf ("can't read %s\n", path); return 1; }
    std::vector<float> x;
    double v;
    while (std::fread (&v, 8, 1, f) == 1) x.push_back (static_cast<float> (v));
    std::fclose (f);
    const auto track = honey::analyse (x, sr, true);
    auto notes = honey::findNotes (track);
    int joined = 0;
    for (size_t i = 1; i < notes.size(); ++i)
        if (notes[i].firstReading <= notes[i - 1].lastReading + 1 && std::abs (notes[i].pitch - notes[i - 1].pitch) < 1.0) ++joined;
    auto edited = notes;
    honey::snapToKey (edited, key, scale, 1.0);
    for (auto& n : edited) { n.drift = 0; n.vibrato = 0; }
    const auto y = honey::render (x, sr, track, edited);
    // Per note >= 0.3 s: the movement left in `z` (tracked near `want`), averaged; worst-10 % too.
    auto score = [&] (const char* name, const std::vector<float>& z, bool atTarget)
    {
        Summary s;
        std::vector<double> each;
        for (size_t k = 0; k < notes.size(); ++k)
        {
            const auto& n = notes[k];
            if ((n.end - n.start) / sr < 0.3) continue;
            const double want = atTarget ? edited[k].target : n.pitch;
            const auto w = wobbleOf (trackNote (z, n.start + 0.06 * sr, n.end - 0.06 * sr, want));
            s.rms += w.rms; s.pp += w.pp; s.slow += w.slow; s.fast += w.fast;
            s.flutter += flutterDb (z, n.start + 0.06 * sr, n.end - 0.06 * sr);
            s.centreErr += atTarget ? std::abs (w.centre - want) * 100.0 : 0.0;
            each.push_back (w.rms);
            ++s.notes;
        }
        const double c = std::max (1, s.notes);
        s.rms /= c; s.pp /= c; s.slow /= c; s.fast /= c; s.flutter /= c; s.centreErr /= c;
        line (name, s);
        std::sort (each.begin(), each.end());
        if (! each.empty()) std::printf ("%-46s   wobble rms p90 %.1f c, worst %.1f c\n", "", each[static_cast<size_t> (0.9 * static_cast<double> (each.size() - 1))], each.back());
    };
    int count = 0;
    for (const auto& n : notes) count += (n.end - n.start) / sr >= 0.3 ? 1 : 0;
    std::printf ("%s: %zu notes, %d >= 0.3 s, %d neighbours split by their own movement\n", path, notes.size(), count, joined);
    score ("  as sung", x, false);
    score ("  Honey Tune: snap + DRIFT 0 + VIBRATO 0", y, true);
    struct Cfg { const char* name; int mode; double speed, hum, amount; };
    const Cfg cfgs[] = { { "Natural 25 ms (R&B)", kPitchNatural, 25, 50, 100 }, { "Classic 5 ms (Melodic)", kPitchClassic, 5, 10, 100 },
                         { "Classic 10 ms (Trap Lead)", kPitchClassic, 10, 20, 100 } };
    for (const auto& c : cfgs)
    {
        char name[96];
        std::snprintf (name, sizeof name, "  Voxology %s alone", c.name);
        score (name, livePitch (x, c.mode, c.speed, c.hum, c.amount, key, scale), true);
        std::snprintf (name, sizeof name, "  Honey Tune flat + Voxology %s", c.name);
        score (name, livePitch (y, c.mode, c.speed, c.hum, c.amount, key, scale), true);
    }
    // Honey Tune keeping the movement (the defaults: snap only), then Voxology.
    {
        auto kept = notes;
        honey::snapToKey (kept, key, scale, 1.0);
        const auto yk = honey::render (x, sr, track, kept);
        score ("  Honey Tune snap only (keeps movement)", yk, true);
        score ("  ... + Voxology Classic 5 ms", livePitch (yk, kPitchClassic, 5, 10, 100, key, scale), true);
    }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 1 && std::string (argv[1]).find (".f64") != std::string::npos)
        return realVocal (argv[1], argc > 2 ? std::atoi (argv[2]) : 0, argc > 3 ? std::atoi (argv[3]) : 0);
    const unsigned seed = argc > 1 ? static_cast<unsigned> (std::atoi (argv[1])) : 7u;
    const auto L = wobblyLine (seed);
    const auto& x = L.audio;
    std::printf ("Wobbly line (seed %u): %zu notes, %.1f s, wobble 20 - 80 c peak at 2 - 7 Hz + drift\n", seed, L.notes.size(), static_cast<double> (x.size()) / sr);

    const auto track = honey::analyse (x, sr, true);
    auto notes = honey::findNotes (track);
    std::printf ("findNotes: %zu notes for %zu sung (", notes.size(), L.notes.size());
    for (const auto& n : L.notes) { std::vector<honey::Note> tmp = notes; std::printf ("%zu ", notesIn (tmp, n).size()); }
    std::printf ("per sung note)\n");
    // The analysis' own error vs the true pitch (what the render can't correct).
    {
        double e = 0; int k = 0;
        for (size_t i = 0; i < track.time.size(); ++i)
        {
            const auto t = static_cast<size_t> (track.time[i]);
            if (track.midi[i] <= 0 || t >= L.trueMidi.size() || L.trueMidi[t] <= 0) continue;
            const double d = 100 * (track.midi[i] - L.trueMidi[t]);
            if (std::abs (d) < 300) { e += d * d; ++k; }
        }
        std::printf ("analysis vs true pitch: rms %.1f c over %d readings\n", std::sqrt (e / std::max (1, k)), k);
    }

    auto snapped = notes;
    honey::snapToKey (snapped, 0, 1, 1.0);   // C major
    std::vector<double> keyNotes;
    for (const auto& n : L.notes) keyNotes.push_back (PitchCorrector::targetNote (n.midi, 0, 1, -1));

    line ("input", measure (x, L, {}, argc > 2, "in"));
    auto run = [&] (const char* name, double drift, double vib, bool snap, bool print = false)
    {
        auto e = snap ? snapped : notes;
        for (auto& n : e) { n.drift = drift; n.vibrato = vib; n.edited = true; }
        const auto y = honey::render (x, sr, track, e);
        line (name, measure (y, L, snap ? keyNotes : std::vector<double> {}, print, name));
        return y;
    };
    run ("render, untouched", 1, 1, false);
    run ("snap 100 %, keep drift + vibrato", 1, 1, true);
    run ("snap, KEEP DRIFT 0", 0, 1, true);
    run ("snap, KEEP VIBRATO 0", 1, 0, true);
    const auto flat = run ("snap, KEEP DRIFT 0 + KEEP VIBRATO 0", 0, 0, true, argc > 2);
    run ("no snap, DRIFT 0 + VIBRATO 0", 0, 0, false);

    // Slow wow only (2 - 3.5 Hz): drift or vibrato?
    {
        const auto W = wobblyLine (seed + 100, 30.0, 80.0, 10.0, 2.0, 3.5);
        const auto tw = honey::analyse (W.audio, sr, true);
        auto nw = honey::findNotes (tw);
        std::printf ("\nSlow wow (2 - 3.5 Hz, 30 - 80 c): findNotes %zu notes for %zu sung\n", nw.size(), W.notes.size());
        line ("input", measure (W.audio, W));
        honey::snapToKey (nw, 0, 1, 1.0);
        std::vector<double> kn;
        for (const auto& n : W.notes) kn.push_back (PitchCorrector::targetNote (n.midi, 0, 1, -1));
        for (auto [name, d, v] : { std::tuple { "snap, KEEP DRIFT 0", 0.0, 1.0 }, { "snap, KEEP VIBRATO 0", 1.0, 0.0 }, { "snap, DRIFT 0 + VIBRATO 0", 0.0, 0.0 } })
        {
            auto e = nw;
            for (auto& n : e) { n.drift = d; n.vibrato = v; }
            line (name, measure (honey::render (W.audio, sr, tw, e), W, kn));
        }
    }

    // Item 3: steady notes moved +2 / -3 semitones (drift / vibrato 0): any warble or flutter added?
    {
        const auto S = wobblyLine (seed + 200, 0, 0, 0, 2, 7, true);
        const auto ts = honey::analyse (S.audio, sr, true);
        auto ns = honey::findNotes (ts);
        std::printf ("\nSteady line: findNotes %zu notes for %zu sung\n", ns.size(), S.notes.size());
        line ("input (steady)", measure (S.audio, S));
        for (double move : { 0.0, 2.0, -3.0, 0.3 })
        {
            auto e = ns;
            for (auto& n : e) { n.target = n.pitch + move; n.drift = 0; n.vibrato = 0; }
            std::vector<double> want;
            for (const auto& n : S.notes) want.push_back (n.midi + move);
            char name[80]; std::snprintf (name, sizeof name, "moved %+.1f st, drift / vibrato 0", move);
            line (name, measure (honey::render (S.audio, sr, ts, e), S, want));
        }
    }

    // Stereo: left / right with different content (a stereo room), each channel rendered alone (as the plug-in does).
    {
        std::vector<float> l = x, r = x;
        Rng rng (seed + 300);
        // A short stereo "room": different early echoes per side.
        std::vector<float> lw (x.size(), 0.0f), rw (x.size(), 0.0f);
        for (int k = 0; k < 6; ++k)
        {
            const auto dl = static_cast<size_t> (rng.range (0.004, 0.03) * sr), dr = static_cast<size_t> (rng.range (0.004, 0.03) * sr);
            const double gl = rng.range (0.15, 0.35), gr = rng.range (0.15, 0.35);
            for (size_t i = 0; i < x.size(); ++i)
            {
                if (i >= dl) lw[i] += static_cast<float> (gl * x[i - dl]);
                if (i >= dr) rw[i] += static_cast<float> (gr * x[i - dr]);
            }
        }
        for (size_t i = 0; i < x.size(); ++i) { l[i] += lw[i]; r[i] += rw[i]; }
        std::vector<float> mono (x.size());
        for (size_t i = 0; i < x.size(); ++i) mono[i] = 0.5f * (l[i] + r[i]);
        const auto tr = honey::analyse (mono, sr, true);
        auto ne = honey::findNotes (tr);
        honey::snapToKey (ne, 0, 1, 1.0);
        for (auto& n : ne) { n.drift = 0; n.vibrato = 0; }
        const auto yl = honey::render (l, sr, tr, ne), yr = honey::render (r, sr, tr, ne);
        // How far apart do the two sides' pitch lines come out, per note? (Same plan: they should match.)
        double diff = 0; int cnt = 0;
        for (const auto& n : L.notes)
        {
            const auto a = trackNote (yl, n.start + 0.08 * sr, n.end - 0.08 * sr, PitchCorrector::targetNote (n.midi, 0, 1, -1));
            const auto b = trackNote (yr, n.start + 0.08 * sr, n.end - 0.08 * sr, PitchCorrector::targetNote (n.midi, 0, 1, -1));
            for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) { diff += 100 * std::abs (a[i] - b[i]); ++cnt; }
        }
        // And the side signal: (L - R) of the render vs of the input, in the notes.
        double sIn = 0, sOut = 0, mIn = 0, mOut = 0;
        for (const auto& n : L.notes)
            for (auto i = static_cast<size_t> (n.start); i < static_cast<size_t> (n.end); ++i)
            {
                sIn += std::pow (l[i] - r[i], 2); mIn += std::pow (l[i] + r[i], 2);
                sOut += std::pow (yl[i] - yr[i], 2); mOut += std::pow (yl[i] + yr[i], 2);
            }
        std::printf ("\nStereo clip, sides rendered alone: L vs R pitch %.2f c apart (mean), side / mid %.1f dB in -> %.1f dB out\n",
                     diff / std::max (1, cnt), 10 * std::log10 (sIn / mIn), 10 * std::log10 (sOut / mOut));
        line ("left, snap + DRIFT 0 + VIBRATO 0", measure (yl, L, keyNotes));
    }

    // The user's case: Voxology's Pitch on (Folk: Natural, Retune 50 ms, Humanize 60, Amount 70) in F minor from the beat,
    // Honey Tune on Auto that didn't get that key (Chromatic: every note to the nearest semitone), notes moved only.
    // A line in F minor with three notes outside it (A, E, D: each sits halfway between two notes of F minor).
    {
        static constexpr double fLine[] = { 53.2, 55.0, 57.1, 60.0, 58.3, 52.1, 62.2, 53.0 };   // F G A C Bb E D F
        const auto F = wobblyLine (seed + 400, 20.0, 60.0, 20.0, 2.0, 7.0, false, fLine);
        const auto tf = honey::analyse (F.audio, sr, true);
        const auto nf = honey::findNotes (tf);
        std::printf ("\nThe user's case (F minor line with A, E, D in it; %zu notes found for %zu sung):\n", nf.size(), F.notes.size());
        auto both = [&] (const char* name, int hkey, int hscale, double keep)
        {
            auto e = nf;
            honey::snapToKey (e, hkey, hscale, 1.0);
            for (auto& n : e) { n.drift = keep; n.vibrato = keep; }
            std::vector<double> want;   // where Honey Tune puts each sung note
            for (const auto& n : F.notes) want.push_back (PitchCorrector::targetNote (n.midi, hkey, hscale, -1));
            const auto h = honey::render (F.audio, sr, tf, e);
            char label[120];
            std::snprintf (label, sizeof label, "Honey Tune %s", name);
            line (label, measure (h, F, want));
            std::snprintf (label, sizeof label, "  + Voxology Folk (Natural 50 ms, 70 %%) F minor");
            const auto v = livePitch (h, kPitchNatural, 50, 60, 70, 5, 2);
            line (label, measure (v, F, want, false));
            int moved = 0;
            for (size_t k = 0; k < F.notes.size(); ++k)
            {
                const auto& n = F.notes[k];
                const auto w = wobbleOf (trackNote (v, n.start + 0.08 * sr, n.end - 0.08 * sr, want[k]));
                if (std::abs (w.centre - want[k]) > 0.15) { ++moved; std::printf ("      note %zu: Honey Tune %s -> Voxology pulls it to %.2f (%+.0f c)\n", k, std::to_string ((int) want[k]).c_str(), w.centre, 100 * (w.centre - want[k])); }
            }
            std::printf ("      %d of %zu notes pulled away from where Honey Tune put them\n", moved, F.notes.size());
        };
        both ("Auto = Chromatic, keep movement (only notes moved)", 0, 0, 1.0);
        both ("F minor, keep movement", 5, 2, 1.0);
        both ("F minor, DRIFT 0 + VIBRATO 0", 5, 2, 0.0);
        both ("Chromatic, DRIFT 0 + VIBRATO 0", 0, 0, 0.0);
    }

    // Item 4: Voxology's live Pitch after Honey Tune (the flattened render), Auto-Edit's settings per style.
    {
        std::printf ("\nVoxology's Pitch after Honey Tune (Honey Tune: snap C major, DRIFT 0 + VIBRATO 0):\n");
        line ("Honey Tune only", measure (flat, L, keyNotes));
        struct Cfg { const char* name; int mode; double speed, hum, amount; int key, scale; };
        const Cfg cfgs[] = { { "+ Pitch Natural 50 ms 70 % (Folk), C major", kPitchNatural, 50, 60, 70, 0, 1 },
                             { "+ Pitch Natural 25 ms (R&B), C major", kPitchNatural, 25, 50, 100, 0, 1 },
                             { "+ Pitch Natural 15 ms (Pop), C major", kPitchNatural, 15, 40, 100, 0, 1 },
                             { "+ Pitch Classic 5 ms (Melodic), C major", kPitchClassic, 5, 10, 100, 0, 1 },
                             { "+ Pitch Classic 10 ms (Trap Lead), Chromatic", kPitchClassic, 10, 20, 100, 0, 0 },
                             { "+ Pitch Natural 25 ms, wrong key (C# major)", kPitchNatural, 25, 50, 100, 1, 1 },
                             { "+ Pitch Classic 5 ms, wrong key (C# major)", kPitchClassic, 5, 10, 100, 1, 1 } };
        for (const auto& c : cfgs)
            line (c.name, measure (livePitch (flat, c.mode, c.speed, c.hum, c.amount, c.key, c.scale), L, keyNotes));
        std::printf ("Voxology's Pitch alone on the wobbly input:\n");
        for (const auto& c : cfgs)
            line (c.name, measure (livePitch (x, c.mode, c.speed, c.hum, c.amount, c.key, c.scale), L, c.key == 0 && c.scale == 1 ? keyNotes : std::vector<double> {}));
    }
    return 0;
}
