#pragma once

#include "PitchCorrector.h"
#include "VocalChain.h"

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace vox {

/** Vocal styles Auto-Edit can aim for (what "finished" sounds like). */
inline constexpr int kStyles = 8;
inline constexpr std::array<const char*, kStyles> kStyleNames { "Trap Lead", "Rap", "Melodic", "Robot", "R&B", "Pop", "Folk", "Natural Singer" };
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
    double f0Median = 0.0;             // the reference singer's median pitch (Hz); 0 = unknown (older saved profiles)
    std::string about;                 // a line on what it sounds like (built-in references)
    bool builtin = false;              // from the built-in library (ReferenceLibrary.cpp), not a file
    std::string section;               // built-in menu section: "Male", "Female" or "Mixed" (empty for your own files)
    std::string pitch;                 // built-in: the Pitch mode its tuning sounds like ("Natural", or "Off" for rap), shown in the menu
    bool split = false;                // separated from a full song (UVR etc.): its low end (under kSplitLowHz) and its space aren't used
    bool lowFromStyle = false;         // built-in: its low end (under kSplitLowHz) isn't used (boomy / pitched-down source vocals)
};

/** Split vocals (a song's vocal separated with UVR / Demucs etc.), measured on 142 MUSDB18 songs split with UVR's
    MDX-Net vocal model against their real vocal stems (test-audio/split-measurements): s brightness and punch read the
    same (median +0.1 dB, 90 % within 1.5 / 0.8 dB), tone from 250 Hz up within about 1 dB (90 %: 3.6 dB), but the low
    end reads 9 dB too low at 100 Hz (the splitter gives it to the bass) and the space can't be measured (7 of 142). */
inline constexpr double kSplitLowHz = 250.0;
/** Does a file name look like a splitter's output (UVR "(Vocals)", model names, Demucs' "vocals")? */
bool looksSplit (const std::string& fileName);

