#include "vox/DeClick.h"

#include <algorithm>
#include <cmath>

namespace vox {

namespace {
constexpr double kFloorDb = -70.0;    // top-end peaks quieter than this are inaudible: left alone
constexpr double kPercentile = 0.85;  // the "typical peak" around a block
constexpr double kEdgeRatio = 0.25;   // -12 dB: the sound on one side of a burst counts as quiet under this (vs the burst)
}

void DeClick::prepare (double sampleRate, int numChannels, int lookAheadAvailable)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    lookAhead = std::max (0, lookAheadAvailable);
    for (auto& f : detHp) design::apply (f, design::butterworth (true, kSplitHz, sr));
    design::apply (fullHp, design::butterworth (true, 80.0, sr));
    blockLen = std::max (4, static_cast<int> (std::lround (kBlockSeconds * sr)));
    contextBlocks = std::max (4, static_cast<int> (std::lround (kContextSeconds / kBlockSeconds)));
    maxBlocks = std::max (1L, static_cast<long> (std::lround (kMaxClickSeconds / kBlockSeconds)));
    // The ring holds a block's context on both sides, plus room to look back past a whole click.
    peaks.assign (static_cast<size_t> (2 * contextBlocks + maxBlocks + 4), 0.0);
    fullPeaks.assign (peaks.size(), 0.0);
    sortBuf.assign (static_cast<size_t> (2 * contextBlocks + 1), 0.0);
    const int pad = static_cast<int> (std::ceil (kPadSeconds * sr));
    cutLine.assign (static_cast<size_t> (lookAhead + 2 * pad + 4), 0.0);
    // A click is known (contextBlocks after it, plus its length, plus a block) before its audio, padded, arrives.
    available = lookAhead >= (contextBlocks + maxBlocks + 2) * blockLen + pad + 1;
    fadeStep = 1.0 / (kFadeSeconds * sr);
    floorAmp = fromDb (kFloorDb);
    reset();
}

void DeClick::reset() noexcept
{
    for (auto& f : detHp) f.reset();
    fullHp.reset();
    std::fill (peaks.begin(), peaks.end(), 0.0);
    std::fill (fullPeaks.begin(), fullPeaks.end(), 0.0);
    blockPeakFull = 0.0;
    std::fill (cutLine.begin(), cutLine.end(), 0.0);
    blockFill = 0;
    blockPeak = 0.0;
    blocks = 0;
    runStart = runEnd = -1;
    runPeak = runBg = 0.0;
    now = 0;
    gainDb = 0.0;
    clicks = 0;
    deepest = 0.0;
}

void DeClick::markClick (long start, long end, double cutDb) noexcept
{
    const long pad = static_cast<long> (std::ceil (kPadSeconds * sr));
    const auto size = static_cast<long> (cutLine.size());
    for (long t = start - pad; t <= end + pad; ++t)
    {
        auto& slot = cutLine[static_cast<size_t> (((t % size) + size) % size)];
        slot = std::min (slot, cutDb);
    }
    ++clicks;
    deepest = std::min (deepest, cutDb);
}

