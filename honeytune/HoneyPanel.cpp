#include "HoneyPanel.h"

#include "HoneyTheme.h"

using namespace juce;

namespace
{
    using honeytheme::ink; using honeytheme::navy;

    String noteName (double midi)
    {
        const int m = roundToInt (midi);
        return String (vox::kNoteNames[static_cast<size_t> ((m % 12 + 12) % 12)]) + String (m / 12 - 1);
    }
    String cents (double semis)
    {
        const int c = roundToInt (semis * 100.0);
        return (c > 0 ? "+" : "") + String (c) + " cents";
    }
}

HoneyPanel::HoneyPanel() : look (std::make_unique<honeytheme::Look>())
{
    setLookAndFeel (look.get());
    addAndMakeVisible (roll);

    for (int k = 0; k < 12; ++k) key.addItem (vox::kNoteNames[static_cast<size_t> (k)], k + 1);
    key.addItem ("Auto", honeyui::kAutoKey + 1);
    for (int k = 0; k < vox::kScales; ++k) scale.addItem (vox::kScaleNames[static_cast<size_t> (k)], k + 1);

    for (auto* s : { &snap, &drift, &vibrato, &noteDrift, &noteVibrato })
    {
        s->setRange (0.0, 100.0, 1.0);
        s->setTextValueSuffix (" %");
        s->setSliderStyle (Slider::LinearHorizontal);
        s->setTextBoxStyle (Slider::TextBoxRight, false, 46, 20);
        addAndMakeVisible (*s);
    }
    auto label = [this] (Label& l, const String& text)
    {
        l.setText (text, dontSendNotification);
        l.setFont (FontOptions (12.0f, Font::bold));
        l.setColour (Label::textColourId, Colour (0xff8d641f));
        addAndMakeVisible (l);
    };
    label (keyLabel, "KEY");
    label (scaleLabel, "SCALE");
    label (snapLabel, "SNAP TO NOTE");
    label (driftLabel, "KEEP DRIFT");
    label (vibratoLabel, "KEEP VIBRATO");
    label (noteDriftLabel, "THIS NOTE: DRIFT");
    label (noteVibratoLabel, "THIS NOTE: VIBRATO");
    for (auto* c : { &key, &scale }) addAndMakeVisible (*c);
    for (auto* l : { &keyLabel, &scaleLabel, &snapLabel, &driftLabel, &vibratoLabel })
        l->setVisible (false);   // painted in gold by the panel
    for (auto* b : { &snapNote, &resetNote, &resetAll, &fit, &original, &tuned }) addAndMakeVisible (*b);
    // A / B switch: the lit side is what you see and hear (your edits are kept either way).
    original.setTooltip ("Hear and see the clip as recorded");
    tuned.setTooltip ("Hear and see it with your edits");
    for (auto* b : { &original, &tuned })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
    }
    original.setConnectedEdges (Button::ConnectedOnRight);
    tuned.setConnectedEdges (Button::ConnectedOnLeft);
    tuned.setToggleState (true, dontSendNotification);
    auto ab = [this]
    {
        if (model != nullptr) model->setOriginal (original.getToggleState());
        roll.repaint();
    };
    original.onClick = ab;
    tuned.onClick = ab;

    key.onChange = scale.onChange = [this] { applySettings(); };
    for (auto* s : { &snap, &drift, &vibrato }) s->onDragEnd = [this] { applySettings(); };
    for (auto* s : { &snap, &drift, &vibrato })
        s->onValueChange = [this, s] { if (! s->isMouseButtonDown()) applySettings(); };   // typed values
    for (auto* s : { &noteDrift, &noteVibrato })
    {
        s->onDragEnd = [this] { applyNoteEdit(); };
        s->onValueChange = [this, s] { if (! s->isMouseButtonDown()) applyNoteEdit(); };
    }

    snapNote.onClick = [this]
    {
        const int i = roll.getSelected();
        if (i < 0 || model == nullptr) return;
        const auto& snapData = roll.getSnapshot();
        auto e = snapData.notes[static_cast<size_t> (i)].edit;
        e.moved = true;
        e.target = honeyui::keyNote (snapData.notes[static_cast<size_t> (i)].note.pitch, snapData.key, snapData.scale);
        model->setEdit (i, e);
        refresh();
    };
    resetNote.onClick = [this]
    {
        if (roll.getSelected() < 0 || model == nullptr) return;
        model->setEdit (roll.getSelected(), {});
        refresh();
    };
    resetAll.onClick = [this]
    {
        if (model == nullptr) return;
        model->resetAllEdits();
        refresh();
    };
    fit.onClick = [this] { roll.fitAll(); };   // also undoes your zoom
    roll.onSelectionChanged = [this] { updateNoteControls(); };
    roll.onSeek = [this] (double t)
    {
        if (model != nullptr) model->seek (t);
        lastPosition = t;   // the readout shows it straight away
        repaint (readoutArea.toNearestInt());
    };
    updateNoteControls();
    sendLookAndFeelChange();   // the slider boxes were made before their parent had the cream look
}

