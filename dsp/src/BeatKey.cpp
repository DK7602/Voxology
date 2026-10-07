#include "vox/BeatKey.h"

#include "vox/FilterDesign.h"
#include "vox/Fft.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vox {

void BeatKey::prepare (double sampleRate)
{
    sr = sampleRate;
    dec = std::max (1, static_cast<int> (std::lround (sr / kRate)));
    fs = sr / dec;
    const auto lp = design::butterworth (false, 0.4 * fs, sr);
    design::apply (aa1, lp);
    design::apply (aa2, lp);
    ring.assign (static_cast<size_t> (kFft), 0.0);
    buf.assign (static_cast<size_t> (kFft), {});
    window.resize (static_cast<size_t> (kFft));
    for (size_t i = 0; i < window.size(); ++i)
        window[i] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / kFft);
    mag.assign (static_cast<size_t> (kFft / 2 + 1), 0.0);
    hop = static_cast<int> (std::lround (0.25 * fs));
    fade = std::exp (-0.25 / 40.0);
    reset();
}

void BeatKey::reset() noexcept
{
    aa1.reset(); aa2.reset();
    std::fill (ring.begin(), ring.end(), 0.0);
    pos = 0; sinceLast = 0; decCount = 0; decAcc = 0.0;
    chroma.fill (0.0);
    bass.fill (0.0);
    tuneRe = tuneIm = 0.0;
    currentSet = -1;
    res = {};
}

void BeatKey::process (const double* mono, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const double v = aa2.process (aa1.process (mono[i]));
        if (++decCount < dec) continue;
        decCount = 0;
        ring[static_cast<size_t> (pos)] = v;
        pos = (pos + 1) % kFft;
        if (++sinceLast >= hop) { sinceLast = 0; analyse(); }
    }
}

void BeatKey::analyse() noexcept
{
    double energy = 0.0;
    for (size_t i = 0; i < buf.size(); ++i)
    {
        const double v = ring[(static_cast<size_t> (pos) + i) % ring.size()];
        energy += v * v;
        buf[i] = { v * window[i], 0.0 };
    }
    // Silence (stopped, a gap): nothing to learn, nothing forgotten.
    if (energy / kFft < 1.0e-8) return;
    fft (buf);
    for (size_t k = 0; k < mag.size(); ++k) mag[k] = std::abs (buf[k]);

    const double binHz = fs / kFft;
    const auto lo = static_cast<size_t> (std::ceil (50.0 / binHz)), hi = std::min (mag.size() - 2, static_cast<size_t> (2000.0 / binHz));
    std::array<double, 12> frame {}, bassFrame {};
    double frameRe = 0.0, frameIm = 0.0, total = 0.0;
    const auto reach = static_cast<size_t> (std::ceil (40.0 / binHz));   // neighbourhood: +-40 Hz
    for (size_t k = lo; k <= hi; ++k)
    {
        const double m = mag[k];
        if (m <= mag[k - 1] || m < mag[k + 1]) continue;   // a peak
        // It must stand out: 4x (12 dB) over the neighbourhood's average.
        double sum = 0.0; size_t cnt = 0;
        for (size_t j = (k > reach ? k - reach : 0); j <= std::min (mag.size() - 1, k + reach); ++j) { sum += mag[j]; ++cnt; }
        if (m < 4.0 * sum / static_cast<double> (cnt)) continue;
        // Exact frequency (parabola through the log magnitudes), then note and cents.
        const double a = std::log (mag[k - 1] + 1.0e-30), b = std::log (m), c = std::log (mag[k + 1] + 1.0e-30);
        const double den = a - 2.0 * b + c;
        const double kk = static_cast<double> (k) + (std::abs (den) > 1.0e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0);
        const double midi = 69.0 + 12.0 * std::log2 (kk * binHz / 440.0);
        const double note = std::round (midi);
        const double w = std::sqrt (m);   // level, compressed: loud 808s don't drown the chords
        const auto pc = static_cast<size_t> (((static_cast<int> (note) % 12) + 12) % 12);
        frame[pc] += w;
        total += w;
        // Tuning: only peaks with enough resolution (a bin is < 1/4 semitone wide above ~100 Hz).
        if (kk * binHz > 100.0)
        {
            const double ang = 2.0 * std::numbers::pi * (midi - note);
            frameRe += w * std::cos (ang);
            frameIm += w * std::sin (ang);
        }
    }
    if (total <= 0.0) return;
    // The bass note (808, bass line): the LOWEST peak that stands out between 28 and 160 Hz, so its
    // overtones (an 808 on E1 rings loudly on B2) don't count. It usually sits on the home note. It
    // counts as much as it's loud next to the loudest note in the frame: a real 808 / bass fully, an
    // acoustic guitar's low strings (no bass player) hardly.
    double loudest = 0.0;
    for (size_t k = lo; k <= hi; ++k) loudest = std::max (loudest, mag[k]);
    for (size_t k = std::max<size_t> (2, static_cast<size_t> (std::ceil (28.0 / binHz))); k <= static_cast<size_t> (160.0 / binHz); ++k)
    {
        const double m = mag[k];
        if (m <= mag[k - 1] || m < mag[k + 1]) continue;
        double sum = 0.0; size_t cnt = 0;
        for (size_t j = (k > reach ? k - reach : 1); j <= k + reach; ++j) { sum += mag[j]; ++cnt; }
        if (m < 4.0 * sum / static_cast<double> (cnt)) continue;
        const double midi = 69.0 + 12.0 * std::log2 (static_cast<double> (k) * binHz / 440.0);
        bassFrame[static_cast<size_t> (((static_cast<int> (std::lround (midi)) % 12) + 12) % 12)] += std::min (1.0, m / (loudest + 1.0e-30));
        break;
    }
    for (size_t i = 0; i < 12; ++i) chroma[i] = chroma[i] * fade + frame[i] / total;
    for (size_t i = 0; i < 12; ++i) bass[i] = bass[i] * fade + bassFrame[i];
    tuneRe = tuneRe * fade + frameRe / total;
    tuneIm = tuneIm * fade + frameIm / total;
    res.heardSeconds += 0.25;
    if (res.heardSeconds < kMinSeconds) return;

    // Free to change its mind while it's still learning (first 15 s), firm after that.
    // Random / drum-only material spreads over all 12 (a 7-note set holds ~58 %); real music > 85 %.
    pickNoteSet (chroma, res.heardSeconds < 15.0 ? 0.0 : 0.03, 0.70, 0.85, currentSet, res, &bass);
    res.tuneCents = 100.0 * std::atan2 (tuneIm, tuneRe) / (2.0 * std::numbers::pi);
}

