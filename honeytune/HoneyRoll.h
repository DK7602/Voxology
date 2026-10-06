#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HoneyView.h"

/** The honeycomb note editor: every note is a gold-rimmed marble cell on a piano roll.
    Off-key notes glow blue; once fixed they fill gold. Drag a cell up / down to move it (whole notes;
    hold Alt for cents), double-click to snap it to the key, Delete to reset it.
    Mouse wheel scrolls up / down, Shift + wheel scrolls in time, Ctrl + wheel (or + / -) zooms. */
class HoneyRoll final : public juce::Component,
                        private juce::ScrollBar::Listener
{
public:
    HoneyRoll();

    void setModel (honeyui::Model* m) { model = m; refresh(); }
    /** Pull the latest notes from the model (keeps the selection and the view). */
    void refresh();

    int getSelected() const { return selected; }
    void setSelected (int index);
    const honeyui::Snapshot& getSnapshot() const { return snap; }
    std::function<void()> onSelectionChanged;
    std::function<void (double)> onSeek;   // clicked the ruler (clip seconds)
    std::function<void (bool)> onUndo;     // Ctrl + Z (true) / Ctrl + Y or Ctrl + Shift + Z (false)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Where playback is in the clip (seconds); negative = stopped. The view follows it. */
    void setPlayhead (double seconds);
    double getPlayhead() const { return playhead; }

    /** Fit the whole clip and its notes into view. */
    void fitAll();

private:
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void updateScrollBars();
    double defaultZoom() const;
    juce::Rectangle<float> gridArea() const;
    float xOf (double seconds) const;
    double secondsAt (float x) const;
    float yOf (double midi) const;
    double midiAt (float y) const;
    juce::Path cellPath (int index, double midi) const;
    int noteAt (juce::Point<float>) const;
    void drawBackground (juce::Graphics&, juce::Rectangle<float> grid);
    void drawCell (juce::Graphics&, int index);
    void drawKeyboard (juce::Graphics&);
    void drawRuler (juce::Graphics&);

    honeyui::Model* model = nullptr;
    honeyui::Snapshot snap;
    bool fitted = false;
    float lastGridHeight = 0.0f;
    bool userZoomed = false;
    double playhead = -1.0;
    void drawPlayhead (juce::Graphics&);

    double viewStart = 0.0, pixelsPerSecond = 40.0;   // time axis
    double topMidi = 72.0, rowHeight = 18.0;          // pitch axis (topMidi = the row at the top edge)

    int selected = -1, hovered = -1;
    bool dragging = false;
    double dragStartTarget = 0.0, dragTarget = 0.0;
    float dragStartY = 0.0f, dragStartX = 0.0f;
    // What a drag does: up / down = pitch; sideways = move in time (decided by the first 6 px);
    // grabbing a note's left / right end stretches it from that end.
    enum class Drag { undecided, pitch, time, leftEdge, rightEdge } dragMode = Drag::undecided;
    double dragSeconds = 0.0;   // how far the time / edge has moved
    /** Where a note is shown in time (seconds): as sung in Original, else as it will sound (with the drag). */
    std::pair<double, double> span (int index) const;
    /** A moment of the note as sung (seconds) -> where it's shown. */
    double shownTime (int index, double sungSeconds) const;
    bool seeking = false;

    juce::ScrollBar hBar { false }, vBar { true };
    juce::Image honeycomb;   // the faint background pattern (cached per size)

    static constexpr int kKeysWidth = 46, kRulerHeight = 20, kBar = 12;
};
