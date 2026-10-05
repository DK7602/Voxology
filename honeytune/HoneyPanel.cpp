#include "HoneyPanel.h"

using namespace juce;

namespace
{
    const Colour cream { 0xfff3ede0 }, panel { 0xffeae0cb }, ink { 0xff1d2733 }, slate { 0xff566170 };
    const Colour gold { 0xffc9952f }, goldDeep { 0xff8d641f }, blue { 0xff3fa9f5 };

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

/** Dark text on cream, gold accents. */
struct HoneyPanel::Look final : public LookAndFeel_V4
{
    Look()
    {
        setColour (Label::textColourId, ink);
        setColour (ComboBox::backgroundColourId, Colour (0xfffffaf0));
        setColour (ComboBox::textColourId, ink);
        setColour (ComboBox::outlineColourId, gold);
        setColour (ComboBox::arrowColourId, goldDeep);
        setColour (PopupMenu::backgroundColourId, Colour (0xfffffaf0));
        setColour (PopupMenu::textColourId, ink);
        setColour (PopupMenu::highlightedBackgroundColourId, gold);
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (Slider::backgroundColourId, Colour (0xffd8c9a8));
        setColour (Slider::trackColourId, gold);
        setColour (Slider::thumbColourId, goldDeep);
        setColour (Slider::textBoxTextColourId, ink);
        setColour (Slider::textBoxBackgroundColourId, Colour (0xfffffaf0));
        setColour (Slider::textBoxOutlineColourId, gold.withAlpha (0.6f));
        setColour (TextButton::buttonColourId, Colour (0xfffffaf0));
        setColour (TextButton::textColourOffId, ink);
        setColour (TextButton::textColourOnId, ink);
        setColour (ComboBox::focusedOutlineColourId, goldDeep);
    }

    Label* createSliderTextBox (Slider& slider) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        l->setColour (Label::textColourId, ink);
        l->setColour (Label::backgroundColourId, Colour (0xfffffaf0));
        l->setColour (Label::outlineColourId, gold.withAlpha (0.6f));
        l->setColour (TextEditor::textColourId, ink);
        l->setColour (TextEditor::backgroundColourId, Colour (0xfffffaf0));
        l->setColour (TextEditor::highlightColourId, gold.withAlpha (0.4f));
        l->setColour (CaretComponent::caretColourId, ink);
        return l;
    }
};

HoneyPanel::HoneyPanel() : look (std::make_unique<Look>())
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
        s->setTextBoxStyle (Slider::TextBoxRight, false, 52, 20);
        addAndMakeVisible (*s);
    }
    auto label = [this] (Label& l, const String& text)
    {
        l.setText (text, dontSendNotification);
        l.setFont (FontOptions (12.0f, Font::bold));
        l.setColour (Label::textColourId, slate);
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
    for (auto* b : { &snapNote, &resetNote, &resetAll, &fit }) addAndMakeVisible (*b);

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
    fit.onClick = [this] { roll.fitAll(); };
    roll.onSelectionChanged = [this] { updateNoteControls(); };
    updateNoteControls();
    sendLookAndFeelChange();   // the slider boxes were made before their parent had the cream look
}

HoneyPanel::~HoneyPanel() { setLookAndFeel (nullptr); }