HoneyPanel::~HoneyPanel() { setLookAndFeel (nullptr); }

void HoneyPanel::setModel (honeyui::Model* m)
{
    model = m;
    if (model != nullptr)
    {
        (model->isOriginal() ? original : tuned).setToggleState (true, dontSendNotification);
        const auto s = model->getSettings();
        key.setSelectedId (s.key + 1, dontSendNotification);
        scale.setSelectedId (s.scale + 1, dontSendNotification);
        snap.setValue (s.snap * 100.0, dontSendNotification);
        drift.setValue (s.drift * 100.0, dontSendNotification);
        vibrato.setValue (s.vibrato * 100.0, dontSendNotification);
    }
    roll.setModel (model);
    refresh();
}

void HoneyPanel::refresh()
{
    roll.refresh();
    const auto& s = roll.getSnapshot();
    int sungOff = 0, off = 0, fixed = 0, edited = 0;
    for (const auto& n : s.notes)
    {
        sungOff += n.wasOff ? 1 : 0;
        off += n.off ? 1 : 0;
        fixed += n.fixed ? 1 : 0;
        edited += n.edit.isDefault() ? 0 : 1;
    }
    switch (s.status)
    {
        case 2:
            status = String (s.notes.size()) + " notes  |  key " + vox::kNoteNames[static_cast<size_t> (s.key)] + " "
                   + vox::kScaleNames[static_cast<size_t> (s.scale)] + "  |  sung off-key " + String (sungOff) + ", fixed " + String (fixed)
                   + ", still off-key " + String (off) + "  |  " + String (edited) + " changed by hand  |  heard " + vox::kNoteNames[static_cast<size_t> (s.guess.key)]
                   + (s.guess.minor ? " minor" : " major") + " (" + String (roundToInt (s.guess.confidence * 100)) + " % sure)";
            break;
        case 1: status = "Listening to the clip..."; break;
        case 3: status = "Couldn't read the clip's audio."; break;
        default: status = model == nullptr ? "Honey Tune works on a clip: in Cubase select the vocal event, then Audio > Extensions > Honey Tune."
                                          : "Waiting for the clip's audio..."; break;
    }
    updateNoteControls();
    repaint();
}

void HoneyPanel::setPlayhead (double seconds)
{
    roll.setPlayhead (seconds);
    if (seconds >= 0.0 && std::abs (seconds - lastPosition) > 0.0005)
    {
        lastPosition = seconds;
        repaint (readoutArea.toNearestInt());
    }
}

void HoneyPanel::applySettings()
{
    if (model == nullptr) return;
    honeyui::Settings s;
    s.key = key.getSelectedId() - 1;
    s.scale = scale.getSelectedId() - 1;
    s.snap = snap.getValue() / 100.0;
    s.drift = drift.getValue() / 100.0;
    s.vibrato = vibrato.getValue() / 100.0;
    model->setSettings (s);
    refresh();
}

