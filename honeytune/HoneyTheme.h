#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Honey Tune's look: blue-and-white liquid marble, polished gold, honey drips. */
namespace honeytheme {

const juce::Colour ink { 0xff142a44 }, navy { 0xff1d4f8a }, deepBlue { 0xff2c5a8a }, glow { 0xff7fd3ff };
const juce::Colour gold { 0xffc9952f }, goldLight { 0xfff6d682 }, goldDeep { 0xff7f5a18 }, night { 0xff121a24 };

juce::Image marble();   // 1600 x 1000 blue / white liquid marble
juce::Image goldTexture();
juce::Image logo();

/** Marble across `area` (fills it, cropped), washed toward white by `wash` (0..1). */
void drawMarble (juce::Graphics&, juce::Rectangle<float> area, float wash, juce::Point<float> offset = {});
/** Fill a path with marble (a patch chosen by `seed`, scaled down so the veins are fine). */
void fillMarble (juce::Graphics&, const juce::Path&, int seed, float wash, float opacity = 1.0f);
/** Fill a path with polished gold (texture + light from the top). */
void fillGold (juce::Graphics&, const juce::Path&, juce::Rectangle<float> lightArea);
/** A gold frame (bevelled border) of `thickness` around `r`, rounded. */
void drawGoldFrame (juce::Graphics&, juce::Rectangle<float> r, float thickness, float corner);
/** Honey drips hanging from the line y = `edgeY` between x0 and x1 (deterministic per seed). */
void drawDrips (juce::Graphics&, float x0, float x1, float edgeY, float maxLength, int count, int seed);
/** Translucent white glass behind text / controls so they read on marble. */
void drawGlass (juce::Graphics&, juce::Rectangle<float> r, float corner, float alpha = 0.72f);

/** Combo boxes, sliders and buttons: white glass, gold edges, navy text. */
class Look final : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

} // namespace honeytheme
