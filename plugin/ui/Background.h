// TURBO TUBES — plugin/ui/Background.h
// The faceplate is the user's own photograph of the tube box, embedded in the
// binary. It is drawn to fill the panel; a soft legibility wash sits over the
// box's front face so the mounted controls stay readable.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TurboLookAndFeel.h"

namespace ttp
{

class Background : public juce::Component
{
public:
    Background();

    void paint (juce::Graphics& g) override;

private:
    juce::Image photo;
};

} // namespace ttp
