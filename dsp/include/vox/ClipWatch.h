#pragma once

#include <vector>

/** Clip watch: spots a vocal that arrives clipped or too hot. Read-only (the audio is not touched), no latency.

    Clipped: 3 or more samples in a row at exactly the same loud value (above a quarter of the recent peak and
    -40 dBFS), at any level: the signature of hard clipping or a brick-wall stage before Voxology. Smooth audio
    never does that. Too hot: samples over full scale (|x| > 1, possible in a 32-bit float mix). */
namespace vox {

class ClipWatch
{
public:
    static constexpr int kMinRun = 3;

    struct Counts
    {
        int runs = 0;    // clipped stretches
        int overs = 0;   // samples over full scale
    };

    void prepare (int numChannels);
    void reset() noexcept;
    void process (const double* const* ch, int nch, int n) noexcept;
    Counts take() noexcept { const Counts c = counts; counts = {}; return c; }   // since the last call

private:
    struct Channel
    {
        double prev = 0.0, peak = 0.0;
        int run = 0;
    };
    std::vector<Channel> chans;
    Counts counts;
};

} // namespace vox
