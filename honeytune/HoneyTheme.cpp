#include "HoneyTheme.h"

#include "HoneyAssets.h"

using namespace juce;

namespace honeytheme {

Image marble()      { return ImageCache::getFromMemory (HoneyAssets::marble_blue_white_jpg, HoneyAssets::marble_blue_white_jpgSize); }
Image goldTexture() { return ImageCache::getFromMemory (HoneyAssets::gold_jpg, HoneyAssets::gold_jpgSize); }
Image logo()        { return ImageCache::getFromMemory (HoneyAssets::logo_png, HoneyAssets::logo_pngSize); }
Image creamPanel()  { return ImageCache::getFromMemory (HoneyAssets::marble_cream_panel_jpg, HoneyAssets::marble_cream_panel_jpgSize); }
Image title()       { return ImageCache::getFromMemory (HoneyAssets::title_png, HoneyAssets::title_pngSize); }
Image creamMarble() { return ImageCache::getFromMemory (HoneyAssets::marble_cream_jpg, HoneyAssets::marble_cream_jpgSize); }

void drawMarble (Graphics& g, Rectangle<float> area, float wash, Point<float> offset, bool cream)
{
    const auto img = cream ? creamMarble() : marble();
    if (img.isValid())
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.toNearestInt());
        g.setOpacity (1.0f);   // images draw with the current colour's opacity
        const float scale = std::max ((area.getWidth() + offset.x) / (float) img.getWidth(), (area.getHeight() + offset.y) / (float) img.getHeight());
        g.drawImageTransformed (img, AffineTransform::scale (scale).translated (area.getX() - offset.x, area.getY() - offset.y));
    }
    if (wash > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (wash));
        g.fillRect (area);
    }
}

void fillMarble (Graphics& g, const Path& p, int seed, float wash, float opacity)
{
    const auto img = marble();
    const auto b = p.getBounds();
    Random rnd (seed * 7919 + 3);
    const float sx = rnd.nextFloat() * (float) img.getWidth() * 0.6f, sy = rnd.nextFloat() * (float) img.getHeight() * 0.6f;
    FillType ft (img, AffineTransform::translation (-sx, -sy).scaled (0.45f).translated (b.getX(), b.getY()));
    ft.setOpacity (opacity);
    g.setFillType (ft);
    g.fillPath (p);
    if (wash > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (wash));
        g.fillPath (p);
    }
}

void fillGold (Graphics& g, const Path& p, Rectangle<float> light)
{
    g.setFillType (FillType (goldTexture(), AffineTransform::scale (0.5f).translated (light.getX(), light.getY())));
    g.fillPath (p);
    // Polish: bright at the top, deeper at the bottom.
    ColourGradient shine (Colours::white.withAlpha (0.38f), 0, light.getY(), Colours::black.withAlpha (0.28f), 0, light.getBottom(), false);
    shine.addColour (0.45, Colours::transparentWhite);
    g.setGradientFill (shine);
    g.fillPath (p);
}

void drawGoldBar (Graphics& g, Rectangle<float> r, float corner)
{
    g.setColour (Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f).expanded (0.5f, 0.5f), corner);
    Path p;
    p.addRoundedRectangle (r, corner);
    fillGold (g, p, r);
    g.setColour (goldLight.withAlpha (0.9f));
    g.drawHorizontalLine (roundToInt (r.getY()), r.getX() + corner, r.getRight() - corner);
    g.setColour (goldDeep.withAlpha (0.95f));
    g.drawHorizontalLine (roundToInt (r.getBottom()) - 1, r.getX() + corner, r.getRight() - corner);
}

void drawGoldText (Graphics& g, const String& text, Rectangle<float> r, Font font, Justification j)
{
    g.setFont (font);
    g.setColour (Colour (0xff0d1c2e).withAlpha (0.75f));
    g.drawText (text, r.translated (1.0f, 1.5f), j, false);
    g.setGradientFill (ColourGradient (Colour (0xfffff0c0), 0, r.getY() + r.getHeight() * 0.2f, Colour (0xffc08a2a), 0, r.getBottom() - r.getHeight() * 0.2f, false));
    g.drawText (text, r, j, false);
}

void drawCreamGlass (Graphics& g, Rectangle<float> r, float corner)
{
    Graphics::ScopedSaveState save (g);
    Path clip;
    clip.addRoundedRectangle (r, corner);
    g.reduceClipRegion (clip);
    g.setOpacity (1.0f);
    g.drawImage (creamPanel(), r, RectanglePlacement::fillDestination);
    // linear-gradient(160deg, rgba(255,255,255,.5), transparent 45%)
    const auto dir = Point<float> (std::sin (degreesToRadians (160.0f)), -std::cos (degreesToRadians (160.0f)));
    const float len = std::abs (r.getWidth() * dir.x) + std::abs (r.getHeight() * dir.y);
    const auto start = r.getCentre() - dir * (len * 0.5f);
    ColourGradient sheen (Colours::white.withAlpha (0.5f), start, Colours::white.withAlpha (0.0f), start + dir * (len * 0.45f), false);
    g.setGradientFill (sheen);
    g.fillRect (r);
    // inset 0 0 14px rgba(36,97,143,.35)
    for (int i = 0; i < 14; ++i)
    {
        g.setColour (Colour (36, 97, 143).withAlpha (0.35f * std::pow (1.0f - (float) i / 14.0f, 2.0f) * 0.35f));
        g.drawRoundedRectangle (r.reduced ((float) i + 0.5f), std::max (0.0f, corner - (float) i), 1.0f);
    }
}

