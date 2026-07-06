// TURBO TUBES — plugin/ui/Background.cpp

#include "Background.h"
#include "BinaryData.h"

namespace ttp
{

Background::Background()
{
    setInterceptsMouseClicks (false, false);
    photo = juce::ImageCache::getFromMemory (BinaryData::turbo_box_jpg,
                                             BinaryData::turbo_box_jpgSize);
}

void Background::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // The photo keeps its native 1168:880 aspect at the top; a teal rack strip
    // fills whatever remains below it for the setup controls.
    const float photoH = b.getWidth() * (880.0f / 1168.0f);

    if (photo.isValid())
        g.drawImage (photo, b.withHeight (photoH),
                     juce::RectanglePlacement::stretchToFit, false);
    else
    {
        g.setColour (col::tealDark);
        g.fillRect (b.withHeight (photoH));
    }

    // Soft darkening over the box's front face (lower part of the photo) so
    // knob labels and meters read cleanly against the weathered paint.
    const float faceTop = photoH * 0.44f;
    juce::ColourGradient wash (juce::Colours::transparentBlack, b.getCentreX(), faceTop,
                               juce::Colour (0x66000000), b.getCentreX(), photoH, false);
    wash.addColour (0.25, juce::Colour (0x30000000));
    g.setGradientFill (wash);
    g.fillRect (juce::Rectangle<float> (0, faceTop, b.getWidth(), photoH - faceTop));

    // ---- rack strip below the photo: brushed teal steel with a lip + rivets
    if (photoH < b.getHeight())
    {
        auto strip = b.withTop (photoH);
        juce::ColourGradient sg (col::tealDark, strip.getX(), strip.getY(),
                                 col::tealDeep, strip.getX(), strip.getBottom(), false);
        sg.addColour (0.5, col::tealMid.darker (0.15f));
        g.setGradientFill (sg);
        g.fillRect (strip);

        // top lip where the box meets the rack
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (strip.getX(), strip.getY(), strip.getWidth(), 3.0f);
        g.setColour (col::tealLight.withAlpha (0.5f));
        g.fillRect (strip.getX(), strip.getY() + 3.0f, strip.getWidth(), 1.5f);

        // faint brushed streaks
        juce::Random rnd (0x5713);
        g.setColour (juce::Colours::black.withAlpha (0.06f));
        for (int i = 0; i < 40; ++i)
        {
            const float x = strip.getX() + rnd.nextFloat() * strip.getWidth();
            g.fillRect (x, strip.getY() + 6.0f, 0.8f, strip.getHeight() - 8.0f);
        }

        // corner rivets
        auto rivet = [&] (float cx, float cy)
        {
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillEllipse (cx - 5.0f, cy - 4.0f, 10.0f, 10.0f);
            g.setColour (col::brass.brighter (0.1f));
            g.fillEllipse (cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.fillEllipse (cx - 3.0f, cy - 3.5f, 3.5f, 3.5f);
        };
        const float m = 14.0f;
        rivet (strip.getX() + m,               strip.getY() + m);
        rivet (strip.getRight() - m,           strip.getY() + m);
        rivet (strip.getX() + m,               strip.getBottom() - m);
        rivet (strip.getRight() - m,           strip.getBottom() - m);
    }
}

} // namespace ttp
