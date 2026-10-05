#include "HoneyRoll.h"

using namespace juce;

namespace
{
    // Voxology palette
    const Colour cream { 0xfff3ede0 }, creamDark { 0xffe4d8bf }, ink { 0xff1d2733 }, slate { 0xff566170 };
    const Colour gold { 0xffc9952f }, goldLight { 0xfff4d684 }, goldDeep { 0xff8d641f };
    const Colour blue { 0xff3fa9f5 }, navy { 0xff1d4f8a };

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
        b->setColour (ScrollBar::thumbColourId, gold.withAlpha (0.7f));
        b->setColour (ScrollBar::trackColourId, creamDark);
        addAndMakeVisible (*b);
    }
}

//==============================================================================
void HoneyRoll::refresh()
{
    if (model == nullptr) return;
    snap = model->snapshot();
    if (selected >= static_cast<int> (snap.notes.size())) selected = -1;
    if (! fitted && snap.status == 2 && ! snap.notes.empty())
    {
        fitAll();
        fitted = true;
    }
    updateScrollBars();
    repaint();
}

void HoneyRoll::setSelected (int index)
{
    if (index == selected) return;
    selected = index;
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void HoneyRoll::fitAll()
{
    // Opens on the first ~10 seconds of singing (cells big enough to grab), the clip's main range
    // in height (the middle 90 % of its notes, so one stray high note doesn't shrink everything).
    const auto grid = gridArea();
    if (grid.getWidth() < 10.0f || snap.seconds <= 0.0) return;
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    const double firstNote = snap.notes.empty() ? 0.0 : snap.notes.front().note.start / sr;
    pixelsPerSecond = std::max (grid.getWidth() / std::min (snap.seconds, 10.0), 40.0);
    viewStart = std::max (0.0, firstNote - 0.5);
    std::vector<double> pitches;
    for (const auto& n : snap.notes) { pitches.push_back (n.note.pitch); pitches.push_back (n.note.target); }
    double lo = 48.0, hi = 72.0;
    if (! pitches.empty())
    {
        std::sort (pitches.begin(), pitches.end());
        lo = pitches[pitches.size() / 20];
        hi = pitches[pitches.size() - 1 - pitches.size() / 20];
    }
    lo -= 2.0; hi += 2.0;
    rowHeight = jlimit (16.0, 30.0, grid.getHeight() / (hi - lo + 1.0));
    const double rows = grid.getHeight() / rowHeight;
    topMidi = jlimit (kLowestMidi + rows, kHighestMidi, (hi + lo) / 2.0 + rows / 2.0);
    updateScrollBars();
    repaint();
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
    if (bar == &hBar) viewStart = start;
    else topMidi = kHighestMidi - start;
    repaint();
}

void HoneyRoll::resized()
{
    const auto b = getLocalBounds();
    hBar.setBounds (b.getX() + kKeysWidth, b.getBottom() - kBar, b.getWidth() - kKeysWidth - kBar, kBar);
    vBar.setBounds (b.getRight() - kBar, b.getY() + kRulerHeight, kBar, b.getHeight() - kRulerHeight - kBar);
    honeycomb = {};
    if (! fitted) fitAll();
    updateScrollBars();
}

//==============================================================================
Path HoneyRoll::cellPath (int index, double midi) const
{
    const auto& n = snap.notes[static_cast<size_t> (index)].note;
    const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
    const float x0 = xOf (n.start / sr), x1 = std::max (x0 + 6.0f, xOf (n.end / sr));
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

int HoneyRoll::noteAt (Point<float> pt) const
{
    for (int i = static_cast<int> (snap.notes.size()); --i >= 0;)
    {
        const double m = (dragging && i == selected) ? dragTarget : snap.notes[static_cast<size_t> (i)].note.target;
        if (cellPath (i, m).contains (pt)) return i;
    }
    return -1;
}

//==============================================================================
void HoneyRoll::paint (Graphics& g)
{
    const auto grid = gridArea();
    g.fillAll (cream);
    drawBackground (g, grid);

    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (grid.toNearestInt());
        const auto vis = g.getClipBounds().toFloat();
        const double sr = snap.track != nullptr ? snap.track->sampleRate : 48000.0;
        for (int i = 0; i < static_cast<int> (snap.notes.size()); ++i)
        {
            const auto& n = snap.notes[static_cast<size_t> (i)].note;
            if (xOf (n.end / sr) < vis.getX() || xOf (n.start / sr) > vis.getRight()) continue;
            if (i != selected) drawCell (g, i);
        }
        if (selected >= 0) drawCell (g, selected);   // on top
    }
    drawKeyboard (g);
    drawRuler (g);

    if (snap.status != 2 || snap.notes.empty())
    {
        g.setColour (ink);
        g.setFont (FontOptions (16.0f));
        const String msg = snap.status == 1 ? "Listening to the clip..."
                         : snap.status == 3 ? "Couldn't read the clip's audio."
                         : snap.status == 2 ? "No sung notes found in this clip."
                                            : "Waiting for the clip's audio...";
        g.drawFittedText (msg, grid.toNearestInt(), Justification::centred, 2);
    }
}

void HoneyRoll::drawBackground (Graphics& g, Rectangle<float> grid)
{
    // Rows: notes of the key light, the rest a shade darker; the key's root gets a gold line.
    const int top = static_cast<int> (std::ceil (topMidi)), bottom = static_cast<int> (std::floor (midiAt (grid.getBottom())));
    for (int m = top; m >= bottom; --m)
    {
        const float y = yOf (m) - static_cast<float> (rowHeight) / 2;
        const auto row = Rectangle<float> (grid.getX(), y, grid.getWidth(), static_cast<float> (rowHeight)).getIntersection (grid);
        g.setColour (inScale (m, snap.key, snap.scale) ? cream.brighter (0.25f) : creamDark);
        g.fillRect (row);
        g.setColour (((m - snap.key) % 12 + 12) % 12 == 0 ? gold.withAlpha (0.45f) : goldDeep.withAlpha (0.10f));
        g.drawHorizontalLine (roundToInt (y + rowHeight), grid.getX(), grid.getRight());
    }

    // The honeycomb pattern, faint (drawn once per size).
    if (! honeycomb.isValid() || honeycomb.getWidth() != getWidth() || honeycomb.getHeight() != getHeight())
    {
        honeycomb = Image (Image::ARGB, std::max (1, getWidth()), std::max (1, getHeight()), true);
        Graphics hg (honeycomb);
        const float r = 22.0f, w = std::sqrt (3.0f) * r;
        Path hex;
        for (int k = 0; k < 6; ++k)
        {
            const float a = MathConstants<float>::pi / 3.0f * static_cast<float> (k) + MathConstants<float>::pi / 6.0f;
            const Point<float> p { r * std::cos (a), r * std::sin (a) };
            if (k == 0) hex.startNewSubPath (p); else hex.lineTo (p);
        }
        hex.closeSubPath();
        hg.setColour (goldDeep.withAlpha (0.07f));
        int row = 0;
        for (float y = 0; y < static_cast<float> (getHeight()) + r; y += r * 1.5f, ++row)
            for (float x = (row % 2) ? w / 2 : 0.0f; x < static_cast<float> (getWidth()) + w; x += w)
                hg.strokePath (hex, PathStrokeType (1.2f), AffineTransform::translation (x, y));
    }
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (grid.toNearestInt());
        g.drawImageAt (honeycomb, 0, 0);
    }

    // Seconds
    const double step = pixelsPerSecond > 60 ? 1.0 : pixelsPerSecond > 15 ? 5.0 : 10.0;
    g.setColour (slate.withAlpha (0.12f));
    for (double s = std::ceil (viewStart / step) * step; xOf (s) < grid.getRight(); s += step)
        g.drawVerticalLine (roundToInt (xOf (s)), grid.getY(), grid.getBottom());
}