struct AutoEditSettings
{
    int style = 0;
    int intensity = 1;
    double bpm = 0.0;          // host tempo; 0 = unknown (120 is used and the report says so)
    const ReferenceProfile* reference = nullptr;   // when set (and ok): match it instead of the style's target
    bool beatKeyKnown = false;  // Pitch follows a beat whose key is known: use it, not the voice's guess
    int beatKeyNote = 0, beatScale = 1;   // the key / scale Pitch uses from it (followBeatKey)
    std::string beatKeyName;              // e.g. "B minor"
    bool beatKeyFromVoice = false;        // that key was learned from the voice (no beat to hear)
    const std::vector<float>* beat = nullptr;   // Vocal Fit: the beat from the side-chain, recorded with the vocal (mono, same length)
    bool unmaskOnBeat = false;            // a Voxology in BEAT mode is in the project (Unmask can already make room)
    const std::atomic<bool>* cancel = nullptr;  // set it (another thread) to stop early: the plug-in is closing (result: not ok)
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

/** Vocal Fit: what Auto-Edit heard of the beat on the side-chain, recorded at the same moments as the vocal.
    The beat's character against the voice (its tone, hats, 808, how much it moves) doesn't depend on how loud
    the beat is sent; the level the two play at does (it's only right with the vocal fader at 0 dB and a 0 dB
    post-fader send). */
struct BeatAnalysis
{
    bool heard = false;                // a beat played under the vocal (at least kBeatMinSeconds while singing)
    double seconds = 0.0;              // beat heard in all (above -60 dBFS)
    double underVoiceSeconds = 0.0;    // ... of it while the vocal sings
    double lufs = -120.0;              // the beat, as it reaches the side-chain
    double vocalOverBeatDb = 0.0;      // vocal loudness minus beat loudness (LUFS, the same stretch of song)
    std::vector<double> bandDb;        // the beat's third-octave balance while the vocal sings (vs its 500 Hz - 2 kHz average)
    std::vector<double> overDb;        // per band, while singing: vocal minus beat (dB), less that at 500 Hz - 2 kHz:
                                       // + = the voice stands out from the beat more there, - = the beat covers it more
    double brightDb = -120.0;          // hi-hats / top: 5 - 12 kHz vs 500 Hz - 2 kHz (dB)
    double lowDb = -120.0;             // kick / 808: under 100 Hz vs the whole beat (dB)
    double busyDb = 0.0;               // how much the beat moves: P90 - P10 of its 50 ms levels (dB); low = a steady wall of sound
    double yourVocalCorr = 0.0;        // how closely the beat's 1 - 4 kHz level follows your vocal's, word by word (best within
                                       // +-250 ms): your own vocal is IN the "beat" (a full mix or a bounce sent to the side-chain)
    bool hasYourVocal = false;         // yourVocalCorr >= kYourVocalCorr
};
inline constexpr double kBeatMinSeconds = 4.0;
/** MUSDB18-7: beats alone reach 0.51 at most (0 of 144 over 0.6, nor 0 of 144 other songs' full mixes); a song's own
    full mix reads 0.81 typically (122 of 144 over 0.6; the rest have the vocal low in the mix, where it skews little). */
inline constexpr double kYourVocalCorr = 0.6;
inline constexpr double kFitLevelMaxDb = 6.0;   // FIT LEVEL moves Output at most this far (a wrong reading can't make a big jump)

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
    BeatAnalysis beat;                         // Vocal Fit (heard = false: no beat on the side-chain)
    std::vector<AutoEditReason> fit;           // Vocal Fit: what it heard in the beat and did about it (module "fit")
    std::array<double, 3> fitToneDb {};        // Vocal Fit: Tone EQ target moved for this beat: low-mids, presence, air (dB)
    double fitPunchDb = 0.0;                   // Vocal Fit: Compressor aims for this much more (+) or less (-) punch (dB)
    double fitLevelDb = 0.0;                   // Vocal Fit: FIT LEVEL button: Output change that puts the vocal where pros
                                               // sit against the beat (at most +-kFitLevelMaxDb; 0 = no button)
    double fitLevelFullDb = 0.0;               // ... the whole move, before that cap
    ChainParams params;                        // the settings to apply
    double outputLufs = -120.0;
};

/** Third-octave band centres used by the analysis and the style targets. */
const std::vector<double>& analysisBands();
/** The style's target balance at the analysis bands (dB vs the 500 Hz - 2 kHz average), for a voice whose
    median pitch is f0Hz (0 = unknown: a typical 200 Hz voice). Measured from finished pro vocals. */
std::vector<double> styleTarget (int style, double f0Hz = 0.0);

VocalAnalysis analyseVocal (const std::vector<std::vector<float>>& audio, double sampleRate);

/** Vocal Fit: the beat (mono) against the vocal it was recorded with (the same moments, the same length). */
BeatAnalysis analyseBeat (const std::vector<float>& beat, const std::vector<std::vector<float>>& vocal, double sampleRate);

/** Measures a reference vocal. It should be the vocal on its own: a full song (beat + vocal) reads ~7 dB
    off the real vocal's tone (measured on 144 pro songs), so that gets a warning (it still works, roughly). */
ReferenceProfile analyseReference (const std::vector<std::vector<float>>& audio, double sampleRate, const std::string& name,
                                   const std::atomic<bool>* cancel = nullptr);

/** The built-in reference library: finished pro vocals (measurements only), in menu order: Male, Female, Mixed. */
const std::vector<ReferenceProfile>& builtinReferences();
/** A built-in reference by name; also finds the names used before v0.28 (saved in older projects). nullptr = none. */
const ReferenceProfile* findBuiltinReference (const std::string& name);

/** Listens to the vocal, decides every module and explains each choice. */
AutoEditResult autoEdit (const std::vector<std::vector<float>>& audio, double sampleRate, const AutoEditSettings& settings);

} // namespace vox
