#pragma once

#include <array>
#include <cstdint>
#include <vector>

/** De-clip: repairs a recording's flattened peaks (it was recorded or exported too hot).

    Detection: 3 or more samples in a row at exactly the same loud value (within 0.01 %, above a quarter
    of the recent peak and -40 dBFS) - the signature of hard clipping; smooth peaks never do that.
    Repair: the flat stretch is redrawn from the voice around it (Janssen's least-squares interpolation:
    an all-pole model fitted to 512 samples each side - more than one voice cycle, then the missing samples that fit it best),
    kept at least at the clip level with the clip's sign (the true peak was there or higher).
    Runs over 64 samples (1.3 ms at 48 kHz) are left alone. Each channel on its own.

    Constant latency kLookahead. Exact delayed pass-through on audio without flat runs. No allocation in
    process(). Offline and live give the same result. */
namespace vox {

class DeClip
{
public:
    static constexpr int kLookahead = 640;   // samples (13 ms at 48 kHz): a run plus its context must fit
    static constexpr int kContext = 512, kMaxRun = 64, kOrder = 32;

    void prepare (int numChannels);
    void reset() noexcept;
    void setEnabled (bool on) noexcept { enabled = on; }
    void process (double* const* ch, int nch, int n) noexcept;
    int takeRepairs() noexcept { const int r = repairs; repairs = 0; return r; }   // runs fixed since the last call

private:
    struct Channel
    {
        std::vector<double> ring;   // kRing, indexed by absolute time
        int64_t runStart = -1;      // start of the flat run being followed (-1 = none)
        double runValue = 0.0;
        std::vector<int64_t> pendingA, pendingB;   // runs waiting for their right-hand context
        double peak = 0.0;          // recent peak (slow decay)
    };
    void repair (Channel& c, int64_t a, int64_t b) noexcept;

    static constexpr int kRing = 2048, kMask = kRing - 1;
    std::vector<Channel> chans;
    int64_t now = 0;
    bool enabled = true;
    int repairs = 0;
    // Work space for one repair (sized for the worst case).
    std::vector<double> win, known, ac, coef, R, A, rhs, sol;
};

} // namespace vox
