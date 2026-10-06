#include "vox/ClipWatch.h"

#include <algorithm>
#include <cmath>

namespace vox {

void ClipWatch::prepare (int numChannels)
{
    chans.assign (static_cast<size_t> (std::max (1, numChannels)), {});
    reset();
}

void ClipWatch::reset() noexcept
{
    for (auto& c : chans) c = {};
    counts = {};
}

void ClipWatch::process (const double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, static_cast<int> (chans.size()));
    const double peakFall = 0.99999;   // ~ -1 dB / 2.4 s at 48 kHz
    for (int c = 0; c < nch; ++c)
    {
        auto& s = chans[static_cast<size_t> (c)];
        for (int i = 0; i < n; ++i)
        {
            const double v = ch[c][i], a = std::abs (v);
            s.peak = std::max (a, s.peak * peakFall);
            if (a > 1.0) ++counts.overs;
            const bool flat = a > 0.01 && a > 0.25 * s.peak && std::abs (v - s.prev) <= 1.0e-6 * a;
            if (flat) { if (++s.run == kMinRun - 1) ++counts.runs; }   // run counts equal pairs: 2 pairs = 3 samples
            else s.run = 0;
            s.prev = v;
        }
    }
}

} // namespace vox