void HoneyPanel::updateNoteControls()
{
    const int i = roll.getSelected();
    const auto& s = roll.getSnapshot();
    const bool has = i >= 0 && i < static_cast<int> (s.notes.size());
    for (Component* c : { (Component*) &noteDrift, (Component*) &noteVibrato, (Component*) &snapNote, (Component*) &resetNote })
        c->setEnabled (has);
    if (! has)
    {
        noteInfo = "Click a note to select it. Drag up / down to move it (hold Alt for fine moves), double-click to snap it, Delete to reset it. Zoom: Ctrl + wheel or + / -.";
        repaint();
        return;
    }
    const auto& n = s.notes[static_cast<size_t> (i)];
    noteDrift.setValue (n.note.drift * 100.0, dontSendNotification);
    noteVibrato.setValue (n.note.vibrato * 100.0, dontSendNotification);
    const double sung = n.note.pitch, now = n.note.target;
    noteInfo = "Note " + String (i + 1) + ": sung " + noteName (sung) + " " + cents (sung - std::round (sung))
             + "  ->  plays " + noteName (now) + (std::abs (now - std::round (now)) > 0.005 ? " " + cents (now - std::round (now)) : String())
             + (n.off ? "  (off-key)" : n.wasOff ? "  (was off-key, fixed)" : "  (in key)")
             + (n.edit.isDefault() ? "" : "  - changed by hand");
    repaint();
}

void HoneyPanel::applyNoteEdit()
{
    const int i = roll.getSelected();
    if (i < 0 || model == nullptr) return;
    auto e = roll.getSnapshot().notes[static_cast<size_t> (i)].edit;
    e.drift = noteDrift.getValue() / 100.0;
    e.vibrato = noteVibrato.getValue() / 100.0;
    model->setEdit (i, e);
    refresh();
}

