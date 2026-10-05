#pragma once

#include "PitchCorrector.h"
#include "VocalChain.h"

#include <array>
#include <string>
#include <vector>

namespace vox {

/** Vocal styles Auto-Edit can aim for (what "finished" sounds like). */
inline constexpr int kStyles = 5;
inline constexpr std::array<const char*, kStyles> kStyleNames { "Trap Lead", "Rap", "Melodic", "Ad-libs", "R&B" };
inline constexpr int kIntensities = 3;
inline constexpr std::array<const char*, kIntensities> kIntensityNames { "Light", "Balanced", "Strong" };

struct AutoEditSettings
{
    int style = 0;
    int intensity = 1;
    double bpm = 0.0;          // host tempo; 0 = unknown (120 is used and the report says so)
};

/** One explained decision: which module and control, the value chosen and why (plain language). */
struct AutoEditReason
{
    std::string module;        // "cleanup", "eq", "deess", "rider", "comp", "sat", "double", "delay", "reverb", "out"
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

/** Listens to the vocal, decides every module and explains each choice. */
AutoEditResult autoEdit (const std::vector<std::vector<float>>& audio, double sampleRate, const AutoEditSettings& settings);

} // namespace vox
