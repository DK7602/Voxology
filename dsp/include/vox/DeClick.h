#pragma once

#include "Modules.h"

#include <array>
#include <cstdint>
#include <vector>

/** Mouth de-click (part of the Cleanup module, after Pops): lip smacks, tongue clicks and the sticky "tk" of a dry
    mouth. A click is a very short burst of top end (well under 4 ms) that jumps far above the top end around it; a
    "t" or "k" lasts longer (5 - 30 ms) and an "s" much longer, so those are left alone.

    Look-ahead for free, like Pops and Breaths: it listens to the chain's input (the side-chain, Pitch's latency
    ahead of the audio), so it knows how long a burst lasts before that audio arrives, and only then decides. A click
    is fixed by turning the sound down for its few milliseconds (0.5 ms fades), never further than the click sticks
    out of everything around it: in a gap it goes, inside a word (where the voice covers it) the dip stays small. Linked channels, double precision, no allocation after prepare(), exact
    pass-through when off. */
namespace vox {

struct DeClickParams
{
    double amount = 0.0;   // 0..100 %: how sensitive (and how deep, up to kMaxCutDb); 0 = off
};

class DeClick
{
public:
    static constexpr double kSplitHz = 1500.0;          // the detector listens above this
    static constexpr double kMaxClickSeconds = 0.004;   // longer bursts are consonants ("t", "k"), left alone
    static constexpr double kMaxCutDb = 30.0;
    static constexpr double kPadSeconds = 0.0008;       // the cut starts this long before the click and ends this long after
    static constexpr double kFadeSeconds = 0.0005;
    static constexpr double kBlockSeconds = 0.0005, kContextSeconds = 0.010;

    /** lookAheadAvailable: how far the side-chain runs ahead of the audio. A click is decided about 16 ms after it
        starts (the context after it, plus its length); with less look-ahead (Record mode) it stays off. */
    void prepare (double sampleRate, int numChannels, int lookAheadAvailable);
    void reset() noexcept;
    void setParams (const DeClickParams& p) noexcept { params = p; }
    static bool isNeutral (const DeClickParams& p) noexcept { return p.amount < 0.05; }
    bool isAvailable() const noexcept { return available; }
    /** sc: the mono side-chain, lookAheadAvailable samples ahead of ch. */
    void process (double* const* ch, int nch, int n, const double* sc) noexcept;

    /** Clicks fixed since the last call, and the deepest cut (dB, <= 0). */
    int takeClicks() noexcept { const int c = clicks; clicks = 0; return c; }
    double takeCutDb() noexcept { const double v = deepest; deepest = 0.0; return v; }
    /** Tests: reset, with the sample clock started at `sample` (as if that much had already played). */
    void startClockAt (int64_t sample) noexcept;

private:
    void markClick (int64_t start, int64_t end, double cutDb) noexcept;
    void judgeBlock() noexcept;

    DeClickParams params;
    double sr = 48000.0;
    int channels = 2, lookAhead = 0;
    std::array<Biquad, 2> detHp {};                                   // the side-chain's top end
    Biquad fullHp;                                                    // the side-chain over 80 Hz (everything)
    // Detector: the top end's peak per 0.5 ms block; each block is judged against the typical peaks (85th percentile)
    // of the kContextSeconds around it, kContextSeconds after it arrived (so the look-ahead sees both sides).
    int blockLen = 24, blockFill = 0, contextBlocks = 20;
    double blockPeak = 0.0;
    int64_t blocks = 0;                // blocks finished (side-chain)
    std::vector<double> peaks, fullPeaks;   // rings of block peaks: the top end, everything (2 x contextBlocks + 1)
    double blockPeakFull = 0.0, runPeakFull = 0.0, runBgFull = 0.0;
    std::vector<double> sortBuf;
    int64_t runStart = -1, runEnd = -1;   // flagged blocks in a row (block indices), -1 = none
    double runPeak = 0.0, runBg = 0.0;
    double ratio = 1.0, floorAmp = 0.0, maxCut = 0.0;
    int64_t maxBlocks = 8;
    int64_t now = 0;                   // side-chain sample count (64-bit: a long is 32 on Windows, 12 h at 48 kHz)
    std::vector<double> cutLine;       // per sample, the cut (dB, <= 0) to apply when that audio arrives (ring)
    double gainDb = 0.0, fadeStep = 0.0;
    int clicks = 0;
    double deepest = 0.0;
    bool available = true;   // enough look-ahead to decide in time
};

} // namespace vox