int pickNoteSet (const std::array<double, 12>& chroma, double margin, double confLo, double confHi, int& currentSet, BeatKey::Result& res,
                 const std::array<double, 12>* bass) noexcept
{
    static constexpr std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    double sum = 0.0;
    for (double c : chroma) sum += c;
    if (sum <= 0.0) return 0;
    std::array<double, 12> held {};
    for (int r = 0; r < 12; ++r)
        for (int step : major) held[static_cast<size_t> (r)] += chroma[static_cast<size_t> ((r + step) % 12)];
    int best = 0, second = -1;
    for (int r = 1; r < 12; ++r) if (held[static_cast<size_t> (r)] > held[static_cast<size_t> (best)]) best = r;
    for (int r = 0; r < 12; ++r) if (r != best && (second < 0 || held[static_cast<size_t> (r)] > held[static_cast<size_t> (second)])) second = r;
    if (currentSet < 0 || held[static_cast<size_t> (best)] > held[static_cast<size_t> (currentSet)] + margin * sum) currentSet = best;
    const int set = currentSet;
    const double share = held[static_cast<size_t> (set)] / sum;

    // Home: the set's note leaned on most (plus a quarter of its fifth, which backs a tonic up), and
    // above all the note the bass sits on (the 808 plays the home note far more than any other).
    // (bass: on the chroma's scale, each analysis adds up to 1 to it and exactly 1 to the chroma).
    double bestHome = -1.0; int home = 0;
    for (int step : major)
    {
        const int n = (set + step) % 12;
        double w = chroma[static_cast<size_t> (n)] + 0.25 * chroma[static_cast<size_t> ((n + 7) % 12)];
        if (bass != nullptr) w += (*bass)[static_cast<size_t> (n)];
        if (w > bestHome) { bestHome = w; home = step; }
    }
    res.ready = true;
    res.setRoot = set;
    res.tonicOffset = home;
    res.confidence = std::clamp ((share - confLo) / (confHi - confLo), 0.0, 1.0);
    const int other = set == best ? second : best;
    res.unclear = (held[static_cast<size_t> (set)] - held[static_cast<size_t> (other)]) < 0.03 * sum;
    // The runner-up's note that ours doesn't have (neighbouring sets differ by one note).
    res.openNote = -1;
    if (res.unclear)
    {
        auto in = [] (int root, int n) { for (int step : major) if ((root + step) % 12 == n) return true; return false; };
        int count = 0, found = -1;
        for (int n = 0; n < 12; ++n) if (in (other, n) && ! in (set, n)) { ++count; found = n; }
        if (count == 1) res.openNote = found;
        // The two sets differ by one barely played note, so either names the key. Prefer the one where
        // home is plain minor or major (E minor rather than E dorian): the same notes are allowed.
        const int homeNote = (set + home) % 12;
        const int otherOffset = (homeNote - other + 12) % 12;
        const bool otherHasHome = in (other, homeNote);
        if (res.openNote >= 0 && otherHasHome && home != 0 && home != 9 && (otherOffset == 0 || otherOffset == 9))
        {
            int mine = -1;
            for (int n = 0; n < 12; ++n) if (in (set, n) && ! in (other, n)) mine = n;
            res.setRoot = other;
            res.tonicOffset = otherOffset;
            res.openNote = mine;
        }
    }
    int ties = 0;
    for (int r = 0; r < 12; ++r) if (r != set && held[static_cast<size_t> (r)] >= held[static_cast<size_t> (set)] - 0.03 * sum) ++ties;
    return ties;
}

