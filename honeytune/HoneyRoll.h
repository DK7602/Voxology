#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HoneyView.h"

/** The honeycomb note editor: every note is a gold-rimmed light blue marble shape on a piano roll, the outline of its
    sung waveform (how loud it was, moment to moment). Notes that will play off the key glow red; pulled into the key
    they fill gold. Drag a cell up / down to move it (whole notes;
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
    std::function<void()> onWantTuned;     // an edit while the Original shows: switch to Tuned first
    std::function<void()> onToggleOriginal;   // B
    std::function<void (double)> onPlayFrom;  // play from there (clip seconds; < 0 = from the playhead, or stop if playing)
    std::function<void (double, double)> onLoop;   // loop and play this stretch (clip seconds)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Where the host's playhead is in the clip (seconds; negative = not in this clip). It stays drawn when playback stops
        (where it stopped, or wherever the host's cursor is then). The view follows it while playing; when stopped it
        scrolls to it only when it moves out of view (you can look around freely). */
    void setPlayhead (double seconds, bool isPlaying);
    double getPlayhead() const { return playhead; }
    bool isPlaying() const { return playing; }

    /** The working view (false: the first seconds of singing, close up) or the whole clip and every note (true).
        False (nothing changed) while the roll is too small. */
    bool fitAll (bool whole = false);
    /** The Fit button / F: the whole clip; again: back to the working view. */
    void fitButton();
    /** Another clip: fit it afresh (no selection). */
    void resetView();

private:
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void updateScrollBars();
    double defaultZoom() const;
    juce::Rectangle<float> gridArea() const;
    float xOf (double seconds) const;
    double secondsAt (float x) const;
    float yOf (double midi) const;
    double midiAt (float y) const;
    juce::Path cellPath (int index, double midi) const;   // where a note can be grabbed (a hexagon, a row high)
    double shownPitch (int index) const;                   // where it's drawn (Original: as sung; with a drag)
    double clampTop (double top) const;
    enum class Command { snapToKey, steady, resetPitch, resetTiming, resetTone, resetSteady, resetAll, loop };
    void command (int index, Command);
    void apply (int index, const honeyui::NoteEdit&);
    void showMenu (int hit, juce::Point<float> at);
    /** The note as drawn: its sung waveform around its pitch, one pixel column at a time: the outline = its peaks
        (Track::level), the body = its RMS (Track::power), in dB; as tall as it was loud (the clip's loud singing = 1.7 rows). */
    struct Wave { juce::Path outline, body; };
    Wave wavePaths (int index, double midi) const;
    int noteAt (juce::Point<float>) const;
    void drawBackground (juce::Graphics&, juce::Rectangle<float> grid);
    void drawCell (juce::Graphics&, int index);
    void drawKeyboard (juce::Graphics&);
    void drawPitchLine (juce::Graphics&, juce::Rectangle<float> visible);
    void drawName (juce::Graphics&, int index);
    void drawRuler (juce::Graphics&);

    honeyui::Model* model = nullptr;
    honeyui::Snapshot snap;
    bool fitted = false;
    float lastGridHeight = 0.0f;
    bool userZoomed = false, wholeView = false;
    bool following = true;   // the view pages along with the playhead (paused when you look elsewhere while it plays)
    double playhead = -1.0;
    bool playing = false;
    void drawPlayhead (juce::Graphics&);

    double viewStart = 0.0, pixelsPerSecond = 40.0;   // time axis
    double topMidi = 72.0, rowHeight = 18.0;          // pitch axis (topMidi = the row at the top edge)

    int selected = -1, hovered = -1;
    bool dragging = false;
    double dragStartTarget = 0.0, dragTarget = 0.0;
    float dragStartY = 0.0f, dragStartX = 0.0f;
    // What a drag does: up / down = pitch; sideways = move in time (decided by the first 6 px);
    // grabbing a note's left / right end stretches it from that end.
    enum class Drag { undecided, pitch, time, leftEdge, rightEdge } dragMode = Drag::undecided, pendingEdge = Drag::undecided;
    double dragSeconds = 0.0;   // how far the time / edge has moved
    /** Where a note is shown in time (seconds): as sung in Original, else as it will sound (with the drag). */
    std::pair<double, double> span (int index) const;
    /** A moment of the note as sung (seconds) -> where it's shown. */
    double shownTime (int index, double sungSeconds) const;
    bool seeking = false;

    juce::ScrollBar hBar { false }, vBar { true };
    juce::Image honeycomb;   // the faint background pattern (cached per size)

    static constexpr int kKeysWidth = 46, kRulerHeight = 20, kBar = 12;
    static constexpr double kMinRow = 8.0, kMaxRow = 60.0, kMaxPixelsPerSecond = 2000.0;
    static constexpr float kWaveRangeDb = 36.0f;   // a note's waveform: the clip's loud singing = full height, this far under = flat
};
