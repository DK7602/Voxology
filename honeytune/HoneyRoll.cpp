#include "HoneyRoll.h"

#include "HoneyTheme.h"

using namespace juce;

namespace
{
    using honeytheme::ink; using honeytheme::navy; using honeytheme::deepBlue;
    using honeytheme::gold; using honeytheme::goldDeep;
    const Colour glowBlue = honeytheme::glow;

    constexpr double kLowestMidi = 24.0, kHighestMidi = 108.0;

    bool inScale (int midi, int key, int scale)
    {
        return vox::kScaleMasks[static_cast<size_t> (scale)][static_cast<size_t> (((midi - key) % 12 + 12) % 12)];
    }

    String noteName (double midi)
    {
        const int m = roundToInt (midi);
        return String (vox::kNoteNames[static_cast<size_t> ((m % 12 + 12) % 12)]) + String (m / 12 - 1);
    }
}

HoneyRoll::HoneyRoll()
{
    setWantsKeyboardFocus (true);
    for (auto* b : { &hBar, &vBar })
    {
        b->addListener (this);
        b->setAutoHide (false);
        addAndMakeVisible (*b);
    }
}

//==============================================================================
void HoneyRoll::refresh()
{
    if (model == nullptr) return;
    const auto before = snap.notes.size();
    snap = model->snapshot();
    if (snap.notes.size() != before)
    {
        // Other notes (a new analysis, a split / join, its undo): the old note numbers mean nothing now.
        chosen.assign (snap.notes.size(), 0);
        selected = -1;
        splitHover = hovered = -1;
    }
    if (selected >= static_cast<int> (snap.notes.size())) selected = -1;
    chosen.resize (snap.notes.size(), 0);
    if (selected >= 0 && ! chosen[static_cast<size_t> (selected)]) chosen[static_cast<size_t> (selected)] = 1;
    if (! fitted && snap.status == 2 && ! snap.notes.empty())
        fitted = fitAll();   // false while the roll is still too small to fit into: resized() tries again
    updateScrollBars();
    repaint();
}

