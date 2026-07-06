// TURBO TUBES — plugin/PluginEditor.cpp

#include "PluginEditor.h"
#include "Presets.h"

namespace ttp
{

// The faceplate is the photo (1168 x 880) and nothing else — every control is
// mounted on the box itself.
static constexpr int kW = 1168, kH = 880;

//==============================================================================
LabeledKnob::LabeledKnob (juce::AudioProcessorValueTreeState& apvts, const char* paramID,
                          const juce::String& labelText, juce::Component* popupParent)
    : label (labelText)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    // Parent the drag bubble inside the (scaled) content so it inherits this
    // editor's look-and-feel — no global default LnF, no cross-instance state.
    slider.setPopupDisplayEnabled (true, false, popupParent);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, slider);

    if (auto* p = apvts.getParameter (paramID))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (p->getName (64));
    }
}

void LabeledKnob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (15));
}

void LabeledKnob::paint (juce::Graphics& g)
{
    g.setColour (col::creamDim);
    g.setFont (stencilFont (11.5f));
    g.drawText (label, getLocalBounds().removeFromBottom (14), juce::Justification::centred);
}

//==============================================================================
ChoiceButton::ChoiceButton (juce::RangedAudioParameter& param, juce::UndoManager* um,
                            juce::String offText, juce::String onText)
    : offLabel (std::move (offText)), onLabel (std::move (onText)),
      attachment (param, [this] (float v)
                  {
                      const bool on = v > 0.5f;
                      setToggleState (on, juce::dontSendNotification);
                      setButtonText (on ? onLabel : offLabel);
                  }, um)
{
    setClickingTogglesState (false);
    onClick = [this] { attachment.setValueAsCompleteGesture (getToggleState() ? 0.0f : 1.0f); };
    attachment.sendInitialUpdate();
}

//==============================================================================
TurboSwitch::TurboSwitch (juce::RangedAudioParameter& param, juce::UndoManager* um)
    : attachment (param, [this] (float v) { on = v > 0.5f; repaint(); }, um)
{
    attachment.sendInitialUpdate();
    setTooltip ("TURBO: cascade into a second, hotter tube stage");
}

void TurboSwitch::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void TurboSwitch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);

    // red warning plate
    auto plateCol = on ? juce::Colour (0xff8e2f1e) : juce::Colour (0xff5a2418);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (r.translated (0, 2.0f), 8.0f);
    juce::ColourGradient pg (plateCol.brighter (0.25f), r.getX(), r.getY(),
                             plateCol.darker (0.2f), r.getX(), r.getBottom(), false);
    g.setGradientFill (pg);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (on ? col::glowHot : col::brassDark);
    g.drawRoundedRectangle (r, 8.0f, on ? 2.0f : 1.2f);

    if (on)
    {
        g.setColour (col::glow.withAlpha (0.25f));
        g.fillRoundedRectangle (r.expanded (4.0f), 10.0f);
    }

    // lever slot + lever
    auto slot = r.reduced (r.getWidth() * 0.34f, r.getHeight() * 0.22f);
    slot = slot.withWidth (juce::jmin (slot.getWidth(), 16.0f)).withCentre ({ r.getCentreX() - r.getWidth() * 0.22f, r.getCentreY() });
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (slot, 5.0f);

    const float knobY = on ? slot.getY() + 7.0f : slot.getBottom() - 7.0f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (slot.getCentreX() - 8.0f, knobY - 7.0f, 16.0f, 16.0f);
    juce::ColourGradient kg (col::brassLight, slot.getCentreX() - 4.0f, knobY - 5.0f,
                             col::brassDark, slot.getCentreX() + 5.0f, knobY + 6.0f, true);
    g.setGradientFill (kg);
    g.fillEllipse (slot.getCentreX() - 7.5f, knobY - 7.5f, 15.0f, 15.0f);

    // label
    g.setColour (on ? col::cream : col::creamDim);
    g.setFont (stencilFont (juce::jmin (20.0f, r.getHeight() * 0.34f)));
    g.drawText ("TURBO", r.withTrimmedLeft (r.getWidth() * 0.32f), juce::Justification::centred);
}

