#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "vox/BeatKey.h"
#include "vox/LevelMatch.h"
#include "vox/LoudnessMeter.h"
#include "vox/Unmask.h"
#include "vox/VocalChain.h"
#include "suite/SuiteLink.h"
#include "AutoEditController.h"
#include "ChainLibrary.h"
#include "Params.h"
#include "SpectrumTap.h"

#include <atomic>

/** Voxology: an all-in-one vocal chain (Cleanup -> Tone EQ -> De-Esser -> Rider -> Compressor ->
    Saturation -> Doubler -> Delay -> Reverb -> Output) with Auto-Edit, which listens to the vocal,
    sets every module and explains why. */
class VoxologyAudioProcessor final : public juce::AudioProcessor, private juce::Timer, private juce::AsyncUpdater
{
public:
    VoxologyAudioProcessor();
    ~VoxologyAudioProcessor() override;
    void updateTrackProperties (const TrackProperties& properties) override;

    /** Unmask (BEAT mode): which vocal to make room for. -1 = every Voxology vocal in the project. */
    std::atomic<int> unmaskSource { -1 };
    int getLinkSlot() const noexcept { return linkSlot; }
    void setUnmaskSourceByName (const juce::String& name);   // "" = every vocal (message thread)
    juce::String getUnmaskSourceName() const { const juce::ScopedLock sl (styleLock); return unmaskSourceName; }
    bool isBeatMode() const noexcept { return modeParam != nullptr && modeParam->load() > 0.5f; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;   // off the suite link (an inactive project, an insert switched off)
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    /** The DAW's own bypass (its power button): the dry vocal on both sides, delayed by the latency like Voxology's
        own bypass, with the same 20 ms glide. (JUCE's default passed it undelayed while the DAW still made up for
        the latency, so a bypassed vocal played early; a mono vocal also lost its right side.) */
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    /** The DAW bypassed Voxology in the last block it played (any thread): it isn't processing the vocal now. */
    bool isHostBypassed() const noexcept { return hostBypassed.load (std::memory_order_relaxed); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // Pitch: MIDI notes / Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Delay + reverb tails: a long Delay with 95 % Feedback rings for a minute. The DAW may stop calling a plug-in
    // that has been silent for this long (8 s cut that off, and the delay later played the old echo it still held).
    double getTailLengthSeconds() const override { return 60.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<bool> uiRed { false };   // the logo easter egg: red instead of blue (saved with the project)

    struct Meters
    {
        std::atomic<float> inShort { -100.0f }, outShort { -100.0f };     // LUFS
        std::atomic<float> inPeak { -100.0f }, outPeak { -100.0f };       // dBFS, held until read
        std::atomic<float> gate { 0.0f }, deEss { 0.0f }, rider { 0.0f };  // dB
        std::atomic<float> pops { 0.0f }, breath { 0.0f }, noise { 0.0f }, click { 0.0f }, smooth { 0.0f };  // dB, held until read
        std::atomic<int> clicks { 0 };                                      // mouth clicks fixed since the editor opened
        std::atomic<int> clipRuns { 0 }, overs { 0 };                      // incoming vocal: clipped stretches, samples over 0 dBFS (running totals)
        std::array<std::atomic<float>, vox::kDynBands> dynEq {};            // dB per band, held until read
        std::atomic<float> peakGr { 0.0f }, levelGr { 0.0f };             // dB, held until read
        std::atomic<float> suiteGr { 0.0f };                              // dB the compressor takes off (>= 0), held until the suite link reads it
        std::atomic<float> satResidual { 0.0f }, satSignal { 0.0f };      // energies, drained by the editor
        std::atomic<float> matchDb { 0.0f };
        std::atomic<float> bpm { 0.0f };
        std::atomic<float> pitchSung { 0.0f };      // fractional MIDI note heard, 0 = none
        std::atomic<int> pitchTarget { -1 };        // the note it pulls to, -1 = none
        std::atomic<float> pitchCorr { 0.0f };      // semitones applied
        std::array<std::atomic<float>, vox::kUnmaskBands> umDip {};     // BEAT: deepest dip per band, dB, held until read
        std::array<std::atomic<float>, vox::kUnmaskBands> umVocal {};   // BEAT: the linked vocal's band levels, dB
        std::atomic<int> umLink { 0 };              // BEAT: vocals heard this block (0 = none)
        std::array<std::atomic<int>, vox::kMaxVoices> hvNotes { -1, -1, -1, -1, -1, -1, -1, -1 };   // the harmony voices' notes (MIDI, -1 = none)
        // Key (Auto). VOCAL: 0 manual, 1 following a key, 2 nothing heard yet, 3 listening / not sure yet;
        // bkSource: what it hears (0 nothing, 1 Voxology on a beat, 2 the beat on the side-chain, 3 the voice).
        // scState: the side-chain input (0 off, 1 on but no sound for 2 s, 2 sound coming in).
        // BEAT: this beat's own reading (bkState 4 = listening, 5 = has a key).
        std::atomic<int> bkState { 0 }, bkSource { 0 }, scState { 0 }, bkKey { 0 }, bkMode { 0 }, bkSet { 0 }, bkUnclear { 0 }, bkOpen { -1 }, keyUsed { 0 }, scaleUsed { 0 };
        std::atomic<int> midiNotes { 0 }, notesUsed { 0 }, heldNotes { 0 };   // heldNotes: how many MIDI notes are held now
        std::atomic<int> recMode { 0 }, latencyMs10 { 0 };   // Record mode on; the chain's latency (0.1 ms)   // pitch classes (bits): MIDI held / learned, and what Pitch may aim for
        std::atomic<float> bkConf { 0.0f }, bkTune { 0.0f }, bkHeard { 0.0f };
    };
    Meters meters;

    static void addTo (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load();
        while (! a.compare_exchange_weak (cur, cur + v)) {}
    }
    static void holdMin (std::atomic<float>& a, float v) noexcept { if (v < a.load()) a.store (v); }
    static void holdMax (std::atomic<float>& a, float v) noexcept { if (v > a.load()) a.store (v); }

    SpectrumTap inputTap, outputTap;
    std::atomic<double> hostBpm { 0.0 };
    AutoEditController autoEdit { parameters, [this] { return hostBpm.load(); } };
    ChainLibrary chains { parameters };   // My Chains + Quick Looks (message thread)
    /** The chain last saved / loaded here (saved with the project). Any thread: the host may save from another one. */
    juce::String getChainName() const { const juce::ScopedLock sl (styleLock); return chainName; }
    void setChainName (const juce::String& n) { const juce::ScopedLock sl (styleLock); chainName = n; }

    /** The suite link (Voxology, Mixology, Masterology): is a Mixology in this project, and what did it answer for
        this vocal (found it and treats it as a vocal, or why not; what it hears there). Message thread. */
    struct MixologyView
    {
        bool here = false;       // a Mixology hand-off is alive in this project
        bool answered = false;   // ... and it answered for this track
        suite::VocalAck ack;
    };
    MixologyView mixologyNow() const { const juce::ScopedLock sl (styleLock); return mixology; }

    // The song's style (Mixology's list of 75, the same in Mixology and Masterology), shared through the suite link.
    // It suggests the vocal STYLE (aeStyle) on a track where you haven't picked one yourself. Message thread.
    /** You picked a song style in Voxology's window: the others follow. */
    void pickStyle (const std::string& key, const std::string& name);
    /** You picked this track's vocal style yourself (the STYLE arrows): the song's style no longer changes it. */
    void setVocalStyleMine (bool mine) { vocalStyleMine.store (mine); if (! mine) suggestVocalStyle(); }
    bool isVocalStyleMine() const { return vocalStyleMine.load(); }
    suite::StyleInfo songStyleNow() const { const juce::ScopedLock sl (styleLock); return songStyle.style(); }
    std::uint32_t songStyleVersion() const { return styleVersion.load(); }

    // The DAW you work in, shared the same way: found from the host, or picked here / in Mixology / in Masterology.
    // Every tip and report line then says how to do things in that DAW. "" = back to the one found.
    void pickDaw (const std::string& key);
    suite::StyleInfo dawNow() const { const juce::ScopedLock sl (styleLock); return daw.style(); }
    std::string dawDetected() const { return detectedDaw; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    template <typename Sample>
    void processAnyPrecision (juce::AudioBuffer<Sample>& buffer, const juce::MidiBuffer& midi, bool hostBypass);
    std::atomic<bool> hostBypassed { false };
    void handleMidi (const juce::MidiBuffer& midi) noexcept;
    std::atomic<float>* midiParam = nullptr;
    std::array<int, 128> midiHeld {};   // per note: how many times held (several channels)
    int midiHeldCount = 0;
    std::atomic<int> midiLearned { 0 };   // Learn: the scale taught by MIDI (saved with the project)

    vox::VocalChain chain, recChain;    // recChain: Record mode (low latency), prepared alongside
    std::atomic<bool> usingRec { false };
    std::atomic<float>* recParam = nullptr;
    std::atomic<int> wantedLatency { 0 };
    vox::VocalChain& active() noexcept { return usingRec.load() ? recChain : chain; }
    vox::VocalChain& standbyChain() noexcept { return usingRec.load() ? chain : recChain; }
    void handleAsyncUpdate() override;
    // Switching Record mode: the other chain is cleaned on the message thread after it was last used (a reset takes
    // milliseconds: too long for the audio thread), hears the vocal for its latency, then crossfades in (20 ms).
    enum : int { standbyClean = 0, standbyDirty = 1, standbyCleaning = 2 };
    std::atomic<int> standby { standbyClean };
    int switchPos = -1;                  // samples into a switch (-1 = none); audio thread
    void cleanStandby() noexcept;
    template <typename Sample>
    void switchChains (juce::AudioBuffer<Sample>& buffer, int nch, int n) noexcept;
    vox::ChainParams chainParams;
    VoxParams::Reader reader;
    vox::LoudnessMeter inMeter, outMeter;
    vox::LevelMatch levelMatch;
    double appliedMatchDb = 0.0;
    std::atomic<float>* levelMatchParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* umAmountParam = nullptr;
    std::atomic<float>* umFocusParam = nullptr;
    std::atomic<float>* keySrcParam = nullptr;
    vox::BeatKey beatKey;              // BEAT mode: the beat's key and tuning
    vox::BeatKey sideKey;              // VOCAL: the beat fed to the side-chain input
    vox::VoiceKey voiceKey;            // VOCAL: the key of what's sung
    bool wasBeat = false;
    int followSource = 0;              // bkSource of the key Pitch follows (0 = none)
    double sideQuietSeconds = 1.0e9;   // how long the side-chain has had no sound
    bool sideChainOn() const noexcept { return getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0; }
    void storeBeatMeters (const vox::BeatKey::Result& r) noexcept;

    // Unmask: every instance has a link slot; VOCAL mode publishes its processed vocal's bands there.
    static constexpr int kHop = 128;
    static constexpr double kBeatKeySure = 0.5;   // confidence needed to start following a key (keep following down to 0.3)
    double preparedRate = 48000.0;
    int linkSlot = -1;
    juce::String unmaskSourceName, pendingSourceName;   // under styleLock (a project can load off the message thread)
    juce::String chainName;                             // under styleLock
    void resolveUnmaskSource();
    void timerCallback() override;
    int timerTicks = 0;

    // The suite link: Mixology learns the track Voxology sits on and what it does there (so it never adds a second
    // compressor or reverb to this vocal). Message thread, a few times a second.
    void publishSuite();
    int suiteSlot = -1;
    std::uint64_t suiteId = 0;
    MixologyView mixology;   // under styleLock
    // The last Auto-Edit's "recorded too hot / clipped" stops going to Mixology once the vocal has come in clean for a
    // while (fixed at the source): seconds of voice with no overs or clipped stretches since that report.
    double cleanVoiceSeconds = 0.0;
    int seenClipRuns = 0, seenOvers = 0;
    float seenRecordedPeak = -1000.0f;
    int seenRecordedSpots = -1;
    mutable juce::CriticalSection styleLock;
    suite::SongStyle songStyle { suite::StyleInfo::From::voxology };   // under styleLock
    std::atomic<std::uint32_t> styleVersion { 0 };
    std::atomic<bool> vocalStyleMine { false };   // saved with the project
    const std::string detectedDaw = suite::dawFromHost (juce::PluginHostType::getHostPath().toStdString(),
                                                        juce::PluginHostType().getHostDescription());
    suite::Presence suitePresence;   // taking part in the suite link (prepareToPlay .. releaseResources)
    void leaveSuite();
    suite::SharedChoice daw { suite::StyleInfo::From::voxology, suite::Choice::daw };   // under styleLock
    void suggestVocalStyle();                     // the song style's vocal style, unless you picked one
    float suiteCompDb = 0.0f;
    juce::String trackName;
    vox::BandAnalyser vocalBands;
    int hopCount = 0;
    vox::Unmask unmask;

    JUCE_DECLARE_WEAK_REFERENCEABLE (VoxologyAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxologyAudioProcessor)
};
