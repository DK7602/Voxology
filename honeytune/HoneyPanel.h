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
    /** Playback position in the clip (seconds, negative = stopped): moves the playhead and the time readout. */
    void setPlayhead (double seconds);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    HoneyRoll roll;

private:
    void applySettings();
    void updateNoteControls();
    void applyNoteEdit();

    juce::Rectangle<float> frameArea() const;
    void drawDrips (juce::Graphics&, juce::Rectangle<float> frame);

    std::unique_ptr<honeytheme::Look> look;
    juce::Rectangle<float> topBar, bottomBar, titleArea, legendArea, logoArea, noteInfoArea, statusArea, readoutArea;
    double lastPosition = 0.0;
    std::vector<juce::Rectangle<float>> cards;
    static constexpr float kFrame = 9.0f, kSeparator = 6.0f, kDripRoom = 0.0f;
    honeyui::Model* model = nullptr;

    juce::ComboBox key, scale;
    juce::Slider snap, drift, vibrato, noteDrift, noteVibrato, noteFormant;
    juce::TextButton snapNote { "Snap note to key" }, resetNote { "Reset note" }, resetAll { "Reset all notes" }, fit { "Fit" }, original { "Original" }, tuned { "Tuned" },
                     undoBtn { "Undo" }, redoBtn { "Redo" };
    juce::Label keyLabel, scaleLabel, snapLabel, driftLabel, vibratoLabel, noteDriftLabel, noteVibratoLabel, noteFormantLabel;
    juce::String status, noteInfo;
    bool statusWarning = false;   // the status line is a warning (red)
};