void HoneyRoll::setSelected (int index)
{
    const bool single = index < 0 ? selectionSize() == 0 : selectionSize() == 1 && isSelected (index);
    if (index == selected && single) return;
    chosen.assign (snap.notes.size(), 0);
    if (index >= 0 && index < static_cast<int> (chosen.size())) chosen[static_cast<size_t> (index)] = 1;
    selected = index;
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

std::vector<int> HoneyRoll::getSelection() const
{
    std::vector<int> out;
    for (size_t i = 0; i < chosen.size() && i < snap.notes.size(); ++i)
        if (chosen[i]) out.push_back (static_cast<int> (i));
    return out;
}

int HoneyRoll::selectionSize() const
{
    int count = 0;
    for (size_t i = 0; i < chosen.size() && i < snap.notes.size(); ++i) count += chosen[i] ? 1 : 0;
    return count;
}

bool HoneyRoll::isSelected (int i) const
{
    return i >= 0 && static_cast<size_t> (i) < chosen.size() && chosen[static_cast<size_t> (i)] != 0;
}

void HoneyRoll::selectMany (std::vector<char> pick, int primary)
{
    pick.resize (snap.notes.size(), 0);
    chosen = std::move (pick);
    selected = isSelected (primary) ? primary : -1;
    if (selected < 0)
        for (size_t i = 0; i < chosen.size(); ++i) if (chosen[i]) { selected = static_cast<int> (i); break; }
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

bool HoneyRoll::fitAll (bool whole)
{
    userZoomed = whole;
    wholeView = whole;
    // The working view (on opening): the first ~6 seconds of singing (notes big enough to grab), the clip's main range
    // in height (the middle 90 % of its notes, so one stray high note doesn't shrink everything). The whole clip (Fit):
    // all of it in time, every note in height.
    const auto grid = gridArea();
    if (grid.getWidth() < 10.0f || grid.getHeight() < 40.0f || snap.seconds <= 0.0) return false;
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    const double firstNote = snap.notes.empty() ? 0.0 : snap.notes.front().note.start / sr;
    if (whole)
    {
        pixelsPerSecond = grid.getWidth() / std::max (0.5, snap.seconds * 1.02);
        viewStart = 0.0;
    }
    else
    {
        pixelsPerSecond = defaultZoom();
        // Around the playhead when it's in this clip (where you are), else the first note.
        viewStart = playhead >= 0.0 ? std::max (0.0, playhead - 1.0) : std::max (0.0, firstNote - 0.5);
    }
    std::vector<double> pitches;
    for (const auto& n : snap.notes) { pitches.push_back (n.note.pitch); pitches.push_back (n.note.target); }
    double lo = 48.0, hi = 72.0;
    if (! pitches.empty())
    {
        std::sort (pitches.begin(), pitches.end());
        const size_t trim = whole ? 0 : pitches.size() / 20;
        lo = pitches[trim];
        hi = pitches[pitches.size() - 1 - trim];
    }
    lo -= 2.0; hi += 2.0;
    rowHeight = jlimit (whole ? kMinRow : 16.0, 30.0, grid.getHeight() / (hi - lo + 1.0));
    const double rows = grid.getHeight() / rowHeight;
    // Centre on where most of the singing is (each note weighted by its length); the whole range when it all shows.
    double centre = (hi + lo) / 2.0;
    if (! whole)
    {
        std::vector<std::pair<double, double>> w;
        double total = 0.0;
        for (const auto& n : snap.notes) { w.push_back ({ n.note.target, n.note.end - n.note.start }); total += n.note.end - n.note.start; }
        std::sort (w.begin(), w.end());
        double acc = 0.0;
        for (const auto& [p, d] : w) { acc += d; if (acc >= total / 2.0) { centre = p; break; } }
    }
    topMidi = clampTop (centre + rows / 2.0);
    updateScrollBars();
    repaint();
    return true;
}

void HoneyRoll::fitButton()
{
    // Fit: the whole clip; again: back to the working view (around the playhead).
    fitAll (! wholeView);
    following = true;
}

void HoneyRoll::resetView()
{
    fitted = false;
    userZoomed = wholeView = false;
    selected = hovered = -1;
    following = true;
    refresh();
}

double HoneyRoll::clampTop (double top) const
{
    const double rows = gridArea().getHeight() / rowHeight;
    return jlimit (std::min (kLowestMidi + rows, kHighestMidi), kHighestMidi, top);
}

void HoneyRoll::setPlayhead (double seconds, bool isPlaying)
{
    const bool moved = std::abs (seconds - playhead) >= 1.0e-4;
    if (! moved && isPlaying == playing) return;
    const auto grid = gridArea();
    auto area = [&] (double t) { return Rectangle<float> (xOf (t) - 12.0f, 0.0f, 24.0f, (float) getHeight()); };
    if (playhead >= 0.0) repaint (area (playhead).toNearestInt());
    if (isPlaying && ! playing) following = true;   // it starts playing: follow it again
    playhead = seconds;
    playing = isPlaying;
    if (playhead < 0.0) return;
    // Keep it in view: page along when it reaches the right edge (or is out of view), unless you're dragging a note or
    // looked elsewhere while it plays (scrolled or zoomed: until it plays again, or you click the ruler / press F).
    // Stopped, it pages only when it moved there (the host's cursor), so you can scroll around a stopped song.
    const double visible = grid.getWidth() / pixelsPerSecond;
    if (! dragging && moved && (! playing || following) && (playhead > viewStart + visible * 0.92 || playhead < viewStart))
    {
        viewStart = std::max (0.0, playhead - visible * 0.08);
        updateScrollBars();
        repaint();
        return;
    }
    repaint (area (playhead).toNearestInt());
}

void HoneyRoll::drawPlayhead (Graphics& g)
{
    if (playhead < 0.0) return;
    const auto grid = gridArea();
    const float x = xOf (playhead);
    if (x < grid.getX() || x > grid.getRight()) return;
    // A bright line with a blue glow, and a marker on the ruler (stopped: the same line, a little quieter).
    g.setColour (glowBlue.withAlpha (playing ? 0.35f : 0.22f));
    g.fillRect (x - 3.0f, grid.getY(), 6.0f, grid.getHeight());
    g.setColour (Colours::white.withAlpha (0.95f));
    g.fillRect (x - 0.75f, grid.getY(), 1.5f, grid.getHeight());
    Path tri;
    tri.addTriangle (x - 6.0f, 2.0f, x + 6.0f, 2.0f, x, (float) kRulerHeight - 2.0f);
    g.setColour (Colours::white);
    g.fillPath (tri);
    g.setColour (ink);
    g.strokePath (tri, PathStrokeType (1.0f));
}

double HoneyRoll::defaultZoom() const
{
    // About 6 seconds across, never so far out that sung notes turn into slivers.
    return std::max (gridArea().getWidth() / std::min (std::max (snap.seconds, 1.0), 6.0), 120.0);
}

//==============================================================================
Rectangle<float> HoneyRoll::gridArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (kKeysWidth).withTrimmedTop (kRulerHeight)
                           .withTrimmedRight (kBar).withTrimmedBottom (kBar);
}

float HoneyRoll::xOf (double s) const   { return gridArea().getX() + static_cast<float> ((s - viewStart) * pixelsPerSecond); }
double HoneyRoll::secondsAt (float x) const { return viewStart + (x - gridArea().getX()) / pixelsPerSecond; }
float HoneyRoll::yOf (double m) const   { return gridArea().getY() + static_cast<float> ((topMidi - m) * rowHeight); }
double HoneyRoll::midiAt (float y) const { return topMidi - (y - gridArea().getY()) / rowHeight; }

void HoneyRoll::updateScrollBars()
{
    const auto grid = gridArea();
    const double total = std::max (snap.seconds, 1.0);
    hBar.setRangeLimits (0.0, total, dontSendNotification);
    hBar.setCurrentRange (viewStart, grid.getWidth() / pixelsPerSecond, dontSendNotification);
    // The vertical bar runs top (high notes) to bottom: position = kHighestMidi - topMidi.
    vBar.setRangeLimits (0.0, kHighestMidi - kLowestMidi, dontSendNotification);
    vBar.setCurrentRange (kHighestMidi - topMidi, grid.getHeight() / rowHeight, dontSendNotification);
}

void HoneyRoll::scrollBarMoved (ScrollBar* bar, double start)
{
    if (bar == &hBar) { viewStart = start; if (playing) following = false; }   // looking elsewhere: stop paging along
    else topMidi = kHighestMidi - start;
    repaint();
}

void HoneyRoll::resized()
{
    const auto b = getLocalBounds();
    hBar.setBounds (b.getX() + kKeysWidth, b.getBottom() - kBar, b.getWidth() - kKeysWidth - kBar, kBar);
    vBar.setBounds (b.getRight() - kBar, b.getY() + kRulerHeight, kBar, b.getHeight() - kRulerHeight - kBar);
    honeycomb = {};
    bool justFitted = false;
    if (! fitted && snap.status == 2 && ! snap.notes.empty())
        fitted = justFitted = fitAll();   // hosts open the editor small, then enlarge it: fit once there's room
    else if (wholeView)
        fitAll (true);
    else if (! userZoomed)
        pixelsPerSecond = defaultZoom();
    if (fitted && ! justFitted && lastGridHeight > 0.0f && gridArea().getHeight() > 0.0f)
    {
        // Keep the same pitch in the middle when the window changes size (hosts resize the editor).
        const double rowsBefore = lastGridHeight / rowHeight, rowsNow = gridArea().getHeight() / rowHeight;
        topMidi = clampTop (topMidi - rowsBefore / 2.0 + rowsNow / 2.0);
    }
    lastGridHeight = gridArea().getHeight();
    updateScrollBars();
}

//==============================================================================
std::pair<double, double> HoneyRoll::span (int index) const
{
    const auto& n = snap.notes[static_cast<size_t> (index)].note;
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    if (model != nullptr && model->isOriginal()) return { n.start / sr, n.end / sr };
    double a = n.soundStart() / sr, b = n.soundEnd() / sr;
    if (dragging && dragMode == Drag::time && isSelected (index))
    {
        // The selection moves together, as far as the nearest note that isn't selected lets every one of them.
        const double d = std::clamp (dragSeconds, groupMin, groupMax);
        return { a + d, b + d };
    }
    if (dragging && index == selected && dragMode != Drag::pitch && dragMode != Drag::undecided)
    {
        // Between the neighbours as they sound (a note can't be moved into another) and inside the clip.
        const auto count = static_cast<int> (snap.notes.size());
        const double gap = honeyui::kTimeGapSeconds, minLen = honeyui::kMinNoteSeconds;
        const double lo = index > 0 ? snap.notes[static_cast<size_t> (index - 1)].note.soundEnd() / sr + gap : 0.0;
        const double hi = std::max (lo + minLen, index + 1 < count ? snap.notes[static_cast<size_t> (index + 1)].note.soundStart() / sr - gap
                                                                   : snap.seconds - gap);
        if (dragMode == Drag::time)
        {
            const double d = std::clamp (dragSeconds, std::min (0.0, lo - a), std::max (0.0, hi - b));
            a += d; b += d;
        }
        if (dragMode == Drag::leftEdge) a = std::clamp (a + dragSeconds, std::min (a, lo), b - minLen);
        if (dragMode == Drag::rightEdge) b = std::clamp (b + dragSeconds, a + minLen, std::max (b, hi));
    }
    return { a, b };
}

double HoneyRoll::shownTime (int index, double sung) const
{
    const auto& n = snap.notes[static_cast<size_t> (index)].note;
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    const auto [a, b] = span (index);
    const double s0 = n.start / sr, s1 = n.end / sr;
    return a + (sung - s0) * (b - a) / std::max (1.0e-6, s1 - s0);
}

double HoneyRoll::sungTime (int index, double shown) const
{
    const auto& n = snap.notes[static_cast<size_t> (index)].note;
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    const auto [a, b] = span (index);
    const double s0 = n.start / sr, s1 = n.end / sr;
    return s0 + (shown - a) * (s1 - s0) / std::max (1.0e-6, b - a);
}

bool HoneyRoll::splitAt (int index, double shownSeconds)
{
    if (model == nullptr || index < 0 || index >= static_cast<int> (snap.notes.size())) return false;
    if (model->isOriginal() && onWantTuned) onWantTuned();
    if (! model->splitNote (index, sungTime (index, shownSeconds))) return false;
    refresh();
    setSelected (index);   // the first part (the second is index + 1)
    return true;
}

std::pair<int, int> HoneyRoll::joinRange (int index) const
{
    const auto count = static_cast<int> (snap.notes.size());
    if (index < 0 || index >= count) return { index, index };
    if (isSelected (index) && selectionSize() > 1)
    {
        // Selected neighbours (in one run, nothing unselected between them) into one.
        const auto sel = getSelection();
        for (size_t k = 1; k < sel.size(); ++k)
            if (sel[k] != sel[k - 1] + 1) return { index, index };
        return { sel.front(), sel.back() };
    }
    if (index + 1 < count) return { index, index + 1 };
    return { index - 1, index };   // the last note: with the one before
}

bool HoneyRoll::join (int index)
{
    const auto [first, last] = joinRange (index);
    if (model == nullptr || last <= first || first < 0) return false;
    if (model->isOriginal() && onWantTuned) onWantTuned();
    if (! model->joinNotes (first, last)) return false;
    refresh();
    setSelected (first);
    return true;
}

void HoneyRoll::updateSplitHover (Point<float> at, bool alt)
{
    int hit = -1;
    if (alt && at.y >= (float) kRulerHeight)
        hit = noteAt (at);
    const float x = at.x;
    if (hit == splitHover && (hit < 0 || std::abs (x - splitX) < 0.5f)) return;
    splitHover = hit;
    splitX = x;
    if (hit >= 0) setMouseCursor (MouseCursor::IBeamCursor);
    repaint();
}

void HoneyRoll::modifierKeysChanged (const ModifierKeys& mods)
{
    if (! isMouseOver()) { if (splitHover >= 0) { splitHover = -1; repaint(); } return; }
    const bool was = splitHover >= 0;
    updateSplitHover (getMouseXYRelative().toFloat(), mods.isAltDown());
    if (was && splitHover < 0) setMouseCursor (MouseCursor::NormalCursor);   // (the note's own grip again on the next move)
}

Path HoneyRoll::cellPath (int index, double midi) const
{
    const auto [t0, t1] = span (index);
    const float x0 = xOf (t0), x1 = std::max (x0 + 12.0f, xOf (t1));
    const float yc = yOf (midi), h = static_cast<float> (rowHeight) * 0.92f;
    const float tip = std::min (h * 0.5f, (x1 - x0) * 0.35f);   // the pointed ends of the hexagon
    Path p;
    p.startNewSubPath (x0, yc);
    p.lineTo (x0 + tip, yc - h / 2);
    p.lineTo (x1 - tip, yc - h / 2);
    p.lineTo (x1, yc);
    p.lineTo (x1 - tip, yc + h / 2);
    p.lineTo (x0 + tip, yc + h / 2);
    p.closeSubPath();
    return p;
}

HoneyRoll::Wave HoneyRoll::wavePaths (int index, double midi) const
{
    const auto [t0, t1] = span (index);
    const float x0 = xOf (t0), x1 = std::max (x0 + 12.0f, xOf (t1));
    const float yc = yOf (midi), row = static_cast<float> (rowHeight);
    const auto& n = snap.notes[static_cast<size_t> (index)].note;
    const auto* track = snap.track.get();
    const double sr = track != nullptr ? track->sampleRate : 48000.0;
    // The moments of the note as sung (shown stretched / moved when it was), one pixel column at a time: its loudest
    // sample (the outline) and its RMS (the body), against the clip's loud singing, mirrored around the pitch.
    const double s0 = n.start / sr, s1 = n.end / sr;
    const int steps = std::clamp (static_cast<int> (std::ceil (x1 - x0)), 4, 8000);
    const float top = track != nullptr && track->levelTop > 0.0f ? track->levelTop : 0.0f;
    std::vector<float> peak (static_cast<size_t> (steps + 1), 0.6f), body (static_cast<size_t> (steps + 1), 0.4f);
    if (top > 0.0f)
        for (int k = 0; k <= steps; ++k)
        {
            const double a = std::max (s0, s0 + (s1 - s0) * (k - 0.5) / steps), b = std::min (s1, s0 + (s1 - s0) * (k + 0.5) / steps);
            // In dB (like a DAW's logarithmic waveform): a quiet verse still shows its shape, a 6 dB swell shows plainly.
            const auto shape = [top] (float v) { return std::clamp (1.0f + 20.0f * std::log10 (std::max (v, 1.0e-6f) / top) / kWaveRangeDb, 0.0f, 1.0f); };
            peak[static_cast<size_t> (k)] = shape (vox::honey::levelOver (*track, a, b));
            body[static_cast<size_t> (k)] = shape (vox::honey::rmsOver (*track, a, b));
        }
    const float minHalf = std::max (1.5f, row * 0.08f), maxHalf = row * 0.85f;
    const float tip = std::min ({ 6.0f, row * 0.25f, (x1 - x0) * 0.2f });   // closed, lightly pointed ends
    auto at = [&] (int k, const std::vector<float>& h)
    {
        const float x = x0 + (x1 - x0) * static_cast<float> (k) / static_cast<float> (steps);
        const float taper = std::sqrt (std::clamp (std::min (x - x0, x1 - x) / std::max (1.0f, tip), 0.0f, 1.0f));
        return std::pair { x, taper * (minHalf + (maxHalf - minHalf) * h[static_cast<size_t> (k)]) };
    };
    auto mirrored = [&] (const std::vector<float>& h)
    {
        Path p;
        p.startNewSubPath (x0, yc);
        for (int k = 1; k < steps; ++k) { const auto [x, y] = at (k, h); p.lineTo (x, yc - y); }
        p.lineTo (x1, yc);
        for (int k = steps - 1; k >= 1; --k) { const auto [x, y] = at (k, h); p.lineTo (x, yc + y); }
        p.closeSubPath();
        return p;
    };
    return { mirrored (peak), mirrored (body) };
}

double HoneyRoll::shownPitch (int i) const
{
    const auto& v = snap.notes[static_cast<size_t> (i)];
    if (model != nullptr && model->isOriginal()) return v.note.pitch;   // Original: where it was sung
    if (dragging && dragMode == Drag::pitch && isSelected (i))
        return i == selected ? dragTarget : jlimit (kLowestMidi, kHighestMidi, v.note.target + (dragTarget - dragStartTarget));
    return v.note.target;
}

int HoneyRoll::noteAt (Point<float> pt) const
{
    for (int i = static_cast<int> (snap.notes.size()); --i >= 0;)
    {
        if (cellPath (i, shownPitch (i)).contains (pt)) return i;
    }
    return -1;
}

//==============================================================================
void HoneyRoll::paint (Graphics& g)
{
    const auto grid = gridArea();
    g.fillAll (Colour (0xfff6f0e2));
    drawBackground (g, grid);

    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (grid.toNearestInt());
        const auto vis = g.getClipBounds().toFloat();
        for (int i = 0; i < static_cast<int> (snap.notes.size()); ++i)
        {
            const auto [t0, t1] = span (i);
            if (xOf (t1) < vis.getX() || xOf (t0) > vis.getRight()) continue;
            if (! isSelected (i)) drawCell (g, i);
        }
        for (int i : getSelection())   // on top
        {
            const auto [t0, t1] = span (i);
            if (xOf (t1) >= vis.getX() && xOf (t0) <= vis.getRight()) drawCell (g, i);
        }
        drawPitchLine (g, vis);
        if (banding)
        {
            const auto band = Rectangle<float> (bandFrom, bandTo);
            g.setColour (Colour (0xff8cc3eb).withAlpha (0.18f));
            g.fillRect (band);
            g.setColour (goldDeep.withAlpha (0.85f));
            g.drawRect (band, 1.2f);
        }
        for (int i = 0; i < static_cast<int> (snap.notes.size()); ++i)
        {
            const auto [t0, t1] = span (i);
            if (xOf (t1) >= vis.getX() && xOf (t0) <= vis.getRight()) drawName (g, i);
        }
        if (splitHover >= 0 && splitHover < static_cast<int> (snap.notes.size()) && ! dragging)
        {
            // Alt over a note: where a double-click splits it.
            const auto cell = cellPath (splitHover, shownPitch (splitHover)).getBounds();
            const float x = jlimit (cell.getX() + 1.0f, cell.getRight() - 1.0f, splitX);
            g.setColour (Colours::white.withAlpha (0.8f));
            g.fillRect (x - 1.5f, cell.getY() - 3.0f, 3.0f, cell.getHeight() + 6.0f);
            g.setColour (Colour (0xff1a1206));
            g.fillRect (x - 0.75f, cell.getY() - 3.0f, 1.5f, cell.getHeight() + 6.0f);
        }
    }
    drawKeyboard (g);
    drawRuler (g);
    drawPlayhead (g);

    if (snap.status != 2 || snap.notes.empty())
    {
        const String msg = snap.status == 1 ? "Listening to the clip..."
                         : snap.status == 3 ? "Couldn't read the clip's audio."
                         : snap.status == 4 ? "This clip is too long for Honey Tune: split it into shorter clips."
                         : snap.status == 2 ? "No sung notes found in this clip."
                                            : "Waiting for the clip's audio...";
        const auto box = grid.withSizeKeepingCentre (320.0f, 44.0f);
        honeytheme::drawGlass (g, box, 8.0f, 0.85f);
        g.setColour (ink);
        g.setFont (FontOptions (16.0f, Font::bold));
        g.drawFittedText (msg, box.toNearestInt(), Justification::centred, 2);
    }
}

