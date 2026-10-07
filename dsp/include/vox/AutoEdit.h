#pragma once

#include "PitchCorrector.h"
#include "VocalChain.h"

#include <array>
#include <string>
#include <vector>

namespace vox {

/** Vocal styles Auto-Edit can aim for (what "finished" sounds like). */
inline constexpr int kStyles = 8;
inline constexpr std::array<const char*, kStyles> kStyleNames { "Trap Lead", "Rap", "Melodic", "Ad-libs", "R&B", "Pop", "Folk", "Natural Singer" };
/** Styles that are sung rather than rapped (a little breath stays, a softer presence). */
inline constexpr bool isSungStyle (int style) noexcept { return style == 2 || style >= 4; }
inline constexpr int kIntensities = 3;
inline constexpr std::array<const char*, kIntensities> kIntensityNames { "Light", "Balanced", "Strong" };

/** A finished vocal (an acapella) to aim at instead of the style's built-in target: its tone,
    "s" level, punch and space. Made with analyseReference(); keeps no audio, only the measurements. */
struct ReferenceProfile
{
    bool ok = false;
    std::string name;                  // shown in the UI and report
    std::string problem;               // why it can't be used (when !ok), plain language
    std::string warning;               // usable, but with a catch (e.g. it sounds like a full song)
    std::vector<double> bandDb;        // third-octave balance vs the 500 Hz - 2 kHz average (analysisBands())
    double sibilanceDb = -120.0;       // loud "s" vs the voice; -120 = none heard
    double microDynDb = 0.0;           // word-to-word punch (see VocalAnalysis)
    double tailDb = -120.0;            // space after phrases; -120 = unknown
    double voicedSeconds = 0.0;
};

struct AutoEditSettings
{
    int style = 0;
    int intensity = 1;
    double bpm = 0.0;          // host tempo; 0 = unknown (120 is used and the report says so)
    const ReferenceProfile* reference = nullptr;   // when set (and ok): match it instead of the style's target
    bool beatKeyKnown = false;  // Pitch follows a beat whose key is known: use it, not the voice's guess
    int beatKeyNote = 0, beatScale = 1;   // the key / scale Pitch uses from it (followBeatKey)
    std::string beatKeyName;              // e.g. "B minor"
};

/** One explained decision: which module and control, the value chosen and why (plain language). */
struct AutoEditReason
{
    std::string module;        // "cleanup", "eq", "dyneq", "deess", "rider", "comp", "sat", "double", "delay", "reverb", "out"
    std::string control, value, why;
};

/** What Auto-Edit measured on the vocal it heard. */
struct VocalAnalysis
{
    double seconds = 0.0, voicedSeconds = 0.0;
    double peakDb = -120.0;
    double inputLufs = -120.0;
    double voiceRmsDb = -120.0;        // median level while singing (RMS dBFS)
    double noiseFloorDb = -120.0;      // gaps between phrases (RMS dBFS); -120 = no gaps heard
    double noisePeakDb = -120.0;       // loud end of the noise peaks (dBFS)
    double quietWordPeakDb = -120.0;   // quiet end of the word peaks (dBFS)
    bool heardGaps = false;
    int clippedRuns = 0;
    double f0Median = 0.0, f0Low = 0.0;   // Hz; 0 = no pitch found
    double pitchedShare = 0.0;         // % of singing frames with a clear pitch
    double heldShare = 0.0;            // % of pitched time in held notes (>= 160 ms within half a semitone): sung vs rapped
    KeyGuess key;                      // from the sung notes
    double rumbleDb = -120.0;          // energy below 60 Hz vs the whole vocal (dB)
    std::vector<double> bandDb;        // third-octave balance vs the 500 Hz - 2 kHz average (dB)
    std::vector<double> bandHz;
    double sibilanceDb = -120.0;       // loud "s" level vs the voice (dB); -120 = none heard
    double sibilanceHz = 0.0;
    double sibilantShare = 0.0;        // % of singing frames that are sibilant
    double rangeDb = 0.0;              // loud vs quiet phrases (P90 - P10 of 400 ms levels, dB)
    double microDynDb = 0.0;           // word-to-word punch: P95 - P50 of 50 ms levels while singing (dB; low = compressed)
    double tailDb = -120.0;            // what rings on after a phrase ends (100 - 300 ms later vs the phrase's end, dB); -120 = unknown
    double lowBassDb = -120.0;         // energy under 100 Hz vs the whole (dB): a beat's kick and 808 live there
    bool stereo = false, oneSided = false;
};

struct AutoEditResult
{
    bool ok = false;
    std::string summary;
    std::string tipTitle, tip;                 // when !ok: what to do next
    std::vector<std::string> notes;            // "TITLE: text" + "NEED: ..." + "STEP: ..." lines
    std::vector<std::string> suggestions;      // extra ideas, one line each
    std::vector<AutoEditReason> reasons;
    std::array<bool, kModules> kept {};        // looked at and chose no change
    VocalAnalysis analysis;
    ChainParams params;                        // the settings to apply
    double outputLufs = -120.0;
};

/** Third-octave band centres used by the analysis and the style targets. */
const std::vector<double>& analysisBands();
/** The style's target balance at the analysis bands (dB vs the 500 Hz - 2 kHz average). */
std::vector<double> styleTarget (int style);

VocalAnalysis analyseVocal (const std::vector<std::vector<float>>& audio, double sampleRate);

/** Measures a reference vocal. It should be the vocal on its own: a full song (beat + vocal) reads ~7 dB
    off the real vocal's tone (measured on 144 pro songs), so that gets a warning (it still works, roughly). */
ReferenceProfile analyseReference (const std::vector<std::vector<float>>& audio, double sampleRate, const std::string& name);

/** Listens to the vocal, decides every module and explains each choice. */
AutoEditResult autoEdit (const std::vector<std::vector<float>>& audio, double sampleRate, const AutoEditSettings& settings);

} // namespace vox
