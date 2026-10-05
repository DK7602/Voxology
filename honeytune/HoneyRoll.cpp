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
    g.fillAll (Colour (0xfff5f8fb));
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
        const String msg = snap.status == 1 ? "Listening to the clip..."
                         : snap.status == 3 ? "Couldn't read the clip's audio."
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
    // Blue / white marble (a little lighter so the notes read) with the gold honeycomb on it.
    honeytheme::drawMarble (g, grid, 0.42f);
    g.setColour (Colour (0xffdfe8f1).withAlpha (0.25f));
    g.fillRect (grid);

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
                hg.setColour (Colours::white.withAlpha (0.45f));
                hg.strokePath (hex, PathStrokeType (0.8f), AffineTransform::translation (x + 0.7f, y + 0.9f));
                hg.setColour (goldDeep.withAlpha (0.55f));
                hg.strokePath (hex, PathStrokeType (1.0f), AffineTransform::translation (x, y));
            }
    }
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (grid.toNearestInt());
        g.setOpacity (1.0f);
        g.drawImageAt (honeycomb, 0, 0);

        // Rows outside the key are raised gold bars (black keys when chromatic); the key's root glows cream.
        const int top = static_cast<int> (std::ceil (topMidi)), bottom = static_cast<int> (std::floor (midiAt (grid.getBottom())));
        for (int m = top; m >= bottom; --m)
        {
            const float y = yOf (m) - static_cast<float> (rowHeight) / 2;
            const auto row = Rectangle<float> (grid.getX(), y, grid.getWidth(), static_cast<float> (rowHeight));
            const bool root = ((m - snap.key) % 12 + 12) % 12 == 0;
            const bool bar = snap.scale == 0 ? vox::kNoteNames[static_cast<size_t> ((m % 12 + 12) % 12)][1] == '#'
                                             : ! inScale (m, snap.key, snap.scale);
            if (bar)
                honeytheme::drawGoldBar (g, row.reduced (0.0f, 1.5f));
            else if (root)
            {
                g.setColour (Colour (0xfffff3d6).withAlpha (0.55f));
                g.fillRect (row);
            }
        }
        // Thin gold line between two neighbouring rows of the key (E-F, B-C).
        for (int m = top; m >= bottom; --m)
        {
            const bool here = snap.scale == 0 || inScale (m, snap.key, snap.scale), below = snap.scale == 0 || inScale (m - 1, snap.key, snap.scale);
            if (snap.scale != 0 && here && below)
                honeytheme::drawGoldBar (g, { grid.getX(), yOf (m) + static_cast<float> (rowHeight) / 2 - 1.0f, grid.getWidth(), 2.0f }, 0.5f);
        }

        // Second lines: thin raised gold.
        const double step = pixelsPerSecond > 60 ? 1.0 : pixelsPerSecond > 15 ? 5.0 : 10.0;
        for (double s = std::ceil (viewStart / step) * step; xOf (s) < grid.getRight(); s += step)
        {
            Path p;
            const Rectangle<float> line (xOf (s) - 1.0f, grid.getY(), 2.2f, grid.getHeight());
            p.addRectangle (line);
            g.setColour (Colours::black.withAlpha (0.18f));
            g.fillRect (line.translated (1.2f, 0.0f));
            honeytheme::fillGold (g, p, line.withY (0.0f).withHeight (40.0f));
        }
    }
}