void HoneyRoll::drawBackground (Graphics& g, Rectangle<float> grid)
{
    // Quiet cream marble (the notes stand out on it) with the gold honeycomb on it.
    honeytheme::drawMarble (g, grid, 0.12f, {}, true);

    if (! honeycomb.isValid() || honeycomb.getWidth() != getWidth() || honeycomb.getHeight() != getHeight())
    {
        honeycomb = Image (Image::ARGB, std::max (1, getWidth()), std::max (1, getHeight()), true);
        Graphics hg (honeycomb);
        const float r = 18.0f, w = std::sqrt (3.0f) * r;
        Path hex;
        for (int k = 0; k < 6; ++k)
        {
            const float a = MathConstants<float>::pi / 3.0f * static_cast<float> (k) + MathConstants<float>::pi / 6.0f;
            const Point<float> p { r * std::cos (a), r * std::sin (a) };
            if (k == 0) hex.startNewSubPath (p); else hex.lineTo (p);
        }
        hex.closeSubPath();
        int row = 0;
        for (float y = 0; y < static_cast<float> (getHeight()) + r; y += r * 1.5f, ++row)
            for (float x = (row % 2) ? w / 2 : 0.0f; x < static_cast<float> (getWidth()) + w; x += w)
            {
                hg.setColour (Colours::white.withAlpha (0.5f));
                hg.strokePath (hex, PathStrokeType (0.8f), AffineTransform::translation (x + 0.7f, y + 0.9f));
                hg.setColour (goldDeep.withAlpha (0.3f));
                hg.strokePath (hex, PathStrokeType (1.0f), AffineTransform::translation (x, y));
            }
    }
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (grid.toNearestInt());
        g.setOpacity (1.0f);
        g.drawImageAt (honeycomb, 0, 0);

        // Rows outside the key are quiet sand-gold lanes (black keys when chromatic), the key's rows stay cream and its
        // root row is a touch warmer; a fine gold hairline between every two rows (a stronger one between two rows of
        // the key: E-F, B-C).
        const int top = static_cast<int> (std::ceil (topMidi)), bottom = static_cast<int> (std::floor (midiAt (grid.getBottom())));
        auto outside = [&] (int m)
        {
            return snap.scale == 0 ? vox::kNoteNames[static_cast<size_t> ((m % 12 + 12) % 12)][1] == '#'
                                   : ! inScale (m, snap.key, snap.scale);
        };
        for (int m = top; m >= bottom; --m)
        {
            const float y = yOf (m) - static_cast<float> (rowHeight) / 2;
            const auto row = Rectangle<float> (grid.getX(), y, grid.getWidth(), static_cast<float> (rowHeight));
            if (outside (m))
            {
                g.setGradientFill (ColourGradient (Colour (0xffe6d6b2).withAlpha (0.78f), 0.0f, row.getY(),
                                                   Colour (0xffdcc89e).withAlpha (0.78f), 0.0f, row.getBottom(), false));
                g.fillRect (row);
            }
            else if (((m - snap.key) % 12 + 12) % 12 == 0)
            {
                g.setColour (gold.withAlpha (0.13f));
                g.fillRect (row);
            }
            const bool keyPair = snap.scale != 0 && ! outside (m) && ! outside (m - 1);
            g.setColour (goldDeep.withAlpha (keyPair ? 0.5f : 0.2f));
            g.fillRect (grid.getX(), row.getBottom() - (keyPair ? 0.6f : 0.5f), grid.getWidth(), keyPair ? 1.2f : 1.0f);
        }

        // Bar lines (raised gold) and beat lines (fine) from the host's tempo; seconds when it has none.
        auto raised = [&] (float x, float width)
        {
            Path p;
            const Rectangle<float> line (x - width * 0.5f, grid.getY(), width, grid.getHeight());
            p.addRectangle (line);
            g.setColour (Colours::black.withAlpha (0.18f));
            g.fillRect (line.translated (1.2f, 0.0f));
            honeytheme::fillGold (g, p, line.withY (0.0f).withHeight (40.0f));
        };
        const auto& tl = snap.timeline;
        if (tl.valid())
        {
            const double beatPx = pixelsPerSecond * (tl.lines[1].seconds - tl.lines[0].seconds);
            for (const auto& l : tl.lines)
            {
                const float x = xOf (l.seconds);
                if (x < grid.getX() - 2.0f || x > grid.getRight() + 2.0f) continue;
                if (l.beat == 1) raised (x, 2.2f);
                else if (beatPx > 14.0)
                {
                    g.setColour (goldDeep.withAlpha (0.35f));
                    g.fillRect (x - 0.5f, grid.getY(), 1.0f, grid.getHeight());
                }
            }
        }
        else
        {
            const double step = pixelsPerSecond > 60 ? 1.0 : pixelsPerSecond > 15 ? 5.0 : 10.0;
            for (double s = std::ceil (viewStart / step) * step; xOf (s) < grid.getRight(); s += step)
                raised (xOf (s), 2.2f);
        }
    }
}

