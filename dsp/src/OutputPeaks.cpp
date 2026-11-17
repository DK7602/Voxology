#include "vox/OutputPeaks.h"

#include "vox/SoftClipper.h"

#include <algorithm>
#include <cmath>

namespace vox {

void OutputPeaks::prepare (double sampleRate) noexcept
{
    release = 1.0 - std::exp (-1.0 / (kReleaseS * std::max (8000.0, sampleRate)));
    reset();
}

void OutputPeaks::reset() noexcept
{
    env = 1.0;
    deepest = 1.0;
}

void OutputPeaks::setParams (PeakMode m, double ceilingDb) noexcept
{
    if (m != PeakMode::limit) env = 1.0;   // back to Limit later: it starts from no turn-down
    mode = m;
    const double db = std::isfinite (ceilingDb) ? std::clamp (ceilingDb, kMinCeilingDb, kMaxCeilingDb) : -1.0;
    ceiling = std::pow (10.0, db / 20.0);
}

void OutputPeaks::process (double* frame, int nch) noexcept
{
    if (mode == PeakMode::off) return;
    if (mode == PeakMode::clip)
    {
        for (int c = 0; c < nch; ++c)
        {
            const double x = frame[c];
            const double y = SoftClipper::shape (x, ceiling, kClipSoftness);
            if (std::abs (x) > 1.0e-12) deepest = std::min (deepest, std::abs (y / x));
            frame[c] = y;
        }
        return;
    }
    // Limit: the gain that keeps this frame's loudest sample at the Ceiling, taken at once; back up slowly.
    double peak = 0.0;
    for (int c = 0; c < nch; ++c) peak = std::max (peak, std::abs (frame[c]));
    const double want = peak > ceiling ? ceiling / peak : 1.0;
    env = want < env ? want : env + (want - env) * release;
    if (env > 1.0 - 1.0e-9) env = 1.0;
    deepest = std::min (deepest, env);
    if (env < 1.0)
        for (int c = 0; c < nch; ++c) frame[c] *= env;
}

double OutputPeaks::takeReductionDb() noexcept
{
    const double d = deepest;
    deepest = 1.0;
    return d < 1.0 ? 20.0 * std::log10 (std::max (d, 1.0e-6)) : 0.0;
}

} // namespace vox
