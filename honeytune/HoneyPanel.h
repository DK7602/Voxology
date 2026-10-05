#pragma once

#include "HoneyRoll.h"
#include "HoneyTheme.h"

/** The whole Honey Tune window: key / scale / clip-wide amounts on top, the honeycomb roll in the
    middle, the selected note's controls at the bottom. */
class HoneyPanel final : public juce::Component
{
public:
    HoneyPanel();
    ~HoneyPanel() override;

    void setModel (honeyui::Model* m);
    /** Call when the clip's notes or render changed (message thread). */
    void refresh();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    HoneyRoll roll;

private:
    void applySettings();
    void updateNoteControls();
    void applyNoteEdit();

    juce::Rectangle<float> frameArea() const;

    std::unique_ptr<honeytheme::Look> look;
    juce::Rectangle<float> topBar, bottomBar, legendArea, logoArea, noteInfoArea, statusArea;
    std::vector<juce::Rectangle<float>> cards;
    static constexpr float kFrame = 9.0f, kSeparator = 6.0f, kDripRoom = 24.0f;
    honeyui::Model* model = nullptr;

    juce::ComboBox key, scale;
    juce::Slider snap, drift, vibrato, noteDrift, noteVibrato;
    juce::TextButton snapNote { "Snap note to key" }, resetNote { "Reset note" }, resetAll { "Reset all notes" }, fit { "Fit" };
    juce::Label keyLabel, scaleLabel, snapLabel, driftLabel, vibratoLabel, noteDriftLabel, noteVibratoLabel;
    juce::String status, noteInfo;
};
