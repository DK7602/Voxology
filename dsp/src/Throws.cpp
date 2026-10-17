#include "vox/Throws.h"

#include "vox/FilterDesign.h"

#include <algorithm>
#include <cmath>

namespace vox {

namespace {
constexpr int kChunk = 256;   // VocalChain::kChunk: the chain never sends more at once
}

void Throws::prepare (double sampleRate)
{
    sr = sampleRate;
    blockLen = std::max (1, static_cast<int> (std::lround (0.010 * sr)));
    hist.assign (static_cast<size_t> (2.5 * sr), 0.0);
    const auto lineSize = static_cast<size_t> (EchoDelay::kMaxSeconds * sr) + 8;
    lineL.assign (lineSize, 0.0);
    lineR.assign (lineSize, 0.0);
    levels.assign (256, -120.0);
    design::apply (hp, design::butterworth (true, 250.0, sr));
    const auto lp = design::butterworth (false, std::min (kToneHz, 0.45 * sr), sr);
    design::apply (lpL, lp);
    design::apply (lpR, lp);
    wash.prepare (sr);
    wash.setParams ({ true, kWashSeconds, 0.0, 100.0, 6000.0, 0.0 });
    washIn.assign (kChunk, 0.0);
    glide = design::onePole (0.050, sr);
    reset();
}

void Throws::reset() noexcept
{
    std::fill (hist.begin(), hist.end(), 0.0);
    std::fill (lineL.begin(), lineL.end(), 0.0);
    std::fill (lineR.begin(), lineR.end(), 0.0);
    std::fill (levels.begin(), levels.end(), -120.0);
    lpL.reset();
    lpR.reset();
    wash.reset();
    now = 0;
    pos = 0;
    blocks = 0;
    blockE = 0.0;
    blockFill = 0;
    inLine = false;
    lineStart = lastLoud = 0;
    linePeak = -120.0;
    quiet = 0;
    echoGain = washGain = 0.0;
    duck = 1.0;
    offFor = 0;
    running = false;
}

void Throws::startClockAt (int64_t sample) noexcept
{
    reset();
    blocks = std::max<int64_t> (0, sample) / blockLen;
    now = blocks * blockLen;
    lineStart = lastLoud = blocks;
}

void Throws::block (double levelDb) noexcept
{
    const auto size = static_cast<int64_t> (levels.size());
    levels[static_cast<size_t> (blocks % size)] = levelDb;
    const int hold = static_cast<int> (std::lround (kHoldSeconds / 0.010));
    if (! inLine)
    {
        if (levelDb > -50.0)
        {
            inLine = true;
            lineStart = lastLoud = blocks;
            linePeak = levelDb;
            quiet = 0;
        }
    }
    else
    {
        linePeak = std::max (linePeak, levelDb);
        if (levelDb < linePeak - kQuietDb || levelDb < -60.0)
        {
            if (++quiet >= hold)
            {
                // The line has ended: was it a line (long enough)? Then throw its last word.
                if (static_cast<double> (lastLoud - lineStart + 1) * 0.010 >= kMinLineSeconds)
                {
                    // The last word: back from the end to the last dip (10 dB under what follows it), at most
                    // kMaxWordSeconds (and not past what the level ring still holds).
                    const int64_t limit = std::max ({ lineStart, lastLoud - static_cast<int64_t> (kMaxWordSeconds / 0.010), blocks - size + 2 });
                    int64_t start = limit;
                    double after = -120.0;
                    for (int64_t b = lastLoud; b > limit; --b)
                    {
                        after = std::max (after, levels[static_cast<size_t> (b % size)]);
                        if (levels[static_cast<size_t> ((b - 1) % size)] < after - 10.0) { start = b; break; }
                    }
                    throwWord (start * blockLen, (lastLoud + 1) * blockLen + static_cast<int64_t> (0.060 * sr));
                }
                inLine = false;
            }
        }
        else
        {
            quiet = 0;
            lastLoud = blocks;
        }
    }
    ++blocks;
}

void Throws::throwWord (int64_t from, int64_t to) noexcept
{
    const auto histSize = static_cast<int64_t> (hist.size()), size = static_cast<int64_t> (lineL.size());
    from = std::max (from, now - histSize + 1);
    to = std::min (to, now);
    if (to - from < blockLen) return;
    const auto d = std::max<int64_t> (1, std::llround (delay));
    const double fadeIn = 0.010 * sr, fadeOut = 0.030 * sr;
    hp.reset();
    for (int64_t s = from; s < to; ++s)
    {
        const double fade = std::min ({ 1.0, static_cast<double> (s - from) / fadeIn, static_cast<double> (to - s) / fadeOut });
        const double x = hp.process (hist[static_cast<size_t> (s % histSize)]) * fade;
        // Its echo was due d after it was sung; if that's gone by, the next repeat (at its level).
        const int64_t age = now - s, k = age / d + 1;
        const int64_t back = age - (k - 1) * d;          // 0 .. d - 1: the slot written `back` samples ago is read in d - back
        lineL[static_cast<size_t> (((pos - back) % size + size) % size)] += x * std::pow (kFeedback, static_cast<double> (k - 1));
    }
    ++throws;
}

void Throws::process (const double* mono, double* const* out, int nch, int n) noexcept
{
    const bool off = isNeutral (params);
    if (off && ! running)
        return;
    running = true;
    DelayParams dp;
    dp.division = params.division;
    dp.bpm = params.bpm;
    delay = std::clamp (EchoDelay::delaySeconds (dp) * sr, 1.0, static_cast<double> (lineL.size()) - 4.0);
    const auto d = static_cast<int64_t> (std::llround (delay));
    const double echoTarget = off ? 0.0 : std::clamp (params.echo, 0.0, 100.0) / 100.0;
    const double washTarget = off ? 0.0 : std::clamp (params.wash, 0.0, 100.0) / 100.0;
    const auto histSize = static_cast<int64_t> (hist.size()), size = static_cast<int64_t> (lineL.size());
    const double duckDown = fromDb (-8.0);

    for (int start = 0; start < n; start += kChunk)
    {
        const int len = std::min (kChunk, n - start);
        for (int i = 0; i < len; ++i)
        {
            const double x = mono[start + i];
            hist[static_cast<size_t> (now % histSize)] = x;
            blockE += x * x;
            if (++blockFill >= blockLen)
            {
                block (10.0 * std::log10 (blockE / blockLen + 1.0e-20));
                blockE = 0.0;
                blockFill = 0;
            }
            const auto rd = static_cast<size_t> (((pos - d) % size + size) % size), at = static_cast<size_t> (pos);
            const double yl = lpL.process (lineL[rd]), yr = lpR.process (lineR[rd]);
            lineL[at] += kFeedback * yr;   // ping-pong: each repeat on the other side
            lineR[at] += kFeedback * yl;
            echoGain += (echoTarget - echoGain) * glide;
            washGain += (washTarget - washGain) * glide;
            duck += ((inLine ? duckDown : 1.0) - duck) * glide;
            const double l = echoGain * duck * yl, r = echoGain * duck * yr;
            if (nch >= 2) { out[0][start + i] += l; out[1][start + i] += r; }
            else out[0][start + i] += 0.5 * (l + r);
            washIn[static_cast<size_t> (i)] = washGain * duck * 0.5 * (yl + yr);
            ++now;
            pos = static_cast<int> ((pos + 1) % size);
            lineL[static_cast<size_t> (pos)] = 0.0;   // (the slot for the next sample: only ever filled from now on)
            lineR[static_cast<size_t> (pos)] = 0.0;
        }
        std::array<double*, 2> o { out[0] + start, out[nch >= 2 ? 1 : 0] + start };
        wash.process (washIn.data(), o.data(), nch, len);
    }
    // Switched off: the echo fades (50 ms), the wash's tail rings out, then it stops (and is exact again).
    offFor = off ? offFor + n : 0;
    if (off && offFor > static_cast<int64_t> ((kWashSeconds + 1.0) * sr))
        reset();
}

} // namespace vox