void DeClick::judgeBlock() noexcept
{
    // The block contextBlocks old against the typical peaks (85th percentile) of the contextBlocks on both sides of it.
    const auto size = static_cast<long> (peaks.size());
    const long centre = blocks - 1 - contextBlocks;
    if (centre < contextBlocks) return;
    auto at = [size] (const std::vector<double>& ring, long b) { return ring[static_cast<size_t> (((b % size) + size) % size)]; };
    auto typicalOf = [&] (const std::vector<double>& ring, long from, long to)   // blocks from..to (inclusive)
    {
        const auto count = static_cast<size_t> (to - from + 1);
        for (size_t j = 0; j < count; ++j) sortBuf[j] = at (ring, from + static_cast<long> (j));
        const auto k = static_cast<std::ptrdiff_t> (kPercentile * static_cast<double> (count - 1));
        std::nth_element (sortBuf.begin(), sortBuf.begin() + k, sortBuf.begin() + static_cast<std::ptrdiff_t> (count));
        return std::max (sortBuf[static_cast<size_t> (k)], floorAmp * 0.1);
    };
    const double peak = at (peaks, centre), peakFull = at (fullPeaks, centre);
    const double typical = typicalOf (peaks, centre - contextBlocks, centre + contextBlocks);
    const double typicalFull = typicalOf (fullPeaks, centre - contextBlocks, centre + contextBlocks);
    const bool flagged = peak > ratio * typical && peak > floorAmp;
    if (flagged)
    {
        if (runStart == -1) { runStart = runEnd = centre; runPeak = peak; runBg = typical; runPeakFull = peakFull; runBgFull = typicalFull; }
        else if (runStart >= 0)
        {
            runEnd = centre;
            runPeak = std::max (runPeak, peak);
            runPeakFull = std::max (runPeakFull, peakFull);
            if (runEnd - runStart + 1 > maxBlocks) runStart = -2;   // too long: a consonant ("t", "k"), not a click
        }
    }
    else if (runStart != -1)
    {
        if (runStart >= 0)
        {
            // Quiet on one side and the voice on the other: a word's hard start or end, the release of a "b", "d", or a
            // final "t" / "k". A consonant, not a click: left alone. (A click is in a gap, quiet on both sides, or inside
            // a word, the voice on both sides.)
            const double before = typicalOf (fullPeaks, runStart - contextBlocks, runStart - 1);
            const double after = typicalOf (fullPeaks, runEnd + 1, centre + contextBlocks);
            const bool quietBefore = before < kEdgeRatio * runPeakFull, quietAfter = after < kEdgeRatio * runPeakFull;
            const bool onset = quietBefore != quietAfter;
            // A click: the whole sound turned down for its few ms, back to about 6 dB over the top end's typical peaks
            // around it, but never further than the click sticks out of everything (inside a word, the voice is louder
            // than the click: the dip stays small), and at most maxCut.
            const double excessDb = 20.0 * std::log10 (runPeak / runBg), excessFull = 20.0 * std::log10 (runPeakFull / runBgFull);
            const double cut = -std::min ({ maxCut, std::max (0.0, excessDb - 6.0), std::max (0.0, excessFull) });
            if (! onset && cut < -0.5) markClick (runStart * blockLen, (runEnd + 1) * blockLen, cut);
        }
        runStart = runEnd = -1;
    }
}

void DeClick::process (double* const* ch, int nch, int n, const double* sc) noexcept
{
    nch = std::min (nch, channels);
    const bool off = isNeutral (params) || sc == nullptr || ! available;
    const double amount = std::clamp (params.amount, 0.0, 100.0) / 100.0;
    // More Amount: more sensitive (a peak 18 dB over the typical peaks around it at 0 %, 14 dB at 100 %) and deeper.
    ratio = fromDb (18.0 - 4.0 * amount);
    maxCut = kMaxCutDb * amount;
    const auto size = static_cast<long> (cutLine.size());
    const auto ringSize = static_cast<long> (peaks.size());

    for (int i = 0; i < n; ++i)
    {
        // Detector, on the side-chain (lookAhead samples ahead of the audio).
        const double in = sc != nullptr ? sc[i] : 0.0;
        double h = in;
        for (auto& f : detHp) h = f.process (h);
        blockPeak = std::max (blockPeak, std::abs (h));
        blockPeakFull = std::max (blockPeakFull, std::abs (fullHp.process (in)));
        if (++blockFill >= blockLen)
        {
            peaks[static_cast<size_t> (blocks % ringSize)] = blockPeak;
            fullPeaks[static_cast<size_t> (blocks % ringSize)] = blockPeakFull;
            blockPeakFull = 0.0;
            ++blocks;
            blockFill = 0;
            blockPeak = 0.0;
            if (! off) judgeBlock();
        }

        // The audio this sample: lookAhead samples behind the side-chain.
        auto& slot = cutLine[static_cast<size_t> ((((now - lookAhead) % size) + size) % size)];
        const double target = off ? 0.0 : slot;
        slot = 0.0;
        const double stepDb = std::max (1.0, maxCut) * fadeStep;   // the whole depth in kFadeSeconds
        if (target < gainDb) gainDb = std::max (target, gainDb - stepDb);
        else if (target > gainDb) gainDb = std::min (target, gainDb + stepDb);
        if (gainDb < -0.01)
        {
            const double g = fromDb (gainDb);
            for (int c = 0; c < nch; ++c) ch[c][i] *= g;
        }
        ++now;
    }
    if (off) { runStart = runEnd = -1; }
}

} // namespace vox
