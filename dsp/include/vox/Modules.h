#pragma once

#include "Biquad.h"
#include "FilterDesign.h"

#include <array>
#include <cmath>
#include <vector>

/** The vocal insert modules (everything before the space effects). All run in double precision,
    detect on the channels linked (one gain for every channel, so a stereo vocal never shifts
    sideways), allocate only in prepare(), and pass audio through exactly (bit for bit) when off
    or at their neutral settings. Up to kMaxChannels channels. */
namespace vox {

inline constexpr int kMaxChannels = 2;

inline double toDb (double g) noexcept { return g > 1.0e-12 ? 20.0 * std::log10 (g) : -240.0; }
inline double fromDb (double db) noexcept { return std::pow (10.0, db / 20.0); }

// ------------------------------------------------------------------------------------------------
/** Cleanup: a low cut (removes rumble, handling noise and pops below the voice) and a gate that
    turns the gaps between phrases down by Range (room noise, headphone bleed, breaths are kept a
    little so the vocal still sounds natural).

    Look-ahead for free: in the chain the gate can listen() to the chain's input, which is
    `lookAheadAvailable` samples (Pitch's latency) ahead of the audio it turns down, so it opens a
    little before a word arrives and soft starts aren't chopped. Without listen() it hears the audio
    itself (no look-ahead). */
struct CleanupParams
{
    bool enabled = true;
    double lowCutHz = 20.0;     // <= kLowCutOffHz = off; 24 dB/oct otherwise
    double gateThrDb = -60.0;   // the gate opens above this (peak level, dBFS)
    double gateRangeDb = 0.0;   // how far the gaps are turned down; 0 = gate off
    double popAmount = 0.0;     // plosive remover, 0..100 % (PopRemover); 0 = off
    double breathDb = 0.0;      // breath control: how far breaths go down, 0..24 dB (BreathControl); 0 = off
};

class Cleanup
{
public:
    static constexpr double kLowCutOffHz = 20.5;
    static constexpr double kHysteresisDb = 4.0;   // closes this far below the threshold
    static constexpr double kHoldSeconds = 0.08;
    static constexpr double kLookAheadSeconds = 0.010;   // opens this long before the word

    /** maxBlock: the most samples listen() / process() get at once. */
    void prepare (double sampleRate, int numChannels, int lookAheadAvailable = 0, int maxBlock = 0);
    void reset() noexcept;
    void setParams (const CleanupParams& p) noexcept { params = p; }
    static bool isNeutral (const CleanupParams& p) noexcept
    {
        return ! p.enabled || (p.lowCutHz <= kLowCutOffHz && p.gateRangeDb < 0.05);
    }
    /** The chain's input for the block process() gets next (lookAheadAvailable samples ahead of it).
        Read only. Call before process(), with the same n. */
    void listen (const double* const* in, int nch, int n) noexcept;
    void process (double* const* ch, int nch, int n) noexcept;

    /** Deepest gate turn-down since the last call (dB, <= 0). */
    double takeGateDb() noexcept { const double v = minGainDb; minGainDb = 0.0; return v; }
    bool gateOpen() const noexcept { return open; }

private:
    void updateFilter() noexcept;

