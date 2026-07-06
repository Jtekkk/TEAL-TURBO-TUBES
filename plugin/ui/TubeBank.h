// TURBO TUBES — plugin/ui/TubeBank.h
// Sits over the top of the faceplate photo, where the five real bottles are.
// Each bottle is a model selector: click to choose. The selected tube gets a
// warm glow halo (intensity tracks the engine envelope) and a highlighted
// name tag on the box lip; the others show dim tags.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TurboLookAndFeel.h"
#include "../../dsp/TubeModels.h"

namespace ttp
{

class TubeBank : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    TubeBank (juce::RangedAudioParameter& tubeParam, juce::UndoManager* um);

    void setGlow (float amount) noexcept { glow = amount; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    // x-centres of the five real bottles, as fractions of the photo width.
    static constexpr float kCentre[tt::kNumTubeModels] = { 0.257f, 0.381f, 0.504f, 0.635f, 0.762f };

    float centreX (int i) const noexcept { return kCentre[i] * (float) getWidth(); }
    juce::Rectangle<float> hitBox (int i) const;
    int tubeIndexAt (juce::Point<float>) const;

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    int selected = 1, hovered = -1;
    float glow = 0.0f;
    juce::Random flickerRnd;
    float flicker = 0.0f;
};

} // namespace ttp