//==============================================================================
void HoneyPanel::paint (Graphics& g)
{
    using namespace honeytheme;
    g.fillAll (night);
    const auto fr = frameArea();
    const auto inner = fr.reduced (kFrame);

    // Marble bars (top and bottom); the roll paints the middle.
    drawCreamGlass (g, topBar, 4.0f);
    drawCreamGlass (g, bottomBar, 4.0f);

    // Glass cards behind every group of controls and text.
    for (const auto& c : cards)
        drawGlass (g, c, 7.0f, 0.6f);

    // Title art
    g.setOpacity (1.0f);
    if (const auto img = title(); img.isValid())
    {
        // A soft dark halo first, so the marble letters stand off the marble bar.
        const auto placed = RectanglePlacement (RectanglePlacement::xLeft | RectanglePlacement::yBottom).appliedTo (img.getBounds().toFloat(), titleArea);
        const auto scaled = img.rescaled (roundToInt (placed.getWidth()), roundToInt (placed.getHeight()), Graphics::highResamplingQuality);
        Graphics::ScopedSaveState save (g);
        g.addTransform (AffineTransform::translation (placed.getX(), placed.getY()));
        DropShadow (Colour (80, 56, 14).withAlpha (0.3f), 4, { 0, 3 }).drawForImage (g, scaled.convertedToFormat (Image::ARGB));
        g.setOpacity (1.0f);
        g.drawImageAt (scaled, 0, 0);
    }

    // Control labels on the marble, in embossed gold.
    for (auto* l : { &keyLabel, &scaleLabel, &snapLabel, &driftLabel, &vibratoLabel })
        drawCaps (g, l->getText(), l->getBounds().toFloat(), FontOptions (12.5f, Font::bold), Justification::centredLeft);

    // Legend
    {
        auto legend = legendArea.reduced (8.0f, 5.0f);
        g.setFont (FontOptions (11.0f, Font::bold));
        int k = 0;
        for (const auto* text : { "in key", "off-key", "fixed" })
        {
            auto row = legend.removeFromTop (legend.getHeight() / (float) (3 - k));
            auto sw = row.removeFromLeft (24.0f).withSizeKeepingCentre (20.0f, 10.0f);
            Path p;
            p.addRoundedRectangle (sw, 3.0f);
            if (k == 1) { g.setColour (Colour (0xffe0242c).withAlpha (0.75f)); g.strokePath (p, PathStrokeType (4.0f)); }
            if (k == 2) fillGold (g, p, sw); else fillMarble (g, p, 5, 0.3f);
            g.setColour (goldDeep);
            g.strokePath (p, PathStrokeType (1.2f));
            g.setColour (ink);
            g.drawText (text, row.withTrimmedLeft (4.0f), Justification::centredLeft);
            ++k;
        }
    }

    // Logo, top right
    g.setOpacity (1.0f);
    if (const auto img = logo(); img.isValid() && ! logoArea.isEmpty())
        g.drawImage (img, logoArea, RectanglePlacement::centred);

    // Time readout (bars . beats like the host, and the song time), synced to playback.
    {
        const bool playing = roll.getPlayhead() >= 0.0;
        drawGlass (g, readoutArea, 7.0f, 0.85f);
        g.setColour (playing ? Colour (0xff1e7d32) : Colour (0xff8d641f));
        g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (playing ? String (CharPointer_UTF8 ("\xe2\x96\xb6")) : String (CharPointer_UTF8 ("\xe2\x96\xa0")),
                    readoutArea.withWidth (24.0f).translated (6.0f, 0.0f), Justification::centred);
        g.setColour (ink);
        g.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 14.0f, Font::bold));
        g.drawText (roll.getSnapshot().timeline.describe (lastPosition), readoutArea.withTrimmedLeft (32.0f), Justification::centredLeft);
    }

    // Bottom text: the selected note, then the status line.
    g.setColour (ink);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawFittedText (noteInfo, noteInfoArea.reduced (10.0f, 0.0f).toNearestInt(), Justification::centredLeft, 1);
    g.setColour (navy);
    g.setFont (FontOptions (11.5f, Font::bold));
    g.drawFittedText (status, statusArea.reduced (10.0f, 0.0f).toNearestInt(), Justification::centredLeft, 1);

    // Gold: the drips, the frame and the bars between the sections.
    drawGoldFrame (g, fr, kFrame, 12.0f);
    for (const auto& sep : { Rectangle<float> (inner.getX(), topBar.getBottom(), inner.getWidth(), kSeparator),
                             Rectangle<float> (inner.getX(), bottomBar.getY() - kSeparator, inner.getWidth(), kSeparator) })
    {
        Path p;
        p.addRectangle (sep);
        fillGold (g, p, sep);
        g.setColour (goldDeep);
        g.drawRect (sep, 0.6f);
    }
}

void HoneyPanel::paintOverChildren (Graphics&) {}

void HoneyPanel::drawDrips (Graphics& g, Rectangle<float> fr)
{
    // Honey running off the bottom of the frame (the drips from the mockup art). Drawn before the
    // frame, with their tops tucked under it, so they flow out of it.
    g.setOpacity (1.0f);
    const float positions[] = { 0.26f, 0.61f, 0.87f };
    for (int k = 0; k < 3; ++k)
    {
        const auto img = honeytheme::drip (k);
        if (! img.isValid()) continue;
        const float h = kDripRoom + kFrame + 12.0f, w = h * (float) img.getWidth() / (float) img.getHeight();
        const float x = fr.getX() + fr.getWidth() * positions[k] - (k == 2 ? w * 0.5f : 0.0f);
        g.drawImage (img, { std::min (x, fr.getRight() - w - 4.0f), fr.getBottom() - kFrame + 0.5f, w, h }, RectanglePlacement::stretchToFit);
    }
}

Rectangle<float> HoneyPanel::frameArea() const
{
    return getLocalBounds().toFloat().reduced (6.0f).withTrimmedBottom (kDripRoom);
}