// =================================================================================================
void VoiceKey::reset() noexcept
{
    fine.fill (0.0);
    chroma.fill (0.0);
    smooth = 0.0; gap = 1.0; sinceAnalyse = 0.0;
    currentSet = -1;
    wasSure = false;
    res = {};
}

void VoiceKey::add (bool voiced, double sungMidi, double clarity, double seconds) noexcept
{
    if (seconds <= 0.0) return;
    const bool clear = voiced && clarity >= kMinClarity && sungMidi > 20.0;
    if (! clear) { gap += seconds; return; }
    // Steady: near the pitch's recent average (~60 ms), so vibrato around a note counts, glides don't.
    if (gap > 0.05) smooth = sungMidi;   // a new phrase starts here
    gap = 0.0;
    smooth += (sungMidi - smooth) * (1.0 - std::exp (-seconds / 0.06));
    if (std::abs (sungMidi - smooth) > 0.4) return;
    // Memory: about a minute of held notes (fades only while notes are heard: pauses forget nothing).
    const double fade = std::exp (-seconds / 60.0);
    for (double& c : fine) c *= fade;
    const auto bin = static_cast<size_t> (((static_cast<long> (std::lround (smooth * 10.0)) % 120) + 120) % 120);
    fine[bin] += seconds;
    res.heardSeconds += seconds;
    sinceAnalyse += seconds;
    if (res.heardSeconds < kMinSeconds || sinceAnalyse < 0.25) return;
    sinceAnalyse = 0.0;
    analyse();
}

void VoiceKey::analyse() noexcept
{
    // The singer's overall offset from the notes (weighted circular mean), then fold into 12 notes.
    double re = 0.0, im = 0.0;
    for (size_t b = 0; b < fine.size(); ++b)
    {
        const double ang = 2.0 * std::numbers::pi * static_cast<double> (b % 10) / 10.0;
        re += fine[b] * std::cos (ang);
        im += fine[b] * std::sin (ang);
    }
    const double offset = std::atan2 (im, re) / (2.0 * std::numbers::pi);   // semitones, -0.5 .. 0.5
    chroma.fill (0.0);
    for (size_t b = 0; b < fine.size(); ++b)
    {
        const double note = static_cast<double> (b) / 10.0 - offset;
        chroma[static_cast<size_t> (((static_cast<long> (std::lround (note)) % 12) + 12) % 12)] += fine[b];
    }
    // Sung melodies stay in the key less strictly than chords (passing notes, scoops landing late):
    // a 7-note set holding 68 % is a guess, 88 % is sure (tested on the user's four acapellas: a set
    // holding ~72 % was one note off the beat's).
    const int ties = pickNoteSet (chroma, res.heardSeconds < 20.0 ? 0.0 : 0.04, 0.68, 0.88, currentSet, res);
    double sum = 0.0;
    for (double c : chroma) sum += c;
    int notes = 0;
    for (double c : chroma) if (c >= 0.03 * sum) ++notes;
    // Too few different notes sung so far (one note held, or three keys hold them all): not sure yet.
    // Once it has been sure, a part that leans on fewer notes doesn't undo that (no flip-flopping).
    if (! wasSure && (ties >= 2 || notes < 5)) res.confidence = std::min (res.confidence, 0.25);
    wasSure = wasSure || res.confidence >= 0.5;
}

} // namespace vox