void HoneyRoll::drawCell (Graphics& g, int index)
{
    const auto& v = snap.notes[static_cast<size_t> (index)];
    const bool isSel = isSelected (index);
    const bool original = model != nullptr && model->isOriginal();
    // Original (A / B): every note where it was sung, judged as sung.
    const double target = shownPitch (index);
    const bool moved = ! original && std::abs (target - v.note.pitch) > 0.04;
    const bool off = original ? v.wasOff
                              : std::abs (target - honeyui::keyNote (target, snap.key, snap.scale)) > 0.25;   // where it lands
    const bool filled = ! original && v.wasOff && ! off;   // was off-key, now on a note of the key
    const bool red = honeyui::glowsRed (v.wasOff, off, original);   // will play off-key

    // Where it was sung (dashed ghost) when it has moved.
    if (moved)
    {
        const auto ghost = wavePaths (index, v.note.pitch).outline;
        Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        PathStrokeType (1.2f).createDashedStroke (dashed, ghost, dashes, 2);
        g.setColour (navy.withAlpha (0.55f));
        g.fillPath (dashed);
    }

    const auto wave = wavePaths (index, target);   // drawn as its waveform
    const auto& cell = wave.outline;
    const auto b = cell.getBounds();

    // A glow behind every note, like the knobs: red when it will play off-key, cream when in key (gold body =
    // sung off-key, now tuned into the key).
    const Colour glowOuter = red ? Colour (0xffe0242c) : Colour (0xfffff0c8);   // red = off-key
    const Colour glowInner = red ? Colour (0xffff8a80) : Colour (0xfffffbee);
    for (int k = 0; k < 2; ++k)
        DropShadow (glowOuter, 16, {}).drawForPath (g, cell);
    DropShadow (glowInner, 6, {}).drawForPath (g, cell);
    g.setColour (glowInner.withAlpha (0.85f));
    g.strokePath (cell, PathStrokeType (4.5f, PathStrokeType::curved));
    g.setColour (Colours::black.withAlpha (0.22f));
    g.fillPath (cell, AffineTransform::translation (0.8f, 1.6f));

    // Light blue marble, or polished gold once fixed: the waveform's peaks; its body (RMS) a deeper shade inside.
    if (filled)
    {
        honeytheme::fillGold (g, cell, b);
        g.setColour (Colour (0xff8a5a10).withAlpha (0.55f));
        g.fillPath (wave.body);
    }
    else
    {
        // Light blue marble: a light blue body with the white veins of the marble over it.
        g.setGradientFill (ColourGradient (Colour (off ? 0xffb4dcf7 : 0xff9ccbee), b.getX(), b.getY(),
                                           Colour (off ? 0xff7ab4e2 : 0xff5d97cc), b.getX(), b.getBottom(), false));
        g.fillPath (cell);
        honeytheme::fillMarble (g, cell, index, 0.0f, 0.28f);
        g.setGradientFill (ColourGradient (Colour (0xff5a98d2), b.getX(), b.getY(), Colour (0xff336fa9), b.getX(), b.getBottom(), false));
        g.fillPath (wave.body);
    }
    {
        // Glassy top highlight
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cell);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.45f), 0, b.getY(), Colours::transparentWhite, 0, b.getCentreY(), false));
        g.fillRect (b);
    }

    // Gold rim, heavier (on a dark outline) when selected.
    if (isSel)
    {
        g.setColour (ink);
        g.strokePath (cell, PathStrokeType (4.4f));
    }
    Path rim;
    PathStrokeType (isSel ? 2.8f : 1.8f).createStrokedPath (rim, cell);
    honeytheme::fillGold (g, rim, b);
    g.setColour (goldDeep);
    g.strokePath (cell, PathStrokeType (0.6f));
    {
        // Bevel: light along the top edges, a dark line just inside.
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (b.withHeight (b.getHeight() * 0.5f).toNearestInt());
        g.setColour (Colour (0xfffff3c4).withAlpha (0.9f));
        g.strokePath (cell, PathStrokeType (0.8f), AffineTransform::translation (0.0f, -0.6f));
    }
    if (index == hovered && ! isSel)
    {
        g.setColour (Colours::white.withAlpha (0.7f));
        g.strokePath (cell, PathStrokeType (1.0f));
    }
}

