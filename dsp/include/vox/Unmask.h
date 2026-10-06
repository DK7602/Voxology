#pragma once

#include "BeatKey.h"
#include "Modules.h"

#include <array>
#include <atomic>
#include <climits>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

/** Unmask: Voxology on the BEAT makes room for the vocal. Voxology on the vocal (VOCAL mode) measures
    how strong the processed vocal is in six bands and publishes it to UnmaskLink, timestamped with
    the song position. Voxology on the beat (BEAT mode) reads it for the same song position and dips
    the beat a few dB in the bands the vocal is using, only while it's using them, by default only in
    the centre of the stereo picture (where the vocal sits), so the beat keeps its width.

    The bands (octaves) and how much each may dip (where words are understood counts most):
      200 Hz  body        weight 0.35
      400 Hz  warmth      0.6
      800 Hz  vowels      0.85
      1.6 kHz words       1.0
      3.15 kHz clarity    1.0
      6.3 kHz air / "s"   0.7
    Nothing here depends on fader levels: the dip follows when and where the vocal sings, relative to
    its own recent level, not how loud the beat is against it. */
namespace vox {

inline constexpr int kUnmaskBands = 6;
inline constexpr std::array<double, kUnmaskBands> kUnmaskHz { 200.0, 400.0, 800.0, 1600.0, 3150.0, 6300.0 };
inline constexpr std::array<double, kUnmaskBands> kUnmaskWeight { 0.35, 0.6, 0.85, 1.0, 1.0, 0.7 };
inline constexpr std::array<const char*, kUnmaskBands> kUnmaskNames { "200", "400", "800", "1.6k", "3.2k", "6.3k" };
inline constexpr double kUnmaskQ = 1.4;      // the analysis bands: about an octave wide
inline constexpr double kUnmaskDipQ = 1.9;   // the dips: a bit narrower, so neighbours barely add up
inline constexpr double kUnmaskSilentDb = -120.0;

// ------------------------------------------------------------------------------------------------
/** Band levels (dB, energy averaged over ~20 ms) of a mono signal. */
class BandAnalyser
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void process (const double* mono, int n) noexcept;
    void process (double x) noexcept;
    std::array<float, kUnmaskBands> levelsDb() const noexcept;

private:
    double sr = 48000.0;
    std::array<Biquad, kUnmaskBands> bp {};
    std::array<double, kUnmaskBands> ms {}, ms2 {};
    double coeff = 0.0;
};

// ------------------------------------------------------------------------------------------------
/** The link between Voxology instances in the same host process: up to kSlots vocals, each with a
    ring of timestamped band frames. One writer per slot (that vocal's audio thread); any number of
    readers. Lock-free on the audio side (a sequence number per frame guards against torn reads);
    names (UI only) are behind a mutex. */
class UnmaskLink
{
public:
    static constexpr int kSlots = 16;
    static constexpr int kFrames = 4096;          // at 128-sample hops: ~11 s of history at 48 kHz
    static constexpr int64_t kNoPosition = INT64_MIN;

    static UnmaskLink& instance();

    int claim();                                   // a free slot, or -1
    void release (int slot);
    void setName (int slot, const std::string& name);
    struct Source { int slot; std::string name; bool live; };
    std::vector<Source> sources() const;           // claimed slots (live = published in the last second)

    /** Vocal side (audio thread): one frame for the song position `pos` (kNoPosition if unknown). */
    void publish (int slot, int64_t pos, const std::array<float, kUnmaskBands>& db) noexcept;
    /** Beat side (audio thread): the frame at or just before `pos` (within maxAgeSamples), or the newest one
        when the position is unknown or not there; false if this slot has nothing recent. */
    bool read (int slot, int64_t pos, int64_t maxAgeSamples, std::array<float, kUnmaskBands>& db) const noexcept;
    bool isLive (int slot) const noexcept;

    /** Key from the beat: Voxology in BEAT mode publishes what it hears (audio thread, lock-free);
        a vocal reads the surest beat in the project. Kept until that instance goes (or leaves BEAT
        mode), so a stopped transport doesn't lose it. */
    struct BeatKeyInfo
    {
        bool present = false;     // a Voxology on a beat
        BeatKey::Result beat;     // what it hears (beat.ready: enough to say)
    };
    void publishKey (int slot, const BeatKeyInfo& k) noexcept;
    void clearKey (int slot) noexcept;
    BeatKeyInfo readBeatKey (int skipSlot) const noexcept;

    /** For tests: forget everything. */
    void resetForTests();

private:
    UnmaskLink();
    struct Frame
    {
        std::atomic<uint32_t> seq { 0 };
        std::atomic<int64_t> pos { kNoPosition };
        std::array<std::atomic<float>, kUnmaskBands> db {};
    };
    struct Slot
    {
        std::atomic<bool> used { false };
        std::atomic<uint32_t> write { 0 };         // frames written so far
        std::atomic<int64_t> lastMs { 0 };
        std::vector<Frame> frames;
        // Beat key (seqlock: odd = being written).
        std::atomic<uint32_t> keySeq { 0 };
        std::atomic<bool> keyPresent { false }, keyReady { false }, keyUnclear { false };
        std::atomic<int> keySet { 0 }, keyTonic { 0 };
        std::atomic<double> keyConf { 0.0 }, keyTune { 0.0 }, keyHeard { 0.0 };
    };
    static int64_t nowMs() noexcept;

    std::array<Slot, kSlots> slots;
    mutable std::mutex nameLock;
    std::array<std::string, kSlots> names;
};

// ------------------------------------------------------------------------------------------------
/** The beat side's dips. Six bell cuts at the Unmask bands, each up to Amount x 6 dB x the band's
    weight, scaled by how present the vocal is in that band right now (relative to its own recent
    peak there) and whether the beat has anything there at all. 15 ms in, 100 ms out. */
struct UnmaskParams
{
    double amount = 0.0;       // 0..100 %; 0 = off
    bool centreOnly = true;    // dip the middle (L + R) only, keep the sides
};

class Unmask
{
public:
    static constexpr double kMaxDipDb = 6.0;
    static constexpr double kPresenceWindowDb = 24.0;   // a band counts from 24 dB under its recent peak ...
    static constexpr double kPresenceRampDb = 18.0;     // ... fully present 6 dB under it
    static constexpr double kBeatFloorDb = -70.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const UnmaskParams& p) noexcept { params = p; }
    static bool isNeutral (const UnmaskParams& p) noexcept { return p.amount < 0.05; }

    /** vocalDb: the vocal's band levels for this stretch (nullptr = no vocal: the dips let go). */
    void process (double* const* ch, int nch, int n, const float* vocalDb) noexcept;

    std::array<double, kUnmaskBands> takeDipDb() noexcept;   // deepest dip per band since the last call (dB, <= 0)
    double currentDipDb (int band) const noexcept { return dip[static_cast<size_t> (band)]; }

private:
    void design (size_t b) noexcept;

    UnmaskParams params;
    double sr = 48000.0;
    int channels = 2;
    BandAnalyser beat;
    std::array<double, kUnmaskBands> vocalPeak {}, dip {}, designed {}, maxDip {};
    std::array<std::array<Biquad, kMaxChannels>, kUnmaskBands> bell {};
    double atk = 0.0, rel = 0.0, peakFall = 0.0;
    int countdown = 0;
    bool running = false;
};

} // namespace vox
