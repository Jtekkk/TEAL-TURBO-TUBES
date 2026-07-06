// TURBO TUBES — plugin/ui/Meters.h
// Honest metering: sample peak (line) + AES-17 RMS (bar) per channel, dB scale
// with the -18 dBFS reference tick called out, latching clip lamp (click to
// clear). Plus the SUPPLY gauge — an analog needle showing how hard the
// virtual power supply is sagging.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TurboLookAndFeel.h"
#include "../../dsp/Meters.h"

namespace ttp
{

class MeterComponent : public juce::Component
{
public:
    MeterComponent (tt::StereoMeter& meterToShow, juce::String labelText)
        : meter (meterToShow), label (std::move (labelText)) {}

    void refresh();                       // called from the editor's timer
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clipLatched = false; repaint(); }

private:
    float dbToY (float db, juce::Rectangle<float> bar) const;

    tt::StereoMeter& meter;
    juce::String label;
    float rmsDb[2] = { -80.0f, -80.0f }, peakDb[2] = { -80.0f, -80.0f };
    float peakHoldDb[2] = { -80.0f, -80.0f };
    int   peakHoldAge[2] = {};
    bool clipLatched = false;

    static constexpr float kMinDb = -42.0f, kMaxDb = 3.0f;
};

//==============================================================================
class SagGauge : public juce::Component
{
public:
    void setValues (float sagAmount, float compDb)
    {
        if (std::fabs (sagAmount - sag) > 0.002f || std::fabs (compDb - comp) > 0.05f)
        {
            sag = sagAmount;
            comp = compDb;
            repaint();
        }
    }

    void paint (juce::Graphics&) override;

private:
    float sag = 0.0f, comp = 0.0f, needle = 0.0f;
};

} // namespace ttp