void HoneyPanel::resized()
{
    const auto inner = frameArea().reduced (kFrame);
    auto r = inner;
    topBar = r.removeFromTop (76.0f);
    r.removeFromTop (kSeparator);
    bottomBar = r.removeFromBottom (78.0f);
    r.removeFromBottom (kSeparator);
    roll.setBounds (r.toNearestInt());
    cards.clear();

    // Top: title | key | scale | snap | drift | vibrato | fit ... legend | logo
    // The title's drips run down to just above the gold trim.
    titleArea = topBar.withTrimmedLeft (10.0f).withTrimmedTop (5.0f).withTrimmedBottom (3.0f).withWidth (190.0f);
    auto top = topBar.reduced (8.0f, 4.0f);
    top.removeFromLeft (192.0f);
    top.removeFromLeft (6.0f);
    top = top.withSizeKeepingCentre (top.getWidth(), 54.0f);
    // The logo only when there's room; the controls come first.
    logoArea = getWidth() >= 1320 ? top.removeFromRight (120.0f) : Rectangle<float>();
    top.removeFromRight (6.0f);
    legendArea = top.removeFromRight (92.0f);
    cards.push_back (legendArea);
    top.removeFromRight (10.0f);
    fit.setBounds (top.removeFromRight (52.0f).withSizeKeepingCentre (52.0f, 28.0f).translated (0.0f, 8.0f).toNearestInt());
    top.removeFromRight (6.0f);
    auto abArea = top.removeFromRight (148.0f).withSizeKeepingCentre (148.0f, 28.0f).translated (0.0f, 8.0f);
    tuned.setBounds (abArea.removeFromRight (70.0f).toNearestInt());
    original.setBounds (abArea.toNearestInt());
    top.removeFromRight (8.0f);
    auto column = [this, &top] (float width, Label& l, Component& c)
    {
        auto col = top.removeFromLeft (width);
        top.removeFromLeft (8.0f);
        col = col.reduced (4.0f, 4.0f);
        l.setBounds (col.removeFromTop (16.0f).toNearestInt());
        c.setBounds (col.removeFromTop (26.0f).toNearestInt());
    };
    column (70.0f, keyLabel, key);
    column (136.0f, scaleLabel, scale);
    const float sliderW = jlimit (120.0f, 210.0f, (top.getWidth() - 3 * 8.0f) / 3.0f);
    column (sliderW, snapLabel, snap);
    column (sliderW, driftLabel, drift);
    column (sliderW, vibratoLabel, vibrato);

    // Bottom: note info line, then per-note sliders and buttons, then the status line.
    auto bottom = bottomBar.reduced (8.0f, 4.0f);
    noteInfoArea = bottom.removeFromTop (22.0f);
    readoutArea = noteInfoArea.removeFromRight (230.0f);
    noteInfoArea.removeFromRight (8.0f);
    statusArea = bottom.removeFromBottom (18.0f);
    cards.push_back (noteInfoArea);
    cards.push_back (statusArea);
    auto row = bottom.reduced (0.0f, 3.0f);
    auto pair = [this, &row] (float width, Label& l, Slider& s)
    {
        auto c = row.removeFromLeft (width);
        row.removeFromLeft (8.0f);
        cards.push_back (c);
        c = c.reduced (8.0f, 2.0f);
        l.setBounds (c.removeFromLeft (128.0f).toNearestInt());
        s.setBounds (c.toNearestInt());
    };
    const float pairW = jlimit (250.0f, 360.0f, (row.getWidth() - 380.0f) / 2.0f);
    pair (pairW, noteDriftLabel, noteDrift);
    pair (pairW, noteVibratoLabel, noteVibrato);
    resetAll.setBounds (row.removeFromRight (116.0f).toNearestInt());
    row.removeFromRight (6.0f);
    resetNote.setBounds (row.removeFromRight (96.0f).toNearestInt());
    row.removeFromRight (6.0f);
    snapNote.setBounds (row.removeFromRight (130.0f).toNearestInt());
}