void HoneyRoll::drawName (Graphics& g, int index)
{
    // The note's name when there's room, over the pitch line (a dark edge on the blue notes, a light one on the gold).
    const auto& v = snap.notes[static_cast<size_t> (index)];
    const bool original = model != nullptr && model->isOriginal();
    const double target = shownPitch (index);
    const bool off = original ? v.wasOff : std::abs (target - honeyui::keyNote (target, snap.key, snap.scale)) > 0.25;
    const bool filled = ! original && v.wasOff && ! off;
    const auto [t0, t1] = span (index);
    const float x0 = xOf (t0), x1 = xOf (t1);
    if (x1 - x0 <= 34.0f || rowHeight < 14.0) return;
    const auto row = static_cast<float> (rowHeight);
    const Rectangle<float> textArea (x0 + std::min (8.0f, (x1 - x0) * 0.12f), yOf (target) - row * 0.5f, x1 - x0 - 16.0f, row);
    const auto name = noteName (target);
    g.setFont (FontOptions (std::min (12.0f, row * 0.62f), Font::bold));
    g.setColour (filled ? Colour (0xfffff6dc).withAlpha (0.9f) : Colour (0xff0d1c2e).withAlpha (0.85f));
    for (auto [dx, dy] : { std::pair { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } })
        g.drawText (name, textArea.translated (dx, dy), Justification::centredLeft, false);
    g.setColour (filled ? Colours::black : Colour (0xffffd56a));   // black on the gold (fixed) notes, bright gold on the blue
    g.drawText (name, textArea, Justification::centredLeft, false);
}

