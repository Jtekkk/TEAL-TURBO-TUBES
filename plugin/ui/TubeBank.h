// TURBO TUBES — plugin/ui/TubeBank.h
// The five bottles on top of the box. Click a tube to select its model; the
// selected tube glows with the programme (glow intensity comes from the
// engine's envelope telemetry), the others idle at pilot-light level.

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
    int tubeIndexAt (juce::Point<float> pos) const;
    juce::Rectangle<float> tubeArea (int index) const;
    void drawTube (juce::Graphics&, int index, juce::Rectangle<float> area,
                   float glowAmount, bool selected, bool hovered);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    int selected = 1, hovered = -1;
    float glow = 0.0f;
    juce::Random flickerRnd;
    float flicker[tt::kNumTubeModels] = {};
};

} // namespace ttp
