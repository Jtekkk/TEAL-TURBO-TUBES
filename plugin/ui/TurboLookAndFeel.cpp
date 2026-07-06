// TURBO TUBES — plugin/ui/TurboLookAndFeel.cpp

#include "TurboLookAndFeel.h"

namespace ttp
{

juce::Font stencilFont (float height, bool bold)
{
    auto f = juce::Font (juce::FontOptions().withHeight (height)
                             .withStyle (bold ? "Bold" : "Regular"));
    return f.withExtraKerningFactor (0.08f);
}

TurboLookAndFeel::TurboLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, col::cream);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, col::cream);
    setColour (juce::ComboBox::textColourId, col::cream);
    setColour (juce::ComboBox::backgroundColourId, col::tealDeep);
    setColour (juce::ComboBox::outlineColourId, col::brassDark);
    setColour (juce::ComboBox::arrowColourId, col::brassLight);
    setColour (juce::PopupMenu::backgroundColourId, col::tealDeep);
    setColour (juce::PopupMenu::textColourId, col::cream);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::rustMid);
    setColour (juce::PopupMenu::highlightedTextColourId, col::cream);
    setColour (juce::TextButton::textColourOnId, col::cream);
    setColour (juce::TextButton::textColourOffId, col::creamDim);
    setColour (juce::TooltipWindow::backgroundColourId, col::tealDeep);
    setColour (juce::TooltipWindow::textColourId, col::cream);
    setColour (juce::TooltipWindow::outlineColourId, col::brassDark);
    setColour (juce::BubbleComponent::backgroundColourId, col::tealDeep);
    setColour (juce::BubbleComponent::outlineColourId, col::brassLight);
}