void HoneyRoll::drawCell (Graphics& g, int index)
{
    const auto& v = snap.notes[static_cast<size_t> (index)];
    const bool isSel = index == selected;
    const double target = (dragging && isSel) ? dragTarget : v.note.target;
    const bool moved = std::abs (target - v.note.pitch) > 0.04;
    const bool glow = v.wasOff && ! v.fixed && ! (dragging && isSel);
    const bool filled = v.fixed || (dragging && isSel && v.wasOff);

    // Where it was sung (dashed ghost) when it has moved.
    if (moved)
    {
        const auto ghost = cellPath (index, v.note.pitch);
        Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        PathStrokeType (1.0f).createDashedStroke (dashed, ghost, dashes, 2);
        g.setColour (slate.withAlpha (0.45f));
        g.fillPath (dashed);
    }

    const auto cell = cellPath (index, target);
    const auto b = cell.getBounds();

    if (glow)   // blue glow around off-key notes
        for (auto [width, alpha] : { std::pair { 9.0f, 0.10f }, { 6.0f, 0.16f }, { 3.5f, 0.30f } })
        {
            g.setColour (blue.withAlpha (alpha));
            g.strokePath (cell, PathStrokeType (width, PathStrokeType::curved));
        }

    // Marble fill: cream (or gold once fixed), with a couple of soft veins.
    const Colour c1 = filled ? goldLight : Colour (0xfffbf7ee), c2 = filled ? gold : Colour (0xffe6dcc6);
    g.setGradientFill (ColourGradient (c1, b.getX(), b.getY(), c2, b.getRight(), b.getBottom(), false));
    g.fillPath (cell);
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cell);
        Random rnd (index * 7919 + 17);
        g.setColour ((filled ? Colours::white : slate).withAlpha (filled ? 0.35f : 0.16f));
        for (int k = 0; k < 2; ++k)
        {
            Path vein;
            float x = b.getX() + rnd.nextFloat() * b.getWidth() * 0.3f, y = b.getY() + rnd.nextFloat() * b.getHeight();
            vein.startNewSubPath (x, y);
            while (x < b.getRight())
            {
                const float nx = x + 10.0f + rnd.nextFloat() * 18.0f;
                const float ny = jlimit (b.getY(), b.getBottom(), y + (rnd.nextFloat() - 0.5f) * b.getHeight());
                vein.quadraticTo ((x + nx) / 2, y + (rnd.nextFloat() - 0.5f) * b.getHeight(), nx, ny);
                x = nx; y = ny;
            }
            g.strokePath (vein, PathStrokeType (0.8f));
        }

        // The sung pitch line inside the cell (moved with the note), like a melody blob.
        if (snap.track != nullptr && b.getWidth() > 8.0f)
        {
            const auto& t = *snap.track;
            const double shift = target - v.note.pitch;
            Path line;
            bool started = false;
            for (int r = v.note.firstReading; r <= v.note.lastReading && r < static_cast<int> (t.midi.size()); ++r)
            {
                const double m = t.midi[static_cast<size_t> (r)];
                if (m <= 0.0) { started = false; continue; }
                const Point<float> p { xOf (t.time[static_cast<size_t> (r)] / t.sampleRate), yOf (m + shift) };
                if (! started) { line.startNewSubPath (p); started = true; } else line.lineTo (p);
            }
            g.setColour (navy.withAlpha (0.75f));
            g.strokePath (line, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    // Gold rim (with a light inner edge), heavier when selected.
    g.setGradientFill (ColourGradient (Colour (0xffe9c46a), b.getX(), b.getY(), goldDeep, b.getX(), b.getBottom(), false));
    g.strokePath (cell, PathStrokeType (isSel ? 2.6f : 1.6f));
    if (isSel)
    {
        g.setColour (ink.withAlpha (0.8f));
        g.strokePath (cell, PathStrokeType (1.0f), AffineTransform());
    }
    else if (index == hovered)
    {
        g.setColour (Colours::white.withAlpha (0.6f));
        g.strokePath (cell, PathStrokeType (1.0f));
    }

    // Note name when there's room.
    if (b.getWidth() > 34.0f && rowHeight >= 14.0)
    {
        g.setColour (ink.withAlpha (0.85f));
        g.setFont (FontOptions (std::min (12.0f, static_cast<float> (rowHeight) * 0.62f), Font::bold));
        g.drawText (noteName (target), b.reduced (b.getHeight() * 0.45f, 0.0f), Justification::centredLeft, false);
    }
}

void HoneyRoll::drawKeyboard (Graphics& g)
{
    const auto grid = gridArea();
    const Rectangle<float> keys (0.0f, grid.getY(), kKeysWidth, grid.getHeight());
    Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (keys.toNearestInt());
    g.setColour (Colour (0xffeae0cb));
    g.fillRect (keys);
    const int top = static_cast<int> (std::ceil (topMidi)), bottom = static_cast<int> (std::floor (midiAt (grid.getBottom())));
    for (int m = top; m >= bottom; --m)
    {
        const float y = yOf (m) - static_cast<float> (rowHeight) / 2;
        const auto row = Rectangle<float> (0.0f, y, kKeysWidth - 1.0f, static_cast<float> (rowHeight));
        const bool black = vox::kNoteNames[static_cast<size_t> ((m % 12 + 12) % 12)][1] == '#';
        const bool root = ((m - snap.key) % 12 + 12) % 12 == 0;
        g.setColour (root ? goldLight : black ? Colour (0xff3a4452) : Colour (0xfffbf7ee));
        g.fillRect (row.reduced (0.0f, 0.5f));
        if (inScale (m, snap.key, snap.scale) && snap.scale != 0)
        {
            g.setColour (gold);
            g.fillRect (row.withX (kKeysWidth - 5.0f).withWidth (4.0f).reduced (0.0f, 1.0f));
        }
        if (rowHeight >= 11.0 || ((m % 12 + 12) % 12) == 0)
        {
            g.setColour (black && ! root ? cream : ink);
            g.setFont (FontOptions (std::min (11.0f, static_cast<float> (rowHeight) * 0.7f)));
            g.drawText (noteName (m), row.withTrimmedLeft (4.0f).withTrimmedRight (6.0f), Justification::centredLeft, false);
        }
    }
    g.setColour (goldDeep.withAlpha (0.5f));
    g.drawVerticalLine (kKeysWidth - 1, keys.getY(), keys.getBottom());
}

void HoneyRoll::drawRuler (Graphics& g)
{
    const auto grid = gridArea();
    const Rectangle<float> ruler (grid.getX(), 0.0f, grid.getWidth(), kRulerHeight);
    g.setColour (Colour (0xffeae0cb));
    g.fillRect (Rectangle<float> (0.0f, 0.0f, static_cast<float> (getWidth()), kRulerHeight));
    g.setColour (goldDeep.withAlpha (0.5f));
    g.drawHorizontalLine (kRulerHeight - 1, 0.0f, static_cast<float> (getWidth()));
    Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (ruler.toNearestInt());
    const double step = pixelsPerSecond > 60 ? 1.0 : pixelsPerSecond > 15 ? 5.0 : 10.0;
    g.setFont (FontOptions (11.0f));
    for (double s = std::ceil (viewStart / step) * step; xOf (s) < ruler.getRight(); s += step)
    {
        const float x = xOf (s);
        g.setColour (slate);
        g.drawVerticalLine (roundToInt (x), kRulerHeight - 6.0f, kRulerHeight - 1.0f);
        const int secs = roundToInt (s);
        g.drawText (String (secs / 60) + ":" + String (secs % 60).paddedLeft ('0', 2), Rectangle<float> (x + 3, 2, 40, 14), Justification::centredLeft);
    }
}

//==============================================================================
void HoneyRoll::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    const int hit = noteAt (e.position);
    setSelected (hit);
    if (hit >= 0)
    {
        dragging = true;
        dragStartTarget = dragTarget = snap.notes[static_cast<size_t> (hit)].note.target;
        dragStartY = e.position.y;
    }
}