    CleanupParams params;
    double sr = 48000.0;
    int channels = 2;
    std::array<std::array<Biquad, 2>, kMaxChannels> hp {}, listenHp {};   // the low cut on the audio / on what listen() hears
    double designedHz = -1.0;
    std::vector<double> heard;   // listen()'s peaks for the next block
    bool listened = false;
    std::vector<double> peakLine;   // those peaks held back so they lead the audio by kLookAheadSeconds
    int peakPos = 0;
    int lookAhead = 0;
    double env = 0.0, envRelease = 0.0, gain = 1.0, gainDb = 0.0, gainAttack = 0.0, gainRelease = 0.0;
    int holdLeft = 0;
    bool open = true;
    double minGainDb = 0.0;
};

// ------------------------------------------------------------------------------------------------
/** Tone EQ: five musical vocal bands.
      1 Body      low shelf (Q 0.7)   80 - 400 Hz    weight / chest
      2 Mud       bell (Q 1.4)       150 - 800 Hz    boxy, cloudy build-up (usually cut)
      3 Nasal     bell (Q 1.6)       500 - 2000 Hz   honky, phone-like (usually cut)
      4 Presence  bell (Q 0.9)      2000 - 8000 Hz   words, clarity, "in front"
      5 Air       high shelf (Q 0.7) 6 - 18 kHz      breath, sheen, expensive top
    Gains and frequencies glide (~20 ms), so moves never click. */
inline constexpr int kEqBands = 5;

struct EqParams
{
    bool enabled = true;
    std::array<double, kEqBands> gainDb { 0, 0, 0, 0, 0 };
    std::array<double, kEqBands> freqHz { 180, 300, 900, 4000, 12000 };
};

struct EqBandInfo { const char* name; double lo, hi, def, q; int type; };   // type 0 low shelf, 1 bell, 2 high shelf
inline constexpr std::array<EqBandInfo, kEqBands> kEqBandInfo {{
    { "Body", 80.0, 400.0, 180.0, 0.7, 0 },
    { "Mud", 150.0, 800.0, 300.0, 1.4, 1 },
    { "Nasal", 500.0, 2000.0, 900.0, 1.6, 1 },
    { "Presence", 2000.0, 8000.0, 4000.0, 0.9, 1 },
    { "Air", 6000.0, 18000.0, 12000.0, 0.7, 2 },
}};
inline constexpr double kEqMaxDb = 12.0;

class VocalEQ
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const EqParams& p) noexcept { params = p; }
    void process (double* const* ch, int nch, int n) noexcept;

    /** Magnitude response of one band (dB) at f, for the given settings (UI / tests / Auto-Edit). */
    static double bandResponseDb (int band, double gainDb, double freqHz, double f, double sr);
    static double responseDb (const EqParams& p, double f, double sr);

private:
    static constexpr int kChunk = 16;
    EqParams params;
    double sr = 48000.0;
    int channels = 2;
    std::array<double, kEqBands> curGain {}, curFreq {};
    std::array<double, kEqBands> designedGain {}, designedFreq {};
    std::array<std::array<Biquad, kEqBands>, kMaxChannels> filt {};
    double glide = 0.0;
};

// ------------------------------------------------------------------------------------------------
/** De-Esser: turns harsh "s", "sh", "t" and "ch" sounds down only while they happen. It compares
    the sibilance band (above Frequency) with the whole voice, so it reacts the same however loud
    you sing, and cuts with a moving high shelf (the body of the word is never touched).
      Sensitivity  how easily a sound counts as sibilant (threshold -6 .. -20 dB band/voice ratio)
      Amount       how hard it cuts (up to 12 dB) */
struct DeEsserParams
{
    bool enabled = true;
    double amount = 0.0;        // 0..100 %; 0 = off
    double sensitivity = 50.0;  // 0..100 %
    double freqHz = 6000.0;     // 3..12 kHz
};

class DeEsser
{
public:
    static constexpr double kMaxCutDb = 12.0;
    static double thresholdDb (double sensitivity) noexcept { return -6.0 - 0.14 * sensitivity; }

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const DeEsserParams& p) noexcept { params = p; }
    static bool isNeutral (const DeEsserParams& p) noexcept { return ! p.enabled || p.amount < 0.05; }
    void process (double* const* ch, int nch, int n) noexcept;

    double takeCutDb() noexcept { const double v = maxCut; maxCut = 0.0; return -v; }   // dB, <= 0