void HoneyRoll::drawPitchLine (Graphics& g, Rectangle<float> vis)
{
    // One black line, as sung: through each note (Tuned = as it will play: moved with the note, its drift and vibrato as
    // kept, so a steadied note draws flat; Original = as sung), and on from one note into the next when they're sung in
    // one breath (gap under 0.2 s): the real slide between them (moved along with both notes), or a smooth curve
    // across a consonant. A longer gap (a pause) leaves a space.
    if (snap.track == nullptr || snap.notes.empty()) return;
    const auto& t = *snap.track;
    const bool original = model != nullptr && model->isOriginal();
    const auto readings = static_cast<int> (t.midi.size());
    auto shiftOf = [&] (int i)   // how far note i moves, with a drag in progress
    {
        const auto& v = snap.notes[static_cast<size_t> (i)];
        return original ? 0.0 : shownPitch (i) - v.note.pitch;
    };
    Path line;
    bool open = false;          // a stroke is under way
    Point<float> last;
    auto put = [&] (Point<float> p) { if (! open) { line.startNewSubPath (p); open = true; } else line.lineTo (p); last = p; };
    int prevShown = -1;         // the note drawn last (its line can carry on)
    for (int i = 0; i < static_cast<int> (snap.notes.size()); ++i)
    {
        const auto [t0, t1] = span (i);
        if (xOf (t1) < vis.getX() - 80.0f || xOf (t0) > vis.getRight() + 80.0f) { open = false; prevShown = -1; continue; }
        const auto& n = snap.notes[static_cast<size_t> (i)].note;
        // On from the note before?
        const bool joined = prevShown == i - 1 && open && t0 - span (i - 1).second <= 0.2;
        if (! joined) open = false;
        else
        {
            const auto& pn = snap.notes[static_cast<size_t> (i - 1)].note;
            const double shA = shiftOf (i - 1), shB = shiftOf (i);
            const double inA = t.time[static_cast<size_t> (std::clamp (pn.lastReading, 0, readings - 1))];
            const double inB = t.time[static_cast<size_t> (std::clamp (n.firstReading, 0, readings - 1))];
            const double outA = span (i - 1).second, outB = t0;
            // (Readings between notes far outside the two notes' range are a consonant misheard: skipped.)
            const double from = t.midi[static_cast<size_t> (std::clamp (pn.lastReading, 0, readings - 1))];
            const double to = t.midi[static_cast<size_t> (std::clamp (n.firstReading, 0, readings - 1))];
            const double lo = std::min (from > 0.0 ? from : to, to > 0.0 ? to : from) - 1.5, hi = std::max (from, to) + 1.5;
            for (int r = pn.lastReading + 1; r < n.firstReading && r < readings; ++r)
            {
                const double m = t.midi[static_cast<size_t> (r)];
                if (m <= 0.0 || m < lo || m > hi) continue;   // (unsung moments: the curve below bridges them)
                const double f = std::clamp ((t.time[static_cast<size_t> (r)] - inA) / std::max (1.0, inB - inA), 0.0, 1.0);
                put ({ xOf (outA + (outB - outA) * f), yOf (m + shA + (shB - shA) * f) });
            }
        }
        // Through the note.
        const auto sounding = original ? std::vector<double> {} : vox::honey::soundingLine (t, n);
        const double drag = original ? 0.0 : shiftOf (i) - (n.target - n.pitch);   // (a drag in progress)
        bool first = true;
        for (int r = n.firstReading; r <= n.lastReading && r < readings; ++r)
        {
            const auto k = static_cast<size_t> (r - n.firstReading);
            const double m = original ? t.midi[static_cast<size_t> (r)] : k < sounding.size() ? sounding[k] : 0.0;
            if (m <= 0.0) { open = false; first = false; continue; }
            if (std::abs (t.midi[static_cast<size_t> (r)] - n.pitch) > 6.0) continue;   // a reading misheard at the note's edge
            const Point<float> p { xOf (shownTime (i, t.time[static_cast<size_t> (r)] / t.sampleRate)), yOf (m + drag) };
            if (first && open && joined && p.getDistanceFrom (last) > 1.0f)
            {
                // Across a gap with nothing sung in it: an S-curve, level at both ends.
                const float mx = 0.5f * (last.x + p.x);
                line.cubicTo ({ mx, last.y }, { mx, p.y }, p);
                last = p;
            }
            else put (p);
            first = false;
        }
        prevShown = i;
    }
    g.setColour (Colours::white.withAlpha (0.6f));   // a light edge, so it reads on the notes and the lanes
    g.strokePath (line, PathStrokeType (3.4f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xff10161e).withAlpha (0.92f));
    g.strokePath (line, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void HoneyRoll::drawKeyboard (Graphics& g)
{
    const auto grid = gridArea();
    const Rectangle<float> keys (0.0f, grid.getY(), kKeysWidth, grid.getHeight());
    Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (keys.toNearestInt());
    honeytheme::drawMarble (g, keys, 0.2f, {}, true);
    const int top = static_cast<int> (std::ceil (topMidi)), bottom = static_cast<int> (std::floor (midiAt (grid.getBottom())));
    for (int m = top; m >= bottom; --m)
    {
        const float y = yOf (m) - static_cast<float> (rowHeight) / 2;
        const auto row = Rectangle<float> (2.0f, y, kKeysWidth - 6.0f, static_cast<float> (rowHeight)).reduced (0.0f, 1.0f);
        const bool black = vox::kNoteNames[static_cast<size_t> ((m % 12 + 12) % 12)][1] == '#';
        const bool root = ((m - snap.key) % 12 + 12) % 12 == 0;
        Path key;
        key.addRoundedRectangle (row, 3.0f);
        if (root)       honeytheme::fillGold (g, key, row);
        else if (black) { g.setColour (deepBlue); g.fillPath (key); }
        else            { g.setColour (Colours::white.withAlpha (0.8f)); g.fillPath (key); }
        g.setColour (goldDeep.withAlpha (0.5f));
        g.strokePath (key, PathStrokeType (0.7f));
        if (inScale (m, snap.key, snap.scale) && snap.scale != 0)
        {
            g.setColour (gold);
            g.fillRect (Rectangle<float> (kKeysWidth - 4.0f, row.getY(), 3.0f, row.getHeight()));
        }
        if (rowHeight >= 11.0 || ((m % 12 + 12) % 12) == 0)
        {
            g.setColour (black && ! root ? Colours::white : ink);
            g.setFont (FontOptions (std::min (11.0f, static_cast<float> (rowHeight) * 0.7f), Font::bold));
            g.drawText (noteName (m), row.withTrimmedLeft (4.0f).withTrimmedRight (4.0f), Justification::centredLeft, false);
        }
    }
    honeytheme::fillGold (g, [&] { Path p; p.addRectangle (kKeysWidth - 2.0f, keys.getY(), 2.0f, keys.getHeight()); return p; }(), keys);
}

void HoneyRoll::drawRuler (Graphics& g)
{
    // A polished gold strip with the time on it.
    const Rectangle<float> strip (0.0f, 0.0f, static_cast<float> (getWidth()), kRulerHeight);
    honeytheme::drawGoldBar (g, strip, 0.0f);
    const auto grid = gridArea();
    const Rectangle<float> ruler (grid.getX(), 0.0f, grid.getWidth(), kRulerHeight);
    Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (ruler.toNearestInt());
    g.setFont (FontOptions (11.0f, Font::bold));
    auto label = [&g] (float x, const String& text)
    {
        g.setColour (Colour (0xff3a2a0a));
        g.drawVerticalLine (roundToInt (x), kRulerHeight - 7.0f, kRulerHeight - 1.0f);
        g.setColour (Colours::white.withAlpha (0.45f));
        g.drawText (text, Rectangle<float> (x + 3, 4, 40, 14), Justification::centredLeft);
        g.setColour (Colour (0xff3a2a0a));
        g.drawText (text, Rectangle<float> (x + 3, 3, 40, 14), Justification::centredLeft);
    };
    const auto& tl = snap.timeline;
    if (tl.valid())
    {
        // Bar numbers like the host's ruler (every 2nd / 4th ... bar when they'd crowd), beat ticks between.
        double barPx = 1.0e9;
        for (size_t i = 1; i < tl.lines.size(); ++i)
            if (tl.lines[i].beat == 1) { for (size_t j = i; j-- > 0;) if (tl.lines[j].beat == 1) { barPx = (tl.lines[i].seconds - tl.lines[j].seconds) * pixelsPerSecond; break; } break; }
        int every = 1;
        while (barPx * every < 34.0 && every < 64) every *= 2;
        for (const auto& l : tl.lines)
        {
            const float x = xOf (l.seconds);
            if (x < ruler.getX() - 40.0f || x > ruler.getRight()) continue;
            if (l.beat == 1 && (l.bar - 1) % every == 0) label (x, String (l.bar));
            else if (l.beat != 1 && barPx > 60.0)
            {
                g.setColour (Colour (0xff3a2a0a).withAlpha (0.6f));
                g.drawVerticalLine (roundToInt (x), kRulerHeight - 4.0f, kRulerHeight - 1.0f);
            }
        }
        return;
    }
    const double step = pixelsPerSecond > 60 ? 1.0 : pixelsPerSecond > 15 ? 5.0 : 10.0;
    for (double s = std::ceil (viewStart / step) * step; xOf (s) < ruler.getRight(); s += step)
    {
        const int secs = roundToInt (s + tl.songOffset);
        label (xOf (s), String (secs / 60) + ":" + String (secs % 60).paddedLeft ('0', 2));
    }
}

//==============================================================================
void HoneyRoll::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    const auto grid = gridArea();
    // Click (or drag) on the ruler: move the host's playhead there (and follow it again).
    if (e.position.y < (float) kRulerHeight && e.position.x >= grid.getX())
    {
        seeking = true;
        following = true;
        if (onSeek) onSeek (std::max (0.0, secondsAt (e.position.x)));
        return;
    }
    // A key on the left: every note sung at that pitch (Ctrl / Cmd: added to the selection).
    if (e.position.x < (float) kKeysWidth && e.position.y >= grid.getY())
    {
        const int midi = static_cast<int> (std::round (midiAt (e.position.y)));
        auto pick = e.mods.isCommandDown() ? chosen : std::vector<char> (snap.notes.size(), 0);
        int first = -1;
        for (size_t i = 0; i < snap.notes.size(); ++i)
            if (static_cast<int> (std::round (shownPitch (static_cast<int> (i)))) == midi) { pick[i] = 1; if (first < 0) first = static_cast<int> (i); }
        selectMany (pick, first);
        return;
    }
    const int hit = noteAt (e.position);
    if (e.mods.isPopupMenu())
    {
        // Right-click: a menu, never a drag. On a note of the selection: for all of them.
        if (hit >= 0 && ! isSelected (hit)) setSelected (hit);
        showMenu (hit, e.position);
        return;
    }
    if (hit < 0)
    {
        // The empty grid: a rubber band (Ctrl / Cmd: added to the selection).
        banding = true;
        bandFrom = bandTo = e.position;
        bandBase = e.mods.isCommandDown() ? chosen : std::vector<char> (snap.notes.size(), 0);
        if (! e.mods.isCommandDown()) setSelected (-1);
        return;
    }
    if (e.mods.isCommandDown())
    {
        // Ctrl / Cmd: this note in or out of the selection.
        auto pick = chosen;
        pick.resize (snap.notes.size(), 0);
        pick[static_cast<size_t> (hit)] = pick[static_cast<size_t> (hit)] ? 0 : 1;
        selectMany (pick, hit);
        if (! isSelected (hit)) return;
    }
    else if (e.mods.isShiftDown() && selected >= 0)
    {
        // Shift: every note from the one selected to this one.
        std::vector<char> pick (snap.notes.size(), 0);
        for (int i = std::min (selected, hit); i <= std::max (selected, hit); ++i) pick[static_cast<size_t> (i)] = 1;
        selectMany (pick, hit);
    }
    else if (isSelected (hit) && selectionSize() > 1)
    {
        selected = hit;   // a note of the selection: drag them all
        if (onSelectionChanged) onSelectionChanged();
    }
    else
        setSelected (hit);
    dragging = true;
    dragStartTarget = dragTarget = snap.notes[static_cast<size_t> (hit)].note.target;
    dragStartY = e.position.y;
    dragStartX = e.position.x;
    dragSeconds = 0.0;
    const auto b = cellPath (hit, shownPitch (hit)).getBounds();
    const float grab = std::min (7.0f, b.getWidth() * 0.2f);
    pendingEdge = e.position.x < b.getX() + grab ? Drag::leftEdge : e.position.x > b.getRight() - grab ? Drag::rightEdge : Drag::undecided;
    dragMode = Drag::undecided;   // (an edge drag starts once it moves: a click on an edge only selects)
}

void HoneyRoll::groupLimits()
{
    // How far the selection can move in time together: each selected note up to the nearest note that isn't selected
    // (they keep 5 ms apart) and the clip's ends.
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    const double gap = honeyui::kTimeGapSeconds;
    const auto count = static_cast<int> (snap.notes.size());
    groupMin = -1.0e9;
    groupMax = 1.0e9;
    for (int i = 0; i < count; ++i)
    {
        if (! isSelected (i)) continue;
        const auto& n = snap.notes[static_cast<size_t> (i)].note;
        const double a = n.soundStart() / sr, b = n.soundEnd() / sr;
        int p = i - 1, q = i + 1;
        while (p >= 0 && isSelected (p)) --p;
        while (q < count && isSelected (q)) ++q;
        const double lo = p >= 0 ? snap.notes[static_cast<size_t> (p)].note.soundEnd() / sr + gap : 0.0;
        const double hi = q < count ? snap.notes[static_cast<size_t> (q)].note.soundStart() / sr - gap : snap.seconds - gap;
        groupMin = std::max (groupMin, std::min (0.0, lo - a));
        groupMax = std::min (groupMax, std::max (0.0, hi - b));
    }
}

void HoneyRoll::mouseDrag (const MouseEvent& e)
{
    if (seeking)
    {
        if (onSeek) onSeek (std::max (0.0, secondsAt (jmax (gridArea().getX(), e.position.x))));
        return;
    }
    if (banding)
    {
        bandTo = e.position;
        const auto band = Rectangle<float> (bandFrom, bandTo);
        auto pick = bandBase;
        pick.resize (snap.notes.size(), 0);
        int first = -1;
        for (int i = 0; i < static_cast<int> (snap.notes.size()); ++i)
            if (cellPath (i, shownPitch (i)).getBounds().intersects (band)) { pick[static_cast<size_t> (i)] = 1; if (first < 0) first = i; }
        chosen = std::move (pick);
        selected = isSelected (selected) ? selected : first;
        if (selected < 0)
            for (size_t i = 0; i < chosen.size(); ++i) if (chosen[i]) { selected = static_cast<int> (i); break; }
        repaint();
        return;
    }
    if (! dragging || selected < 0 || e.mods.isPopupMenu()) return;
    const float dx = e.position.x - dragStartX, dy = e.position.y - dragStartY;
    if (dragMode == Drag::undecided && std::max (std::abs (dx), std::abs (dy)) > 6.0f)
    {
        // (an end of a note: that note only, even in a selection)
        dragMode = pendingEdge != Drag::undecided ? pendingEdge : std::abs (dx) > std::abs (dy) ? Drag::time : Drag::pitch;
        if (dragMode == Drag::leftEdge || dragMode == Drag::rightEdge) { auto keep = selected; setSelected (keep); }
        if (dragMode == Drag::time) groupLimits();
        // An edit while you look at the Original: switch to Tuned (you hear and see what you do), then edit as tuned.
        if (model != nullptr && model->isOriginal() && onWantTuned) onWantTuned();
    }
    if (dragMode == Drag::pitch)
    {
        const double raw = dragStartTarget - dy / rowHeight;
        // Whole notes of the key (Shift: any semitone; Alt: free, in cents). A selection moves by the same step.
        dragTarget = e.mods.isAltDown() ? raw
                   : (e.mods.isShiftDown() || snap.scale == 0) ? std::round (raw)
                                                                : honeyui::keyNote (raw, snap.key, snap.scale);
        dragTarget = jlimit (kLowestMidi, kHighestMidi, dragTarget);
    }
    else if (dragMode != Drag::undecided)
        dragSeconds = dx / pixelsPerSecond;   // time / ends: free, in seconds (kept between the neighbours by span())
    repaint();
}

void HoneyRoll::mouseUp (const MouseEvent& ev)
{
    seeking = false;
    if (banding)
    {
        banding = false;
        repaint();
        if (onSelectionChanged) onSelectionChanged();
        return;
    }
    if (! dragging) return;
    // Where everything ends up (with the drag), before it stops.
    std::vector<std::pair<int, honeyui::NoteEdit>> edits;
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    if (model != nullptr && ! model->isOriginal())
    {
        if (dragMode == Drag::pitch && std::abs (dragTarget - dragStartTarget) > 1.0e-6)
            for (int i : getSelection())
            {
                auto e = snap.notes[static_cast<size_t> (i)].edit;
                e.moved = true;
                e.target = shownPitch (i);
                edits.push_back ({ i, e });
            }
        else if ((dragMode == Drag::time || dragMode == Drag::leftEdge || dragMode == Drag::rightEdge) && std::abs (dragSeconds) > 0.002)
            for (int i : (dragMode == Drag::time ? getSelection() : std::vector<int> { selected }))
            {
                // Back to a shift (from where it was sung) and a length (vs as sung).
                const auto& n = snap.notes[static_cast<size_t> (i)].note;
                const auto [a, b] = span (i);
                auto e = snap.notes[static_cast<size_t> (i)].edit;
                e.shift = a - n.start / sr;
                e.length = (b - a) / std::max (1.0e-6, (n.end - n.start) / sr);
                edits.push_back ({ i, e });
            }
    }
    const bool clicked = dragMode == Drag::undecided;
    dragging = false;
    dragMode = pendingEdge = Drag::undecided;
    dragSeconds = 0.0;
    if (! edits.empty())
    {
        std::vector<int> changed;
        for (const auto& edit : edits) changed.push_back (edit.first);
        applyMany (edits);
        hear (changed, true);   // how it sounds now
    }
    else if (clicked && selected >= 0 && ! ev.mods.isCommandDown() && ! ev.mods.isShiftDown())
        hear ({ selected }, false);
    repaint();
}

void HoneyRoll::hear (const std::vector<int>& which, bool afterRender)
{
    if (model == nullptr || ! hearOnClick || playing || which.empty()) return;
    double a = 1.0e18, b = -1.0;
    for (int i : which)
    {
        if (i < 0 || i >= static_cast<int> (snap.notes.size())) continue;
        const auto [t0, t1] = span (i);
        a = std::min (a, t0);
        b = std::max (b, t1);
    }
    if (b <= a) return;
    // A little before (the consonant) and after (the release); a long selection: its first 4 seconds.
    a = std::max (0.0, a - 0.03);
    model->audition (a, std::min (b + 0.08, a + 4.0), afterRender);
}

void HoneyRoll::apply (int index, const honeyui::NoteEdit& edit)
{
    applyMany ({ { index, edit } });
}

void HoneyRoll::applyMany (const std::vector<std::pair<int, honeyui::NoteEdit>>& edits)
{
    if (model == nullptr || edits.empty()) return;
    for (const auto& [i, e] : edits)
        if (i < 0 || i >= static_cast<int> (snap.notes.size())) return;
    if (model->isOriginal() && onWantTuned) onWantTuned();
    model->setEdits (edits);   // (one undo step)
    refresh();
    if (onSelectionChanged) onSelectionChanged();
}

std::vector<int> HoneyRoll::targets (int index) const
{
    // A command on a note of the selection is for the whole selection; on any other note, for that note.
    if (isSelected (index) && selectionSize() > 1) return getSelection();
    return { index };
}

void HoneyRoll::command (int index, Command c)
{
    if (index < 0 || index >= static_cast<int> (snap.notes.size())) return;
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    const auto which = targets (index);
    if (c == Command::loop)
    {
        double a = 1.0e18, b = 0.0;
        for (int i : which) { a = std::min (a, snap.notes[static_cast<size_t> (i)].note.soundStart() / sr); b = std::max (b, snap.notes[static_cast<size_t> (i)].note.soundEnd() / sr); }
        if (onLoop) onLoop (std::max (0.0, a - 0.15), b + 0.15);
        return;
    }
    std::vector<std::pair<int, honeyui::NoteEdit>> edits;
    for (int i : which)
    {
        const auto& v = snap.notes[static_cast<size_t> (i)];
        auto e = v.edit;
        switch (c)
        {
            case Command::snapToKey:   e.moved = true; e.target = honeyui::keyNote (v.note.pitch, snap.key, snap.scale); break;
            case Command::steady:      e.drift = e.vibrato = 0.0; break;
            case Command::resetPitch:  e.moved = false; e.target = 0.0; break;
            case Command::resetTiming: e.shift = 0.0; e.length = 1.0; break;
            case Command::resetTone:   e.formant = 0.0; break;
            case Command::resetSteady: e.drift = e.vibrato = -1.0; break;
            case Command::resetAll:    e = {}; break;
            case Command::loop:        break;
        }
        edits.push_back ({ i, e });
    }
    applyMany (edits);
    hear (which, true);
}

void HoneyRoll::showMenu (int hit, Point<float> at)
{
    PopupMenu m;
    const double playFrom = std::max (0.0, secondsAt (at.x));
    if (hit >= 0)
    {
        const auto which = targets (hit);
        const bool many = which.size() > 1;
        bool moved = false, timed = false, toned = false, steadied = false, any = false;
        for (int i : which)
        {
            const auto& e = snap.notes[static_cast<size_t> (i)].edit;
            moved |= e.moved; timed |= e.timed(); toned |= std::abs (e.formant) > 1.0e-6; steadied |= e.drift >= 0.0 || e.vibrato >= 0.0; any |= ! e.isDefault();
        }
        const String these = many ? String (static_cast<int> (which.size())) + " notes" : String ("this note");
        m.addSectionHeader (many ? String (static_cast<int> (which.size())) + " notes selected" : "Note " + String (hit + 1));
        m.addItem ("Snap " + String (many ? "them" : "it") + " to the key  (S)", [this, hit] { command (hit, Command::snapToKey); });
        m.addItem ("Steady " + String (many ? "them" : "it") + ": no drift, no vibrato  (D)", [this, hit] { command (hit, Command::steady); });
        m.addSeparator();
        m.addItem ("Loop and play " + these + "  (L)", [this, hit] { command (hit, Command::loop); });
        m.addItem ("Play from here  (Enter)", [this, playFrom] { if (onPlayFrom) onPlayFrom (playFrom); });
        m.addSeparator();
        {
            // Split / join: where the click was; the selected neighbours, or this note and the next.
            const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
            const auto& n = snap.notes[static_cast<size_t> (hit)].note;
            const double cut = sungTime (hit, secondsAt (at.x)) * sr;
            const bool canSplit = cut - n.start >= vox::honey::kMinPartSeconds * sr && n.end - cut >= vox::honey::kMinPartSeconds * sr;
            m.addItem ("Split the note here  (Alt + double-click)", canSplit, false, [this, hit, x = secondsAt (at.x)] { splitAt (hit, x); });
            const auto [first, last] = joinRange (hit);
            const String joinText = last - first > 1 ? "Join the " + String (last - first + 1) + " notes into one  (J)"
                                  : first < hit ? String ("Join with the note before  (J)") : String ("Join with the next note  (J)");
            m.addItem (joinText, last > first, false, [this, hit] { join (hit); });
        }
        m.addSeparator();
        m.addItem ("Reset pitch", moved, false, [this, hit] { command (hit, Command::resetPitch); });
        m.addItem ("Reset timing", timed, false, [this, hit] { command (hit, Command::resetTiming); });
        m.addItem ("Reset tone (formant)", toned, false, [this, hit] { command (hit, Command::resetTone); });
        m.addItem ("Reset drift and vibrato", steadied, false, [this, hit] { command (hit, Command::resetSteady); });
        m.addItem ("Reset everything on " + these + "  (Delete)", any, false, [this, hit] { command (hit, Command::resetAll); });
        m.addSeparator();
        m.addItem ("Select all notes  (Ctrl+A)", [this] { selectMany (std::vector<char> (snap.notes.size(), 1), selected); });
        m.addItem ("Hear notes as you click and change them (while stopped)", true, hearOnClick, [this] { hearOnClick = ! hearOnClick; });
    }
    else
    {
        m.addItem ("Play from here  (Enter)", [this, playFrom] { if (onPlayFrom) onPlayFrom (playFrom); });
        m.addItem (wholeView ? "Back to the close-up view  (F)" : "Fit the whole clip  (F)", [this] { fitButton(); });
        m.addItem ("Select all notes  (Ctrl+A)", [this] { selectMany (std::vector<char> (snap.notes.size(), 1), -1); });
        m.addItem ("Hear notes as you click and change them (while stopped)", true, hearOnClick, [this] { hearOnClick = ! hearOnClick; });
    }
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (1, 1).withPosition (localPointToGlobal (at.toInt()))));
}