//==============================================================================
void TurboLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto square = bounds.withSizeKeepingCentre (size, size);
    const auto centre = square.getCentre();
    const float radius = size * 0.5f;
    const float knobR = radius * 0.78f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // tick ring + value arc
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius * 0.94f, radius * 0.94f,
                             0.0f, startAngle, endAngle, true);
        g.setColour (col::tealDeep.withAlpha (0.9f));
        g.strokePath (track, juce::PathStrokeType (radius * 0.075f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius * 0.94f, radius * 0.94f,
                           0.0f, startAngle, angle, true);
        g.setColour (col::glow.withAlpha (slider.isEnabled() ? 0.95f : 0.3f));
        g.strokePath (arc, juce::PathStrokeType (radius * 0.075f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        g.setColour (col::cream.withAlpha (0.55f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            const auto p1 = centre.getPointOnCircumference (radius * 0.99f, a);
            const auto p2 = centre.getPointOnCircumference (radius * 1.06f, a);
            g.drawLine ({ p1, p2 }, i == 5 ? 1.6f : 1.0f);
        }
    }

    // drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR + radius * 0.06f, knobR * 2.0f, knobR * 2.0f);

    // bezel
    {
        juce::ColourGradient bezel (col::brassDark.darker (0.4f), centre.x, centre.y - knobR,
                                    col::brassDark, centre.x, centre.y + knobR, false);
        g.setGradientFill (bezel);
        g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
    }

    // brass face
    const float faceR = knobR * 0.86f;
    {
        juce::ColourGradient face (col::brassLight, centre.x - faceR * 0.5f, centre.y - faceR * 0.6f,
                                   col::brass, centre.x + faceR * 0.4f, centre.y + faceR * 0.8f, true);
        face.addColour (0.55, col::brass.brighter (0.15f));
        g.setGradientFill (face);
        g.fillEllipse (centre.x - faceR, centre.y - faceR, faceR * 2.0f, faceR * 2.0f);

        // machined rings
        g.setColour (col::brassDark.withAlpha (0.25f));
        for (float rr = 0.35f; rr < 0.95f; rr += 0.18f)
            g.drawEllipse (centre.x - faceR * rr, centre.y - faceR * rr,
                           faceR * rr * 2.0f, faceR * rr * 2.0f, 0.8f);

        // sheen
        juce::Path sheen;
        sheen.addCentredArc (centre.x, centre.y, faceR * 0.92f, faceR * 0.92f, 0.0f,
                             -2.6f, -1.2f, true);
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.strokePath (sheen, juce::PathStrokeType (faceR * 0.10f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // pointer
    {
        const auto tip  = centre.getPointOnCircumference (faceR * 0.92f, angle);
        const auto tail = centre.getPointOnCircumference (faceR * 0.25f, angle);
        g.setColour (juce::Colour (0xff2b1d12));
        g.drawLine ({ tail, tip }, juce::jmax (2.0f, faceR * 0.13f));
        g.setColour (col::cream);
        g.fillEllipse (tip.x - faceR * 0.07f, tip.y - faceR * 0.07f, faceR * 0.14f, faceR * 0.14f);
    }

    if (! slider.isEnabled())
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
    }
}

//==============================================================================
void TurboLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                         bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    // label to the right of a small vintage switch
    const float swW = juce::jmin (34.0f, r.getWidth() * 0.42f);
    auto sw = r.removeFromLeft (swW).withSizeKeepingCentre (swW, juce::jmin (18.0f, r.getHeight()));

    // plate
    g.setColour (col::tealDeep);
    g.fillRoundedRectangle (sw, 4.0f);
    g.setColour (highlighted ? col::brassLight : col::brassDark);
    g.drawRoundedRectangle (sw, 4.0f, 1.2f);

    // lever dot slides left/right, glows when on
    auto dotArea = sw.reduced (3.0f);
    const float dotW = dotArea.getWidth() * 0.45f;
    auto dot = on ? dotArea.removeFromRight ((int) dotW) : dotArea.removeFromLeft ((int) dotW);
    if (on)
    {
        g.setColour (col::glow.withAlpha (0.35f));
        g.fillRoundedRectangle (dot.expanded (3.0f), 5.0f);
    }
    g.setColour (on ? col::glow : col::creamDim.withAlpha (0.6f));
    g.fillRoundedRectangle (dot, 3.0f);

    g.setColour (on ? col::cream : col::creamDim);
    g.setFont (stencilFont (juce::jmin (12.5f, r.getHeight() * 0.62f)));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (5), juce::Justification::centredLeft);
}

//==============================================================================
void TurboLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                             const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    auto base = on ? col::rustMid : col::tealDeep;
    if (down) base = base.darker (0.3f);
    else if (highlighted) base = base.brighter (0.15f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 1.2f), 4.0f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? col::brassLight : col::brassDark);
    g.drawRoundedRectangle (r, 4.0f, on ? 1.6f : 1.1f);
}

void TurboLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                       bool, bool)
{
    g.setColour (b.getToggleState() ? col::cream
                                    : b.isEnabled() ? col::creamDim : col::creamDim.withAlpha (0.4f));
    g.setFont (stencilFont (juce::jmin (13.0f, (float) b.getHeight() * 0.55f)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void TurboLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                     int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (1.0f);
    g.setColour (col::tealDeep);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? col::brassLight : col::brassDark);
    g.drawRoundedRectangle (r, 4.0f, 1.2f);

    juce::Path arrow;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (col::brassLight);
    g.fillPath (arrow);
}

juce::Font TurboLookAndFeel::getComboBoxFont (juce::ComboBox&) { return stencilFont (13.0f, false); }
juce::Font TurboLookAndFeel::getPopupMenuFont()                { return stencilFont (14.0f, false); }
juce::Font TurboLookAndFeel::getLabelFont (juce::Label& l)     { return stencilFont ((float) l.getHeight() * 0.72f); }

void TurboLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent& comp,
                                   const juce::Point<float>& tip, const juce::Rectangle<float>& body)
{
    juce::Path p;
    p.addBubble (body.reduced (0.5f), body.getUnion (juce::Rectangle<float> (tip.x, tip.y, 1, 1)),
                 tip, 5.0f, juce::jmin (15.0f, body.getWidth() * 0.2f, body.getHeight() * 0.2f));
    g.setColour (comp.findColour (juce::BubbleComponent::backgroundColourId));
    g.fillPath (p);
    g.setColour (comp.findColour (juce::BubbleComponent::outlineColourId));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

} // namespace ttp