void HoneyPanel::setModel (honeyui::Model* m)
{
    model = m;
    if (model != nullptr)
    {
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
    int off = 0, fixed = 0, edited = 0;
    for (const auto& n : s.notes)
    {
        off += n.wasOff ? 1 : 0;
        fixed += n.fixed ? 1 : 0;
        edited += n.edit.isDefault() ? 0 : 1;
    }
    switch (s.status)
    {
        case 2:
            status = String (s.notes.size()) + " notes  |  key " + vox::kNoteNames[static_cast<size_t> (s.key)] + " "
                   + vox::kScaleNames[static_cast<size_t> (s.scale)] + "  |  " + String (off) + " off-key, " + String (fixed)
                   + " fixed  |  " + String (edited) + " changed by hand  |  heard " + vox::kNoteNames[static_cast<size_t> (s.guess.key)]
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
        noteInfo = "Click a note to select it. Drag up / down to move it (hold Alt for fine moves), double-click to snap it, Delete to reset it.";
        repaint();
        return;
    }
    const auto& n = s.notes[static_cast<size_t> (i)];
    noteDrift.setValue (n.note.drift * 100.0, dontSendNotification);
    noteVibrato.setValue (n.note.vibrato * 100.0, dontSendNotification);
    const double sung = n.note.pitch, now = n.note.target;
    noteInfo = "Note " + String (i + 1) + ": sung " + noteName (sung) + " " + cents (sung - std::round (sung))
             + "  ->  plays " + noteName (now) + (std::abs (now - std::round (now)) > 0.005 ? " " + cents (now - std::round (now)) : String())
             + (n.wasOff ? (n.fixed ? "  (was off-key, fixed)" : "  (off-key)") : "  (in key)")
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
    g.fillAll (cream);
    auto r = getLocalBounds();
    auto top = r.removeFromTop (64);
    auto bottom = r.removeFromBottom (78);

    // Top and bottom bars: cream glass with a gold edge.
    for (auto bar : { top, bottom })
    {
        g.setGradientFill (ColourGradient (Colour (0xfffbf7ee), 0, (float) bar.getY(), panel, 0, (float) bar.getBottom(), false));
        g.fillRect (bar);
    }
    g.setColour (gold.withAlpha (0.7f));
    g.drawHorizontalLine (top.getBottom() - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (bottom.getY(), 0.0f, (float) getWidth());

    // Title: a small honeycomb cell + HONEY TUNE
    {
        Path hex;
        const float cx = 26.0f, cy = 24.0f, rad = 12.0f;
        for (int k = 0; k < 6; ++k)
        {
            const float a = MathConstants<float>::pi / 3.0f * (float) k + MathConstants<float>::pi / 6.0f;
            const Point<float> p { cx + rad * std::cos (a), cy + rad * std::sin (a) };
            if (k == 0) hex.startNewSubPath (p); else hex.lineTo (p);
        }
        hex.closeSubPath();
        g.setGradientFill (ColourGradient (Colour (0xfff4d684), cx, cy - rad, gold, cx, cy + rad, false));
        g.fillPath (hex);
        g.setColour (goldDeep);
        g.strokePath (hex, PathStrokeType (1.5f));
    }
    g.setColour (goldDeep);
    g.setFont (FontOptions (20.0f, Font::bold));
    g.drawText ("HONEY TUNE", 46, 10, 160, 28, Justification::centredLeft);
    g.setColour (slate);
    g.setFont (FontOptions (11.0f));
    g.drawText ("by Voxology", 48, 36, 160, 14, Justification::centredLeft);

    // Legend
    g.setFont (FontOptions (11.0f));
    auto legend = top.removeFromRight (150).reduced (6, 8);
    auto item = [&] (Colour fill, Colour edge, bool glow, const String& text)
    {
        auto row = legend.removeFromTop (16);
        auto sw = row.removeFromLeft (22).toFloat().reduced (2.0f, 3.0f);
        if (glow) { g.setColour (blue.withAlpha (0.35f)); g.fillRoundedRectangle (sw.expanded (2.0f), 4.0f); }
        g.setColour (fill); g.fillRoundedRectangle (sw, 3.0f);
        g.setColour (edge); g.drawRoundedRectangle (sw, 3.0f, 1.2f);
        g.setColour (ink); g.drawText (text, row.withTrimmedLeft (4), Justification::centredLeft);
    };
    item (Colour (0xfff6f0e2), gold, false, "in key");
    item (Colour (0xfff6f0e2), gold, true, "off-key");
    item (Colour (0xfff4d684), gold, false, "fixed");

    // Bottom: the selected note, then the status line.
    g.setColour (ink);
    g.setFont (FontOptions (13.0f));
    g.drawFittedText (noteInfo, bottom.reduced (12, 0).removeFromTop (30), Justification::centredLeft, 1);
    g.setColour (slate);
    g.setFont (FontOptions (11.5f));
    g.drawFittedText (status, bottom.reduced (12, 0).removeFromBottom (20), Justification::centredLeft, 1);
}

void HoneyPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (64).withTrimmedLeft (190).withTrimmedRight (150).reduced (0, 8);
    auto bottom = r.removeFromBottom (78);
    roll.setBounds (r);

    auto column = [&top] (int width, Label& l, Component& c)
    {
        auto col = top.removeFromLeft (width);
        top.removeFromLeft (14);
        l.setBounds (col.removeFromTop (18));
        c.setBounds (col.removeFromTop (26));
    };
    column (70, keyLabel, key);
    column (150, scaleLabel, scale);
    const int sliderW = jlimit (120, 200, (top.getWidth() - 3 * 14 - 50) / 3);
    column (sliderW, snapLabel, snap);
    column (sliderW, driftLabel, drift);
    column (sliderW, vibratoLabel, vibrato);
    fit.setBounds (top.removeFromLeft (50).withTrimmedTop (18).withHeight (26));

    auto row = bottom.withTrimmedTop (28).withTrimmedBottom (22).reduced (12, 0);
    auto pair = [&row] (int width, Label& l, Slider& s)
    {
        auto c = row.removeFromLeft (width);
        row.removeFromLeft (12);
        l.setBounds (c.removeFromLeft (130));
        s.setBounds (c);
    };
    pair (330, noteDriftLabel, noteDrift);
    pair (340, noteVibratoLabel, noteVibrato);
    resetAll.setBounds (row.removeFromRight (120).reduced (0, 1));
    row.removeFromRight (8);
    resetNote.setBounds (row.removeFromRight (100).reduced (0, 1));
    row.removeFromRight (8);
    snapNote.setBounds (row.removeFromRight (130).reduced (0, 1));
}