//==============================================================================
SnapButton::SnapButton (TurboTubesProcessor& proc, int slotIndex)
    : juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + slotIndex))),
      processor (proc), slot (slotIndex)
{
    setClickingTogglesState (false);
    setTooltip ("Snapshot " + getButtonText() + " — click to switch, right-click to copy the current sound here");
}

void SnapButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
        processor.copyActiveSnapshotTo (slot);
    else
        processor.switchToSnapshot (slot);
    juce::TextButton::mouseDown (e);
}

//==============================================================================
TurboTubesEditor::TurboTubesEditor (TurboTubesProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      tubeBank (*p.apvts.getParameter (id::tube), &p.undoManager),
      meterIn (p.engine.meterIn, "IN"),
      meterOut (p.engine.meterOut, "OUT"),
      drive   (p.apvts, id::drive,   "DRIVE",    &content),
      bias    (p.apvts, id::bias,    "BIAS",     &content),
      sag     (p.apvts, id::sag,     "SAG",      &content),
      inertia (p.apvts, id::inertia, "INERTIA",  &content),
      variance(p.apvts, id::variance,"VARIANCE", &content),
      tilt    (p.apvts, id::tilt,    "TILT",     &content),
      lowCut  (p.apvts, id::lowCut,  "LOW CUT",  &content),
      highCut (p.apvts, id::highCut, "HIGH CUT", &content),
      mix     (p.apvts, id::mix,     "MIX",      &content),
      width   (p.apvts, id::width,   "WIDTH",    &content),
      inTrim  (p.apvts, id::inTrim,  "IN TRIM",  &content),
      outTrim (p.apvts, id::outTrim, "OUT TRIM", &content),
      scHp    (p.apvts, id::scHp,    "SC LO",    &content),
      scLp    (p.apvts, id::scLp,    "SC HI",    &content),
      msBal   (p.apvts, id::msBal,   "M/S TILT", &content)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (content);
    content.addAndMakeVisible (background);

    for (auto* k : { &drive, &bias, &sag, &inertia, &variance, &tilt, &lowCut, &highCut,
                     &mix, &width, &inTrim, &outTrim, &scHp, &scLp, &msBal })
        content.addAndMakeVisible (*k);

    content.addAndMakeVisible (tubeBank);
    content.addAndMakeVisible (meterIn);
    content.addAndMakeVisible (meterOut);
    content.addAndMakeVisible (gauge);

    turbo = std::make_unique<TurboSwitch> (*processor.apvts.getParameter (id::turbo), &processor.undoManager);
    content.addAndMakeVisible (*turbo);

    auto attach = [&] (juce::ToggleButton& b, const char* pid,
                       std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att,
                       const char* tip)
    {
        content.addAndMakeVisible (b);
        b.setTooltip (tip);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, pid, b);
    };
    attach (autoGainBtn, id::autoGain, autoGainAtt, "Matched-loudness compensation: DRIVE changes tone, not level");
    attach (deltaBtn, id::delta, deltaAtt, "Listen to exactly what the tubes add (wet minus dry)");
    attach (scExtBtn, id::scExt, scExtAtt, "Drive the tube dynamics from the external sidechain input");
    attach (bypassBtn, id::bypass, bypassAtt, "Latency-aligned, click-free bypass");

    msModeBtn = std::make_unique<ChoiceButton> (*processor.apvts.getParameter (id::procMode),
                                                &processor.undoManager, "STEREO", "MID/SIDE");
    msModeBtn->setTooltip ("Process left/right or mid/side (M/S TILT shifts drive between mid and side)");
    content.addAndMakeVisible (*msModeBtn);

    qualityBtn = std::make_unique<ChoiceButton> (*processor.apvts.getParameter (id::osQuality),
                                                 &processor.undoManager, "PUNCHY", "PRISTINE");
    qualityBtn->setTooltip ("Oversampling filters: PUNCHY = short/low-latency, PRISTINE = long/flat to 20 kHz");
    content.addAndMakeVisible (*qualityBtn);

    // OS selector
    {
        auto* osParam = processor.apvts.getParameter (id::os);
        osAttachment = std::make_unique<juce::ParameterAttachment> (*osParam,
            [this] (float v)
            {
                osIndex = juce::jlimit (0, 4, (int) std::lround (v));
                for (int i = 0; i < 5; ++i)
                    osButtons[i].setToggleState (i == osIndex, juce::dontSendNotification);
            }, &processor.undoManager);

        static const char* names[5] = { "1X", "2X", "4X", "8X", "16X" };
        for (int i = 0; i < 5; ++i)
        {
            osButtons[i].setButtonText (names[i]);
            osButtons[i].setClickingTogglesState (false);
            osButtons[i].onClick = [this, i] { osAttachment->setValueAsCompleteGesture ((float) i); };
            osButtons[i].setTooltip ("Oversampling factor (higher = cleaner at high drive, more CPU)");
            content.addAndMakeVisible (osButtons[i]);
        }
        osAttachment->sendInitialUpdate();
    }

    // snapshots / undo
    for (int i = 0; i < 4; ++i)
    {
        snapButtons[i] = std::make_unique<SnapButton> (processor, i);
        content.addAndMakeVisible (*snapButtons[i]);
    }
    undoBtn.onClick = [this] { processor.undoManager.undo(); };
    redoBtn.onClick = [this] { processor.undoManager.redo(); };
    undoBtn.setTooltip ("Undo parameter change");
    redoBtn.setTooltip ("Redo");
    content.addAndMakeVisible (undoBtn);
    content.addAndMakeVisible (redoBtn);

    // presets
    {
        const auto& presets = factoryPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            presetBox.addItem (presets[(size_t) i].name, i + 1);
        presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
        presetBox.setTextWhenNothingSelected ("— presets —");
        presetBox.onChange = [this]
        {
            const int idx = presetBox.getSelectedId() - 1;
            if (idx >= 0 && idx != processor.getCurrentProgram())
                processor.setCurrentProgram (idx);
        };
        prevPreset.onClick = [this]
        {
            const int n = (int) factoryPresets().size();
            processor.setCurrentProgram ((processor.getCurrentProgram() + n - 1) % n);
        };
        nextPreset.onClick = [this]
        {
            const int n = (int) factoryPresets().size();
            processor.setCurrentProgram ((processor.getCurrentProgram() + 1) % n);
        };
        content.addAndMakeVisible (presetBox);
        content.addAndMakeVisible (prevPreset);
        content.addAndMakeVisible (nextPreset);
    }

    reseedBtn.onClick = [this] { processor.reseedDrift(); };
    reseedBtn.setTooltip ("Roll a new set of component tolerances (VARIANCE sets how much they matter)");
    content.addAndMakeVisible (reseedBtn);

    footer.setJustificationType (juce::Justification::centredLeft);
    footer.setInterceptsMouseClicks (false, false);
    content.addAndMakeVisible (footer);

    layoutContent();

    setResizable (true, true);
    setResizeLimits (700, 527, 1752, 1320);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kW / (double) kH);
    setSize (1000, 753);   // ~0.86x of the logical canvas — a comfortable default

    startTimerHz (30);
}