private:
    void design (double cutDb) noexcept;

    DeEsserParams params;
    double sr = 48000.0;
    int channels = 2;
    std::array<Biquad, 2> detHp {};
    double detHz = -1.0;
    double hfEnv = 0.0, fullEnv = 0.0, envAtk = 0.0, envRel = 0.0;
    double cut = 0.0, cutAtk = 0.0, cutRel = 0.0, designedCut = 0.0;
    int countdown = 0;
    std::array<Biquad, kMaxChannels> shelf {};
    double maxCut = 0.0;
};

// ------------------------------------------------------------------------------------------------
/** Vocal Rider: rides the vocal's level like an engineer on a fader, so quiet words come up and
    loud ones go down toward Target before the compressor (which then works less and sounds more
    natural). It never boosts the gaps between phrases (gain holds while you're not singing). */
struct RiderParams
{
    bool enabled = true;
    double targetDb = -20.0;   // RMS dBFS the rider steers toward
    double rangeDb = 0.0;      // most it may move either way; 0 = off
    int speed = 1;             // 0 slow, 1 medium, 2 fast
};

class Rider
{
public:
    static constexpr double kVoiceBelowTargetDb = 24.0;   // quieter than this = a gap: hold
    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const RiderParams& p) noexcept { params = p; }
    static bool isNeutral (const RiderParams& p) noexcept { return ! p.enabled || p.rangeDb < 0.05; }
    void process (double* const* ch, int nch, int n) noexcept;

    double currentGainDb() const noexcept { return toDb (gain); }

private:
    RiderParams params;
    double sr = 48000.0;
    int channels = 2;
    double hpState = 0.0, hpPrev = 0.0, hpCoeff = 0.0;
    double ms = 0.0, msCoeff = 0.0, gain = 1.0;
};

// ------------------------------------------------------------------------------------------------
/** Compressor, two stages like a classic vocal chain:
      Peak   fast (1 ms attack, 60 ms release, 6:1, peak detector held 15 ms) catches loud syllables only
      Level  smooth, opto-style (30 ms RMS, 10 ms attack, release 60 - 600 ms: longer the longer it's been
             compressing) evens out the performance; Threshold + Ratio
    then Makeup gain and Mix (parallel compression: 100 % = fully compressed). The detector
    ignores the lows (80 Hz high-pass) so plosives and room rumble don't pump the vocal. */
struct CompParams
{
    bool enabled = true;
    double peakThrDb = 0.0;    // 0 = Peak stage off
    double thrDb = 0.0;        // Level stage threshold
    double ratio = 1.0;        // Level stage ratio; 1 = off
    double makeupDb = 0.0;
    double mix = 100.0;        // %
};

class VocalCompressor
{
public:
    static constexpr double kPeakRatio = 6.0;
    static constexpr double kPeakKneeDb = 4.0, kLevelKneeDb = 6.0;
    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const CompParams& p) noexcept { params = p; }
    static bool isNeutral (const CompParams& p) noexcept
    {
        return ! p.enabled || (p.peakThrDb > -0.05 && p.ratio < 1.005 && std::abs (p.makeupDb) < 0.005);
    }
    void process (double* const* ch, int nch, int n) noexcept;

    /** Static gain computers (dB of gain reduction, <= 0), exposed for tests. */
    static double peakGrDb (double levelDb, double thrDb) noexcept;
    static double levelGrDb (double levelDb, double thrDb, double ratio) noexcept;

    double takePeakGrDb() noexcept { const double v = minPeak; minPeak = 0.0; return v; }
    double takeLevelGrDb() noexcept { const double v = minLevel; minLevel = 0.0; return v; }

private:
    CompParams params;
    double sr = 48000.0;
    int channels = 2;
    Biquad scHp;
    double gr1 = 0.0, gr2 = 0.0, ms2 = 0.0, sustained = 0.0, pkEnv = 0.0, pkRel = 0.0;
    double a1 = 0.0, r1 = 0.0, a2 = 0.0, msC = 0.0, susC = 0.0, makeup = 1.0, mixGlide = 1.0, glide = 0.0;
    double minPeak = 0.0, minLevel = 0.0;
};

} // namespace vox
