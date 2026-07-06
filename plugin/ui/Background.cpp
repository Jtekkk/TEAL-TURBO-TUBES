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

    // The photo IS the whole faceplate — every control mounts on the box.
    if (photo.isValid())
        g.drawImage (photo, b, juce::RectanglePlacement::stretchToFit, false);
    else
        g.fillAll (col::tealDark);

    // Soft darkening over the box's front face (lower ~58%) so the mounted
    // knobs, meters and switches read cleanly against the weathered paint.
    const float faceTop = b.getHeight() * 0.42f;
    juce::ColourGradient wash (juce::Colours::transparentBlack, b.getCentreX(), faceTop,
                               juce::Colour (0x74000000), b.getCentreX(), b.getBottom(), false);
    wash.addColour (0.30, juce::Colour (0x38000000));
    g.setGradientFill (wash);
    g.fillRect (b.withTop (faceTop));
}

} // namespace ttp
