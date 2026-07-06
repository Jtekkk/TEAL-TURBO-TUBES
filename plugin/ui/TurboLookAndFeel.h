// TURBO TUBES — plugin/ui/TurboLookAndFeel.h
// The visual language of the box in the reference photo: chipped teal steel,
// rust, brass hardware, warm tube glow. All vector — no image resources.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ttp
{

namespace col
{
    inline const juce::Colour tealLight   { 0xff4a968c };
    inline const juce::Colour tealMid     { 0xff2e7a72 };
    inline const juce::Colour tealDark    { 0xff1d5751 };
    inline const juce::Colour tealDeep    { 0xff143f3b };
    inline const juce::Colour rustDark    { 0xff5c3a26 };
    inline const juce::Colour rustMid     { 0xff7a4e30 };
    inline const juce::Colour rustLight   { 0xff96633b };
    inline const juce::Colour brass       { 0xff8f7141 };
    inline const juce::Colour brassLight  { 0xffc9a86a };
    inline const juce::Colour brassDark   { 0xff5e4a2b };
    inline const juce::Colour glow        { 0xffff9840 };
    inline const juce::Colour glowHot     { 0xffffc97e };
    inline const juce::Colour cream       { 0xffe9dfc9 };
    inline const juce::Colour creamDim    { 0xffb9b09a };
    inline const juce::Colour panelShadow { 0x66000000 };
    inline const juce::Colour meterGreen  { 0xff7fb069 };
    inline const juce::Colour meterAmber  { 0xffe0a458 };
    inline const juce::Colour meterRed    { 0xffc84630 };
}

juce::Font stencilFont (float height, bool bold = true);

class TurboLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TurboLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawBubble (juce::Graphics&, juce::BubbleComponent&,
                     const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
};

} // namespace ttp
