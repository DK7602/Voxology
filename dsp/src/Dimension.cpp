#include "vox/Dimension.h"

#include "vox/FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vox {

namespace {
constexpr double kChorusMs = 12.0, kChorusDepthMs = 1.5;                       // +-1.5 ms at 0.5 Hz: about 8 cents of wobble
constexpr double kFlangerMs = 2.5, kFlangerDepthMs = 2.0, kFlangerFeedback = 0.5;
constexpr double kPhaserLoHz = 300.0, kPhaserHiHz = 3000.0, kPhaserFeedback = 0.4;
constexpr double kChorusMix = 0.7;
// Feedback rings (peaks up to 1 / (1 - fb)); on average it adds 1 / (1 - fb^2) of power: the copy is scaled back by that.
const double kFlangerNorm = std::sqrt (1.0 - kFlangerFeedback * kFlangerFeedback);
const double kPhaserNorm = std::sqrt (1.0 - kPhaserFeedback * kPhaserFeedback);   // the copy's level at 100 % (flanger / phaser: the classic equal mix)
}

void Dimension::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& l : line) l.allocate (static_cast<int> (std::ceil ((kChorusMs + kChorusDepthMs) / 1000.0 * sr)) + 4);
    mixCoeff = design::onePole (0.030, sr);
    reset();
}

void Dimension::reset() noexcept
{
    for (auto& l : line) l.clear();
    for (auto& a : ap) a.fill (0.0);
    fb.fill (0.0);
    phase = 0.0;
    mix = 0.0;
    mode = std::clamp (params.mode, 0, kDimensionModes - 1);
    running = false;
}

double Dimension::wetSample (int c, double x, double lfo) noexcept
{
    const auto k = static_cast<size_t> (c);
    auto& l = line[k];
    if (mode == 0)   // chorus
    {
        l.push (x);
        return l.read ((kChorusMs + kChorusDepthMs * lfo) / 1000.0 * sr);
    }
    if (mode == 1)   // flanger
    {
        l.push (x + kFlangerFeedback * fb[k]);
        const double y = l.read (std::max (1.0, (kFlangerMs + kFlangerDepthMs * lfo) / 1000.0 * sr));
        fb[k] = y;
        return y * kFlangerNorm;
    }
    // phaser: first-order all-passes (transposed direct form II), all at the swept frequency
    const double f = kPhaserLoHz * std::pow (kPhaserHiHz / kPhaserLoHz, 0.5 * (lfo + 1.0));
    const double t = std::tan (std::numbers::pi * f / sr);
    const double a = (t - 1.0) / (t + 1.0);
    double s = x + kPhaserFeedback * fb[k];
    for (auto& z : ap[k])
    {
        const double y = a * s + z;
        z = s - a * y;
        s = y;
    }
    fb[k] = s;
    return s * kPhaserNorm;
}

void Dimension::process (double* const* ch, int nch, int n) noexcept
{
    const bool off = isNeutral (params);
    if (off && ! running)
        return;
    running = true;
    nch = std::clamp (nch, 1, 2);
    const int want = std::clamp (params.mode, 0, kDimensionModes - 1);
    const double amount = std::clamp (params.amount, 0.0, 100.0) / 100.0;
    const double step = std::clamp (params.rateHz, kMinRateHz, kMaxRateHz) / sr;
    const double spread = 0.5 * std::clamp (params.width, 0.0, 100.0) / 100.0;   // right's sweep up to half a cycle behind
    for (int i = 0; i < n; ++i)
    {
        // A mode change or switching off: fade out first (then switch / stop), so it never clicks.
        const double target = off || want != mode ? 0.0 : amount * (mode == 0 ? kChorusMix : 1.0);
        mix += (target - mix) * mixCoeff;
        if (target == 0.0 && mix < 1.0e-4)
        {
            if (off) { reset(); return; }   // (the rest of the block untouched)
            mode = want;
            for (auto& l : line) l.clear();
            for (auto& a : ap) a.fill (0.0);
            fb.fill (0.0);
            mix = 0.0;
        }
        phase += step;
        if (phase >= 1.0) phase -= 1.0;
        const double norm = 1.0 / std::sqrt (1.0 + 0.6 * mix * mix);   // the same level at any Amount (measured on a voice:
                                                                        // the copy is partly in step with it)
        for (int c = 0; c < nch; ++c)
        {
            const double lfo = std::sin (2.0 * std::numbers::pi * (phase + (c == 1 ? spread : 0.0)));
            double& y = ch[c][i];
            y = (y + mix * wetSample (c, y, lfo)) * norm;
        }
    }
}

} // namespace vox
