#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "vox/LevelMatch.h"
#include "vox/LoudnessMeter.h"
#include "vox/VocalChain.h"
#include "AutoEditController.h"
#include "Params.h"
#include "SpectrumTap.h"

#include <atomic>

/** Voxology: an all-in-one vocal chain (Cleanup -> Tone EQ -> De-Esser -> Rider -> Compressor ->
    Saturation -> Doubler -> Delay -> Reverb -> Output) with Auto-Edit, which listens to the vocal,
    sets every module and explains why. */
class VoxologyAudioProcessor final : public juce::AudioProcessor
{
public:
    VoxologyAudioProcessor();
    ~VoxologyAudioProcessor() override = default;

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
        std::atomic<float> peakGr { 0.0f }, levelGr { 0.0f };             // dB, held until read
        std::atomic<float> satResidual { 0.0f }, satSignal { 0.0f };      // energies, drained by the editor
        std::atomic<float> matchDb { 0.0f };
        std::atomic<float> bpm { 0.0f };
        std::atomic<float> pitchSung { 0.0f };      // fractional MIDI note heard, 0 = none
        std::atomic<int> pitchTarget { -1 };        // the note it pulls to, -1 = none
        std::atomic<float> pitchCorr { 0.0f };      // semitones applied
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxologyAudioProcessor)
};
