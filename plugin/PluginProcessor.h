#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "vox/LevelMatch.h"
#include "vox/LoudnessMeter.h"
#include "vox/Unmask.h"
#include "vox/VocalChain.h"
#include "AutoEditController.h"
#include "Params.h"
#include "SpectrumTap.h"

#include <atomic>

/** Voxology: an all-in-one vocal chain (Cleanup -> Tone EQ -> De-Esser -> Rider -> Compressor ->
    Saturation -> Doubler -> Delay -> Reverb -> Output) with Auto-Edit, which listens to the vocal,
    sets every module and explains why. */
class VoxologyAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    VoxologyAudioProcessor();
    ~VoxologyAudioProcessor() override;
    void updateTrackProperties (const TrackProperties& properties) override;

    /** Unmask (BEAT mode): which vocal to make room for. -1 = every Voxology vocal in the project. */
    std::atomic<int> unmaskSource { -1 };
    int getLinkSlot() const noexcept { return linkSlot; }
    void setUnmaskSourceByName (const juce::String& name);   // "" = every vocal (message thread)
    juce::String getUnmaskSourceName() const { return unmaskSourceName; }
    bool isBeatMode() const noexcept { return modeParam != nullptr && modeParam->load() > 0.5f; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }   // delay + reverb tails

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    struct Meters
    {
        std::atomic<float> inShort { -100.0f }, outShort { -100.0f };     // LUFS
        std::atomic<float> inPeak { -100.0f }, outPeak { -100.0f };       // dBFS, held until read
        std::atomic<float> gate { 0.0f }, deEss { 0.0f }, rider { 0.0f };  // dB
        std::atomic<float> pops { 0.0f }, breath { 0.0f };                 // dB, held until read
        std::array<std::atomic<float>, vox::kDynBands> dynEq {};            // dB per band, held until read
        std::atomic<float> peakGr { 0.0f }, levelGr { 0.0f };             // dB, held until read
        std::atomic<float> satResidual { 0.0f }, satSignal { 0.0f };      // energies, drained by the editor
        std::atomic<float> matchDb { 0.0f };
        std::atomic<float> bpm { 0.0f };
        std::atomic<float> pitchSung { 0.0f };      // fractional MIDI note heard, 0 = none
        std::atomic<int> pitchTarget { -1 };        // the note it pulls to, -1 = none
        std::atomic<float> pitchCorr { 0.0f };      // semitones applied
        std::array<std::atomic<float>, vox::kUnmaskBands> umDip {};     // BEAT: deepest dip per band, dB, held until read
        std::array<std::atomic<float>, vox::kUnmaskBands> umVocal {};   // BEAT: the linked vocal's band levels, dB
        std::atomic<int> umLink { 0 };              // BEAT: vocals heard this block (0 = none)
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

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    template <typename Sample>
    void processAnyPrecision (juce::AudioBuffer<Sample>& buffer);

    vox::VocalChain chain;
    vox::ChainParams chainParams;
    VoxParams::Reader reader;
    vox::LoudnessMeter inMeter, outMeter;
    vox::LevelMatch levelMatch;
    double appliedMatchDb = 0.0;
    std::atomic<float>* levelMatchParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* umAmountParam = nullptr;
    std::atomic<float>* umFocusParam = nullptr;

    // Unmask: every instance has a link slot; VOCAL mode publishes its processed vocal's bands there.
    static constexpr int kHop = 128;
    double preparedRate = 48000.0;
    int linkSlot = -1;
    juce::String unmaskSourceName, pendingSourceName;   // message thread
    void resolveUnmaskSource();
    void timerCallback() override;
    vox::BandAnalyser vocalBands;
    int hopCount = 0;
    vox::Unmask unmask;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxologyAudioProcessor)
};