void HoneyRoll::mouseDrag (const MouseEvent& e)
{
    if (! dragging || selected < 0) return;
    const double moved = (dragStartY - e.position.y) / rowHeight;
    dragTarget = e.mods.isAltDown() ? dragStartTarget + moved                       // free (cents)
                                    : std::round (dragStartTarget + moved);          // whole notes
    dragTarget = jlimit (kLowestMidi, kHighestMidi, dragTarget);
    repaint();
}

void HoneyRoll::mouseUp (const MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    if (selected >= 0 && model != nullptr && std::abs (dragTarget - dragStartTarget) > 1.0e-6)
    {
        auto edit = snap.notes[static_cast<size_t> (selected)].edit;
        edit.moved = true;
        edit.target = dragTarget;
        model->setEdit (selected, edit);
        refresh();
        if (onSelectionChanged) onSelectionChanged();
    }
    repaint();
}

void HoneyRoll::mouseDoubleClick (const MouseEvent& e)
{
    const int hit = noteAt (e.position);
    if (hit < 0 || model == nullptr) return;
    auto edit = snap.notes[static_cast<size_t> (hit)].edit;
    edit.moved = true;
    edit.target = honeyui::keyNote (snap.notes[static_cast<size_t> (hit)].note.pitch, snap.key, snap.scale);
    model->setEdit (hit, edit);
    refresh();
    if (onSelectionChanged) onSelectionChanged();
}