void HoneyRoll::drawCell (Graphics& g, int index)
{
    const auto& v = snap.notes[static_cast<size_t> (index)];
    const bool isSel = index == selected;
    const double target = (dragging && isSel) ? dragTarget : v.note.target;
    const bool moved = std::abs (target - v.note.pitch) > 0.04;
    const bool off = v.wasOff && ! v.fixed && ! (dragging && isSel);
    const bool filled = v.fixed || (dragging && isSel && v.wasOff);

    // Where it was sung (dashed ghost) when it has moved.
    if (moved)
    {
        const auto ghost = cellPath (index, v.note.pitch);
        Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        PathStrokeType (1.2f).createDashedStroke (dashed, ghost, dashes, 2);
        g.setColour (navy.withAlpha (0.55f));
        g.fillPath (dashed);
    }

    const auto cell = cellPath (index, target);
    const auto b = cell.getBounds();

    // A glow behind every note, like the knobs: blue when off-key, cream when in key (or fixed).
    const Colour glowOuter = off ? Colour (0xff1e90ff) : Colour (0xfffff0c8);
    const Colour glowInner = off ? Colour (0xff8fdcff) : Colour (0xfffffbee);
    for (int k = 0; k < 2; ++k)
        DropShadow (glowOuter, 16, {}).drawForPath (g, cell);
    DropShadow (glowInner, 6, {}).drawForPath (g, cell);
    g.setColour (glowInner.withAlpha (0.85f));
    g.strokePath (cell, PathStrokeType (4.5f, PathStrokeType::curved));
    g.setColour (Colours::black.withAlpha (0.22f));
    g.fillPath (cell, AffineTransform::translation (0.8f, 1.6f));

    // Blue / white marble, or polished gold once fixed.
    if (filled) honeytheme::fillGold (g, cell, b);
    else
    {
        // Blue marble: a deep blue body with the white veins of the marble over it.
        g.setGradientFill (ColourGradient (Colour (off ? 0xff6aa6dc : 0xff4f8cc6), b.getX(), b.getY(),
                                           Colour (off ? 0xff3a73ab : 0xff24507f), b.getX(), b.getBottom(), false));
        g.fillPath (cell);
        honeytheme::fillMarble (g, cell, index, 0.0f, 0.42f);
    }
    {
        // Glassy top highlight
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cell);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.45f), 0, b.getY(), Colours::transparentWhite, 0, b.getCentreY(), false));
        g.fillRect (b);

        // The sung pitch line inside the cell (moved with the note).
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
            g.setColour (filled ? goldDeep.darker (0.5f).withAlpha (0.8f) : Colours::white.withAlpha (0.9f));
            g.strokePath (line, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    // Gold rim, heavier (on a dark outline) when selected.
    if (isSel)
    {
        g.setColour (ink);
        g.strokePath (cell, PathStrokeType (5.0f));
    }
    Path rim;
    PathStrokeType (isSel ? 3.4f : 2.6f).createStrokedPath (rim, cell);
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
    g.setColour (Colours::black.withAlpha (0.25f));
    g.strokePath (cell, PathStrokeType (0.6f), AffineTransform::scale (0.94f, 0.82f, b.getCentreX(), b.getCentreY()));
    if (index == hovered && ! isSel)
    {
        g.setColour (Colours::white.withAlpha (0.7f));
        g.strokePath (cell, PathStrokeType (1.0f));
    }

    // Note name when there's room.
    if (b.getWidth() > 34.0f && rowHeight >= 14.0)
    {
        g.setColour (filled ? ink : Colours::white);
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
    honeytheme::drawMarble (g, keys, 0.3f);
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
    const double step = pixelsPerSecond > 60 ? 1.0 : pixelsPerSecond > 15 ? 5.0 : 10.0;
    g.setFont (FontOptions (11.0f, Font::bold));
    for (double s = std::ceil (viewStart / step) * step; xOf (s) < ruler.getRight(); s += step)
    {
        const float x = xOf (s);
        g.setColour (Colour (0xff3a2a0a));
        g.drawVerticalLine (roundToInt (x), kRulerHeight - 7.0f, kRulerHeight - 1.0f);
        const int secs = roundToInt (s);
        const auto label = String (secs / 60) + ":" + String (secs % 60).paddedLeft ('0', 2);
        g.setColour (Colours::white.withAlpha (0.45f));
        g.drawText (label, Rectangle<float> (x + 3, 4, 40, 14), Justification::centredLeft);
        g.setColour (Colour (0xff3a2a0a));
        g.drawText (label, Rectangle<float> (x + 3, 3, 40, 14), Justification::centredLeft);
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
