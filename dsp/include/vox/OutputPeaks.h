#pragma once

#include <array>

namespace vox {

/** The Output's PEAKS control, after the Output level (Beta 25: what Rackaholic's vocal chain has at its end):
      Off    the vocal leaves as it is.
      Clip   rounds off what reaches past the Ceiling (a soft knee from 3 dB under it; never past it). A touch of grit
             when pushed, nothing at all below the knee.
      Limit  turns the whole vocal down for a moment so no sample passes the Ceiling (instant on, about 80 ms back,
             both channels together), so loud words stay clean.
    No lookahead and no oversampling: it adds no latency (Voxology's timing never changes). The A side of A / B and
    BYPASS stay untouched. Message thread: setParams(); audio thread: process(). */
enum class PeakMode { off = 0, clip, limit };
inline constexpr int kPeakModes = 3;
inline constexpr std::array<const char*, kPeakModes> kPeakModeNames { "Off", "Clip", "Limit" };

class OutputPeaks
{
public:
    static constexpr double kMinCeilingDb = -6.0, kMaxCeilingDb = 0.0;
    static constexpr double kReleaseS = 0.080;
    static constexpr double kClipSoftness = 0.5;   // SoftClipper's knee: 3 dB under the Ceiling

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setParams (PeakMode, double ceilingDb) noexcept;

    /** One frame (nch samples, one per channel), in place. */
    void process (double* frame, int nch) noexcept;

    /** How far it turned the vocal down at most since the last call (dB, <= 0). */
    double takeReductionDb() noexcept;

private:
    PeakMode mode = PeakMode::off;
    double ceiling = 0.891250938;   // -1 dBFS
    double release = 0.0;
    double env = 1.0;               // Limit's gain now
    double deepest = 1.0;
};

} // namespace vox
