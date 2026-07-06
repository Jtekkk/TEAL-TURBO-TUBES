// TURBO TUBES — plugin/PluginEditor.h

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/TurboLookAndFeel.h"
#include "ui/Background.h"
#include "ui/TubeBank.h"
#include "ui/Meters.h"

namespace ttp
{

//==============================================================================
// Knob + engraved label + value bubble.
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (juce::AudioProcessorValueTreeState& apvts, const char* paramID,
                 const juce::String& labelText, juce::Component* popupParent);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// Two-state button bound to a 2-choice parameter; shows the current choice.
class ChoiceButton : public juce::TextButton
{
public:
    ChoiceButton (juce::RangedAudioParameter& param, juce::UndoManager* um,
                  juce::String offText, juce::String onText);

private:
    juce::String offLabel, onLabel;
    juce::ParameterAttachment attachment;
};

//==============================================================================
// The big red TURBO lever.
class TurboSwitch : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    TurboSwitch (juce::RangedAudioParameter& param, juce::UndoManager* um);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::ParameterAttachment attachment;
    bool on = false;
};

//==============================================================================
// Snapshot button: click = switch, right-click = copy current state into slot.
class SnapButton : public juce::TextButton
{
public:
    SnapButton (TurboTubesProcessor& proc, int slotIndex);
    void mouseDown (const juce::MouseEvent&) override;

private:
    TurboTubesProcessor& processor;
    int slot;
};

//==============================================================================
class TurboTubesEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TurboTubesEditor (TurboTubesProcessor&);
    ~TurboTubesEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    void layoutContent();

    TurboTubesProcessor& processor;
    TurboLookAndFeel lnf;

    juce::Component content;
    Background background;
    TubeBank tubeBank;
    MeterComponent meterIn, meterOut;
    SagGauge gauge;

    LabeledKnob drive, bias, sag, inertia, variance, tilt, lowCut, highCut,
                mix, width, inTrim, outTrim, scHp, scLp, msBal;

    std::unique_ptr<TurboSwitch> turbo;
    juce::ToggleButton autoGainBtn { "AUTO GAIN" }, deltaBtn { "DELTA" },
                       scExtBtn { "EXT SC" }, bypassBtn { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        autoGainAtt, deltaAtt, scExtAtt, bypassAtt;

    std::unique_ptr<ChoiceButton> msModeBtn, qualityBtn;

    juce::TextButton osButtons[5];
    std::unique_ptr<juce::ParameterAttachment> osAttachment;
    int osIndex = 2;

    std::unique_ptr<SnapButton> snapButtons[4];
    juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, reseedBtn { "RESEED" };
    juce::ComboBox presetBox;

    juce::Label footer;
    juce::TooltipWindow tooltips { this, 400 };

    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TurboTubesEditor)
};

} // namespace ttp

using TurboTubesEditor = ttp::TurboTubesEditor;
