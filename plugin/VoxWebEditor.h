#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

/** The Voxology interface: HTML/CSS/JS (plugin/ui) embedded in the binary and shown in the
    platform web view (WebView2 on Windows, WKWebView on macOS).

    C++ -> JS: parameters through JUCE web relays, plus a "voxMeters" event at 30 Hz (levels,
               module activity, two spectra, Auto-Edit state).
    JS -> C++: parameters through the relays; native functions startAutoEdit, cancelAutoEdit,
               undoAutoEdit, getAutoEditReport. */
class VoxWebEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit VoxWebEditor (VoxologyAudioProcessor&);
    ~VoxWebEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static bool isSupported();

    static constexpr int kDesignWidth = 1600;
    static constexpr int kDesignHeight = 900;

private:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kSpectrumPoints = 96;

    struct Analyser
    {
        std::array<float, kSpectrumPoints> smoothed {};
        bool primed = false;
    };

    template <typename Relay>
    static std::vector<std::unique_ptr<Relay>> makeRelays (const juce::StringArray& ids)
    {
        std::vector<std::unique_ptr<Relay>> relays;
        for (const auto& id : ids)
            relays.push_back (std::make_unique<Relay> (id));
        return relays;
    }

    void timerCallback() override;
    juce::var makeSpectrum (const SpectrumTap& tap, Analyser& state);
    static juce::WebBrowserComponent::Options makeBaseOptions();
    juce::WebBrowserComponent::Options makeEditorOptions();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    VoxologyAudioProcessor& audioProcessor;

    // Relays must exist before the browser, which takes its options from them.
    std::vector<std::unique_ptr<juce::WebSliderRelay>> sliderRelays = makeRelays<juce::WebSliderRelay> (VoxParams::sliderIds());
    std::vector<std::unique_ptr<juce::WebToggleButtonRelay>> toggleRelays = makeRelays<juce::WebToggleButtonRelay> (VoxParams::toggleIds());
    std::vector<std::unique_ptr<juce::WebComboBoxRelay>> comboRelays = makeRelays<juce::WebComboBoxRelay> (VoxParams::comboIds());

    juce::WebBrowserComponent webView;

    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> toggleAttachments;
    std::vector<std::unique_ptr<juce::WebComboBoxParameterAttachment>> comboAttachments;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { static_cast<size_t> (kFftSize), juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, 2 * kFftSize> fftBuffer {};
    Analyser inAnalyser, outAnalyser;
    float gateHold = 0.0f, essHold = 0.0f, peakHold = 0.0f, levelHold = 0.0f, inPkHold = -100.0f, outPkHold = -100.0f;
    double satResHold = 0.0, satSigHold = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxWebEditor)
};