void HoneyRoll::mouseMove (const MouseEvent& e)
{
    const int hit = noteAt (e.position);
    if (hit != hovered) { hovered = hit; repaint(); }
    setMouseCursor (hit >= 0 ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
}

void HoneyRoll::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto grid = gridArea();
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double at = secondsAt (e.position.x);
        pixelsPerSecond = jlimit (grid.getWidth() / std::max (1.0, snap.seconds * 1.2), 1500.0, pixelsPerSecond * std::pow (1.5, w.deltaY * 4.0));
        viewStart = at - (e.position.x - grid.getX()) / pixelsPerSecond;
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        viewStart -= (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) * 300.0 / pixelsPerSecond;
    else
        topMidi += w.deltaY * 12.0;
    const double visible = grid.getWidth() / pixelsPerSecond;
    viewStart = jlimit (0.0, std::max (0.0, snap.seconds - visible * 0.5), viewStart);
    topMidi = jlimit (kLowestMidi + grid.getHeight() / rowHeight, kHighestMidi, topMidi);
    updateScrollBars();
    repaint();
}

bool HoneyRoll::keyPressed (const KeyPress& k)
{
    if (selected < 0 || model == nullptr) return false;
    if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey)
    {
        model->setEdit (selected, {});
        refresh();
        if (onSelectionChanged) onSelectionChanged();
        return true;
    }
    if (k == KeyPress::leftKey || k == KeyPress::rightKey)
    {
        setSelected (jlimit (0, static_cast<int> (snap.notes.size()) - 1, selected + (k == KeyPress::leftKey ? -1 : 1)));
        return true;
    }
    if (k == KeyPress::upKey || k == KeyPress::downKey)
    {
        auto edit = snap.notes[static_cast<size_t> (selected)].edit;
        edit.moved = true;
        edit.target = std::round (snap.notes[static_cast<size_t> (selected)].note.target) + (k == KeyPress::upKey ? 1.0 : -1.0);
        model->setEdit (selected, edit);
        refresh();
        if (onSelectionChanged) onSelectionChanged();
        return true;
    }
    return false;
}