void HoneyRoll::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (e.position.y < (float) kRulerHeight && e.position.x >= gridArea().getX())
    {
        following = true;
        if (onPlayFrom) onPlayFrom (std::max (0.0, secondsAt (e.position.x)));   // double-click the ruler: play from there
        return;
    }
    const int hit = noteAt (e.position);
    if (hit >= 0 && e.mods.isAltDown()) { splitAt (hit, secondsAt (e.position.x)); return; }   // Alt: split it there
    if (hit >= 0) command (hit, Command::snapToKey);
}

void HoneyRoll::mouseMove (const MouseEvent& e)
{
    updateSplitHover (e.position, e.mods.isAltDown());
    if (e.position.y < (float) kRulerHeight) { setMouseCursor (MouseCursor::PointingHandCursor); return; }
    const int hit = noteAt (e.position);
    if (hit != hovered) { hovered = hit; repaint(); }
    if (hit < 0) { setMouseCursor (MouseCursor::NormalCursor); return; }
    if (splitHover >= 0) return;   // (the split cursor)
    const auto b = cellPath (hit, shownPitch (hit)).getBounds();
    const float grab = std::min (7.0f, b.getWidth() * 0.2f);
    const bool edge = e.position.x < b.getX() + grab || e.position.x > b.getRight() - grab;
    setMouseCursor (edge ? MouseCursor::LeftRightResizeCursor : MouseCursor::UpDownLeftRightResizeCursor);
}

