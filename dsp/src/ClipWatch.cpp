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

void ClipWatch::flatTop (Channel& s, double level) noexcept
{
    // A clipper's ceiling is the same every time: a flat top counts once another one has sat at the same level
    // (then both count). A flat top at a level of its own is most likely chance.
    const double near = std::max (1.0e-6 * level, 1.5 * s.step);
    for (auto& t : s.tops)
        if (t.level > 0.0 && std::abs (t.level - level) <= near)
        {
            counts.runs += t.counted ? 1 : 2;
            t.counted = true;
            return;
        }
    s.tops[s.nextTop] = { level, false };
    s.nextTop = (s.nextTop + 1) % s.tops.size();
}

void ClipWatch::process (const double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, static_cast<int> (chans.size()));
    const double peakFall = 0.99999;   // ~ -1 dB / 2.4 s at 48 kHz
    const double stepRise = 1.00002;   // the finest step may grow back ~2.6x a second at 48 kHz (another clip, more bits)
    for (int c = 0; c < nch; ++c)
    {
        auto& s = chans[static_cast<size_t> (c)];
        for (int i = 0; i < n; ++i)
        {
            const double v = ch[c][i], a = std::abs (v), d = std::abs (v - s.prev);
            s.peak = std::max (a, s.peak * peakFall);
            if (a > 1.0) ++counts.overs;
            // The finest step the audio moves in: one step of its bit depth (16-bit audio moves in steps of 1/32768;
            // float audio in steps far too small to matter).
            s.step = d > 0.0 ? std::min (s.step * stepRise, d) : s.step * stepRise;
            if (a > 0.01 && d <= 1.0e-6 * a)
            {
                if (s.run == 0) { s.stepIn = s.lastStep; s.before = s.beforePrev; s.atPeak = false; }
                ++s.run;
                s.atPeak = s.atPeak || a >= kAtPeak * s.peak;
            }
            else
            {
                // A flat stretch just ended (run counts equal pairs: 2 pairs = 3 samples). It's a clipped top when it sat
                // at the recent peak, both its neighbours are lower (nothing gets past a ceiling) and it was entered and
                // left with a jump: a smooth peak in 16-bit audio can sit on one value for a few samples too, but its
                // neighbours are then only a step or two away, while a clipped top cuts off a waveform still moving.
                const double level = std::abs (s.prev), sign = s.prev < 0.0 ? -1.0 : 1.0;
                if (s.run >= kMinRun - 1 && s.atPeak && sign * s.before < level && sign * v < level
                    && std::min (s.stepIn, d) > kJumpSteps * s.step)
                    flatTop (s, level);
                s.run = 0;
            }
            s.lastStep = d;
            s.beforePrev = s.prev;
            s.prev = v;
        }
    }
}

} // namespace vox