TurboTubesEditor::~TurboTubesEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void TurboTubesEditor::layoutContent()
{
    content.setBounds (0, 0, kW, kH);
    background.setBounds (0, 0, kW, kH);

    // Place a rotary so its knob face is centred at (cx, cy) with diameter D.
    auto knob = [] (LabeledKnob& k, int cx, int cy, int D)
    {
        k.setBounds (cx - D / 2, cy - D / 2, D, D + 14);
    };

    // ---- the five bottles across the top ARE the model selectors
    tubeBank.setBounds (0, 0, kW, 414);

    // ---- meters flank the box face
    meterIn.setBounds (54, 430, 78, 196);
    meterOut.setBounds (1036, 430, 78, 196);

    // ---- main knobs, all mounted on the box face
    // dynamics (left)                     tone / mix (right)
    knob (bias,     190, 470, 88);         knob (tilt,    880, 470, 88);
    knob (sag,      294, 470, 88);         knob (mix,     984, 470, 88);
    knob (variance, 190, 576, 88);         knob (lowCut,  880, 576, 88);
    knob (inertia,  294, 576, 88);         knob (highCut, 984, 576, 88);

    // drive + supply + width + turbo (centre)
    knob (drive, 584, 466, 128);
    gauge.setBounds (486, 560, 92, 92);
    knob (width, 676, 566, 84);
    turbo->setBounds (500, 648, 168, 46);

    // ---- trims + sidechain: a lower knob row, still on the box
    knob (inTrim,  108, 692, 70);
    knob (scHp,    214, 692, 70);
    knob (scLp,    312, 692, 70);
    knob (msBal,   904, 692, 70);
    knob (outTrim, 1060, 692, 70);

    // ---- compact control bar across the bottom of the box face
    const int bh = 26;
    int y1 = 748;                          // presets / snapshots / oversampling
    undoBtn.setBounds (28, y1, 52, bh);
    redoBtn.setBounds (84, y1, 52, bh);
    for (int i = 0; i < 4; ++i)
        snapButtons[i]->setBounds (150 + i * 36, y1, 32, bh);
    prevPreset.setBounds (300, y1, 26, bh);
    presetBox.setBounds (330, y1, 232, bh);
    nextPreset.setBounds (566, y1, 26, bh);
    for (int i = 0; i < 5; ++i)
        osButtons[i].setBounds (606 + i * 44, y1, 40, bh);

    int y2 = 780;                          // processing toggles / quality / reseed
    autoGainBtn.setBounds (28, y2, 124, bh);
    deltaBtn.setBounds (158, y2, 84, bh);
    bypassBtn.setBounds (248, y2, 96, bh);
    msModeBtn->setBounds (350, y2, 104, bh);
    scExtBtn.setBounds (460, y2, 92, bh);
    qualityBtn->setBounds (606, y2, 108, bh);
    reseedBtn.setBounds (720, y2, 80, bh);

    // ---- identity + honest latency on the wooden lip below the box
    footer.setBounds (330, 828, 512, 26);
}

void TurboTubesEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) kW,
                                    (float) getHeight() / (float) kH);
    content.setTransform (juce::AffineTransform::scale (scale));
}

//==============================================================================
void TurboTubesEditor::timerCallback()
{
    meterIn.refresh();
    meterOut.refresh();
    tubeBank.setGlow (processor.engine.glowAmount.load (std::memory_order_relaxed));
    tubeBank.repaint();
    gauge.setValues (processor.engine.sagAmount.load (std::memory_order_relaxed),
                     processor.engine.compGainDb.load (std::memory_order_relaxed));

    // snapshot highlight + preset sync
    for (int i = 0; i < 4; ++i)
        snapButtons[i]->setToggleState (processor.getActiveSnapshot() == i, juce::dontSendNotification);
    if (presetBox.getSelectedId() != processor.getCurrentProgram() + 1)
        presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);

    undoBtn.setEnabled (processor.undoManager.canUndo());
    redoBtn.setEnabled (processor.undoManager.canRedo());

    // footer: identity + honest latency readout
    if ((timerTicks & 7) == 0)
    {
        const int lat = processor.getLatencySamples();
        const double ms = 1000.0 * lat / std::max (44100.0, processor.getSampleRate());
        footer.setText (juce::String ("TURBO TUBES | TEAL        LATENCY ")
                            + juce::String (lat) + " smp / " + juce::String (ms, 2) + " ms",
                        juce::dontSendNotification);
        footer.setFont (stencilFont (12.0f, false));
        footer.setColour (juce::Label::textColourId, col::creamDim.withAlpha (0.8f));
    }

    // coalesce knob wiggles into undo transactions roughly once a second
    if (++timerTicks % 30 == 0 && ! juce::Desktop::getInstance().getMainMouseSource().isDragging())
        processor.undoManager.beginNewTransaction();
}

} // namespace ttp
