// TURBO TUBES — plugin/ui/Background.h
// The box itself: chipped teal paint over rusted steel, riveted edges, a dark
// shelf for the tube bank. Painted procedurally once into a cached image
// (seeded, so every instance of the plugin looks like the same battered unit).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TurboLookAndFeel.h"

namespace ttp
{

class Background : public juce::Component
{
public:
    Background() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override;

private:
    void renderPanel (int w, int h);
    juce::Image cache;
};

} // namespace ttp