void HoneyRoll::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto grid = gridArea();
    if (e.mods.isAltDown())
    {
        // Alt + wheel: zoom up / down (taller or shorter rows), the note under the mouse stays put.
        const double at = midiAt (e.position.y);
        rowHeight = jlimit (kMinRow, kMaxRow, rowHeight * std::pow (1.15, (std::abs (w.deltaY) > 0.0f ? w.deltaY : w.deltaX) * 4.0));
        topMidi = at + (e.position.y - grid.getY()) / rowHeight;
    }
    else if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        userZoomed = true;
        wholeView = false;
        const double at = secondsAt (e.position.x);
        pixelsPerSecond = jlimit (std::min (kMaxPixelsPerSecond, grid.getWidth() / std::max (1.0, snap.seconds * 1.2)), kMaxPixelsPerSecond,
                                  pixelsPerSecond * std::pow (1.5, w.deltaY * 4.0));
        viewStart = at - (e.position.x - grid.getX()) / pixelsPerSecond;
        if (playing) following = false;
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        viewStart -= (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) * 300.0 / pixelsPerSecond;
        if (playing) following = false;   // looking elsewhere while it plays: stop paging along (until it plays again)
    }
    else
        topMidi += w.deltaY * 12.0;
    const double visible = grid.getWidth() / pixelsPerSecond;
    viewStart = jlimit (0.0, std::max (0.0, snap.seconds - visible * 0.5), viewStart);
    topMidi = clampTop (topMidi);
    updateScrollBars();
    repaint();
}

bool HoneyRoll::keyPressed (const KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() && onUndo != nullptr)
    {
        const auto c = CharacterFunctions::toLowerCase (static_cast<juce_wchar> (k.getKeyCode()));
        if (c == 'z') { onUndo (! mods.isShiftDown()); return true; }
        if (c == 'y') { onUndo (false); return true; }
    }
    const auto ch = CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (k.getTextCharacter() == '+' || k.getTextCharacter() == '=' || k.getTextCharacter() == '-')
    {
        const auto grid = gridArea();
        if (mods.isAltDown())
        {
            // Alt + / -: taller or shorter rows.
            const double mid = midiAt (grid.getCentreY());
            rowHeight = jlimit (kMinRow, kMaxRow, rowHeight * (k.getTextCharacter() == '-' ? 1.0 / 1.2 : 1.2));
            topMidi = clampTop (mid + grid.getHeight() * 0.5 / rowHeight);
        }
        else
        {
            // Zoom in time around the middle of the view.
            const double mid = secondsAt (grid.getCentreX());
            pixelsPerSecond = jlimit (20.0, kMaxPixelsPerSecond, pixelsPerSecond * (k.getTextCharacter() == '-' ? 1.0 / 1.4 : 1.4));
            viewStart = std::max (0.0, mid - (grid.getWidth() * 0.5) / pixelsPerSecond);
            userZoomed = true;
            wholeView = false;
        }
        updateScrollBars();
        repaint();
        return true;
    }
    if (k == KeyPress::returnKey)
    {
        following = true;
        if (onPlayFrom) onPlayFrom (-1.0);   // play from the playhead (or stop)
        return true;
    }
    if (ch == 'f' && ! mods.isAnyModifierKeyDown()) { fitButton(); return true; }
    if (ch == 'b' && ! mods.isAnyModifierKeyDown()) { if (onToggleOriginal) onToggleOriginal(); return true; }
    if (k == KeyPress::escapeKey) { setSelected (-1); if (model != nullptr) model->stopAudition(); return true; }
    if (mods.isCommandDown() && ch == 'a') { selectMany (std::vector<char> (snap.notes.size(), 1), selected); return true; }
    if (selected < 0 || model == nullptr) return false;
    if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey) { command (selected, Command::resetAll); return true; }
    if (ch == 's' && ! mods.isAnyModifierKeyDown()) { command (selected, Command::snapToKey); return true; }
    if (ch == 'd' && ! mods.isAnyModifierKeyDown()) { command (selected, Command::steady); return true; }
    if (ch == 'l' && ! mods.isAnyModifierKeyDown()) { command (selected, Command::loop); return true; }
    if (ch == 'j' && ! mods.isAnyModifierKeyDown()) { join (selected); return true; }
    if (k.getKeyCode() == KeyPress::leftKey || k.getKeyCode() == KeyPress::rightKey)
    {
        setSelected (jlimit (0, static_cast<int> (snap.notes.size()) - 1, selected + (k.getKeyCode() == KeyPress::leftKey ? -1 : 1)));
        return true;
    }
    if (k.getKeyCode() == KeyPress::upKey || k.getKeyCode() == KeyPress::downKey)
    {
        // Each selected note to the next note of the key above / below (Shift: a semitone, Alt: 5 cents).
        const bool up = k.getKeyCode() == KeyPress::upKey;
        std::vector<std::pair<int, honeyui::NoteEdit>> edits;
        for (int i : targets (selected))
        {
            const double now = snap.notes[static_cast<size_t> (i)].note.target;
            double next = 0.0;
            if (mods.isAltDown())
                next = now + (up ? 0.05 : -0.05);
            else if (mods.isShiftDown() || snap.scale == 0)
                next = std::round (now) + (up ? 1.0 : -1.0);
            else
            {
                next = std::round (now);
                for (int step = 0; step < 12; ++step)
                {
                    next += up ? 1.0 : -1.0;
                    if (std::abs (honeyui::keyNote (next, snap.key, snap.scale) - next) < 1.0e-6) break;
                }
            }
            auto edit = snap.notes[static_cast<size_t> (i)].edit;
            edit.moved = true;
            edit.target = jlimit (kLowestMidi, kHighestMidi, next);
            edits.push_back ({ i, edit });
        }
        applyMany (edits);
        std::vector<int> changed;
        for (const auto& edit : edits) changed.push_back (edit.first);
        hear (changed, true);
        return true;
    }
    return false;
}