void drawCaps (Graphics& g, const String& text, Rectangle<float> r, Font font, Justification j)
{
    g.setFont (font);
    g.setColour (Colours::white.withAlpha (0.9f));
    g.drawText (text, r.translated (0.0f, 1.0f), j, false);
    g.setColour (Colour (0xff8d641f));
    g.drawText (text, r, j, false);
}

void drawGoldFrame (Graphics& g, Rectangle<float> r, float t, float corner)
{
    Path frame;
    frame.addRoundedRectangle (r, corner);
    frame.addRoundedRectangle (r.reduced (t), std::max (0.0f, corner - t));
    frame.setUsingNonZeroWinding (false);
    fillGold (g, frame, r);
    g.setColour (goldDeep.withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.drawRoundedRectangle (r.reduced (t - 0.5f), std::max (0.0f, corner - t), 1.0f);
    g.setColour (goldLight.withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (1.5f), corner, 0.8f);
}

void drawGlass (Graphics& g, Rectangle<float> r, float corner, float alpha)
{
    g.setColour (Colours::white.withAlpha (alpha));
    g.fillRoundedRectangle (r, corner);
    g.setColour (gold.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

//==============================================================================
Look::Look()
{
    setColour (Label::textColourId, ink);
    setColour (ComboBox::textColourId, ink);
    setColour (ComboBox::arrowColourId, goldDeep);
    setColour (PopupMenu::backgroundColourId, Colour (0xfff7fafc));
    setColour (PopupMenu::textColourId, ink);
    setColour (PopupMenu::highlightedBackgroundColourId, gold);
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (TextButton::textColourOffId, ink);
    setColour (TextButton::textColourOnId, ink);
    setColour (Slider::textBoxTextColourId, ink);
}

void Look::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    drawGlass (g, r, 5.0f, box.isMouseOver() ? 0.9f : 0.8f);
    Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 5, ay - 2.5f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 5, ay - 2.5f);
    g.setColour (goldDeep);
    g.strokePath (arrow, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
}

void Look::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    // Joined edges (the Original | Tuned switch) are square, so the pair reads as one control.
    const bool left = ! b.isConnectedOnLeft(), right = ! b.isConnectedOnRight();
    Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 6.0f, 6.0f, left, right, left, right);
    const bool isSwitch = b.getRadioGroupId() != 0;
    if (b.getToggleState() || (! isSwitch && (down || over)))   // the lit side of a switch stays gold
        fillGold (g, p, r);
    else
    {
        g.setColour (Colours::white.withAlpha (over ? 0.95f : 0.82f));
        g.fillPath (p);
    }
    g.setColour (goldDeep.withAlpha (b.isEnabled() ? 0.9f : 0.4f));
    g.strokePath (p, PathStrokeType (1.2f));
}

void Look::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const Rectangle<float> track ((float) x, cy - 3.0f, (float) w, 6.0f);
    drawGlass (g, track, 3.0f, 0.8f);
    Path filled;
    filled.addRoundedRectangle (track.withRight (pos), 3.0f);
    fillGold (g, filled, track);
    // A small gold-rimmed blue marble knob.
    const float kr = 7.5f;
    Path knob;
    knob.addEllipse (pos - kr, cy - kr, kr * 2, kr * 2);
    fillMarble (g, knob, 11, 0.1f);
    g.setColour (s.isEnabled() ? goldDeep : goldDeep.withAlpha (0.4f));
    g.strokePath (knob, PathStrokeType (2.0f));
    g.setColour (goldLight);
    g.strokePath (knob, PathStrokeType (0.8f));
}

void Look::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical, int start, int size, bool over, bool)
{
    const auto area = Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    drawGlass (g, area, 4.0f, 0.45f);
    const auto thumb = vertical ? Rectangle<float> (area.getX(), (float) start, area.getWidth(), (float) size)
                                : Rectangle<float> ((float) start, area.getY(), (float) size, area.getHeight());
    Path p;
    p.addRoundedRectangle (thumb.reduced (1.0f), 4.0f);
    fillGold (g, p, thumb);
    if (over) { g.setColour (Colours::white.withAlpha (0.2f)); g.fillPath (p); }
}

Label* Look::createSliderTextBox (Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setColour (Label::textColourId, ink);
    l->setColour (Label::backgroundColourId, Colours::white.withAlpha (0.78f));
    l->setColour (Label::outlineColourId, gold.withAlpha (0.7f));
    l->setColour (TextEditor::textColourId, ink);
    l->setColour (TextEditor::backgroundColourId, Colours::white);
    l->setColour (TextEditor::highlightColourId, gold.withAlpha (0.4f));
    l->setColour (CaretComponent::caretColourId, ink);
    return l;
}

} // namespace honeytheme
