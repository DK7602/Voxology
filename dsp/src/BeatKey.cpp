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
    tuneRe = tuneIm = 0.0;
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
    std::array<double, 12> frame {};
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
        frame[static_cast<size_t> (((static_cast<int> (note) % 12) + 12) % 12)] += w;
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
    for (size_t i = 0; i < 12; ++i) chroma[i] = chroma[i] * fade + frame[i] / total;
    tuneRe = tuneRe * fade + frameRe / total;
    tuneIm = tuneIm * fade + frameIm / total;
    res.heardSeconds += 0.25;
    if (res.heardSeconds >= kMinSeconds)
    {
        res.ready = true;
        res.key = keyFromHistogram (chroma);
        res.tuneCents = 100.0 * std::atan2 (tuneIm, tuneRe) / (2.0 * std::numbers::pi);
    }
}

} // namespace vox
