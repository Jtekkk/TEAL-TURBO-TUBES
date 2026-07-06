// TURBO TUBES — plugin/ui/Meters.cpp

#include "Meters.h"

namespace ttp
{

void MeterComponent::refresh()
{
    bool changed = false;
    for (int c = 0; c < 2; ++c)
    {
        const float newRms = meter.getRmsDb (c);
        const float newPeak = meter.getPeakDb (c);
        if (std::fabs (newRms - rmsDb[c]) > 0.1f || std::fabs (newPeak - peakDb[c]) > 0.1f)
            changed = true;
        rmsDb[c] = newRms;
        peakDb[c] = newPeak;

        if (newPeak >= peakHoldDb[c] || ++peakHoldAge[c] > 60)   // hold ~2 s at 30 fps
        {
            peakHoldDb[c] = newPeak;
            peakHoldAge[c] = 0;
            changed = true;
        }
    }
    if (meter.consumeClip() && ! clipLatched)
    {
        clipLatched = true;
        changed = true;
    }
    if (changed)
        repaint();
}

float MeterComponent::dbToY (float db, juce::Rectangle<float> bar) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
    return bar.getBottom() - t * bar.getHeight();
}

void MeterComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // caption
    g.setColour (col::creamDim);
    g.setFont (stencilFont (11.0f));
    g.drawText (label, r.removeFromBottom (14.0f), juce::Justification::centred);

    // clip lamp
    auto lampRow = r.removeFromTop (12.0f);
    {
        auto lamp = lampRow.withSizeKeepingCentre (10.0f, 8.0f);
        g.setColour (clipLatched ? col::meterRed : col::tealDeep);
        g.fillRoundedRectangle (lamp, 2.0f);
        if (clipLatched)
        {
            g.setColour (col::meterRed.withAlpha (0.4f));
            g.fillRoundedRectangle (lamp.expanded (3.0f), 4.0f);
        }
        g.setColour (col::brassDark);
        g.drawRoundedRectangle (lamp, 2.0f, 1.0f);
    }
    r.removeFromTop (2.0f);

    // recessed window
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::brassDark);
    g.drawRoundedRectangle (r, 4.0f, 1.2f);

    auto inner = r.reduced (4.0f);
    const float scaleW = 22.0f;
    auto barsArea = inner.withTrimmedRight (scaleW);
    const float gap = 3.0f;
    const float barW = (barsArea.getWidth() - gap) * 0.5f;

    for (int c = 0; c < 2; ++c)
    {
        auto bar = barsArea.removeFromLeft (barW);
        if (c == 0) barsArea.removeFromLeft (gap);

        // RMS fill with level-mapped colour
        const float yRms = dbToY (rmsDb[c], bar);
        juce::ColourGradient fill (col::meterGreen, bar.getX(), bar.getBottom(),
                                   col::meterRed, bar.getX(), dbToY (kMaxDb, bar), false);
        fill.addColour (juce::jlimit (0.01, 0.99, (double) ((-18.0f - kMinDb) / (kMaxDb - kMinDb))), col::meterGreen);
        fill.addColour (juce::jlimit (0.01, 0.99, (double) ((-6.0f - kMinDb) / (kMaxDb - kMinDb))), col::meterAmber);
        g.setGradientFill (fill);
        g.fillRect (juce::Rectangle<float> (bar.getX(), yRms, bar.getWidth(), bar.getBottom() - yRms));

        // sample peak line + hold (hidden when at the floor)
        if (peakDb[c] > kMinDb + 0.5f)
        {
            g.setColour (col::cream.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (bar.getX(), dbToY (peakDb[c], bar), bar.getWidth(), 1.6f));
        }
        if (peakHoldDb[c] > kMinDb + 0.5f)
        {
            g.setColour (col::glowHot.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (bar.getX(), dbToY (peakHoldDb[c], bar), bar.getWidth(), 1.2f));
        }
    }

    // scale
    auto scale = inner.removeFromRight (scaleW);
    g.setFont (stencilFont (8.5f, false));
    for (float db = 0.0f; db >= kMinDb + 5.0f; db -= 6.0f)
    {
        const float y = dbToY (db, scale);
        const bool ref = std::fabs (db + 18.0f) < 0.1f;
        g.setColour (ref ? col::cream : col::creamDim.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (scale.getX(), y, ref ? 6.0f : 4.0f, ref ? 1.4f : 1.0f));
        g.drawText (juce::String ((int) db), (int) scale.getX() + 7, (int) y - 5, (int) scaleW - 7, 10,
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
void SagGauge::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    g.setColour (col::creamDim);
    g.setFont (stencilFont (11.0f));
    g.drawText ("SUPPLY", r.removeFromBottom (14.0f), juce::Justification::centred);

    // cream dial in a brass bezel
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0, 1.5f), 6.0f);
    g.setColour (col::brassDark);
    g.fillRoundedRectangle (r, 6.0f);
    auto dial = r.reduced (3.0f);
    g.setColour (juce::Colour (0xffe8dcc0));
    g.fillRoundedRectangle (dial, 4.0f);

    const juce::Point<float> pivot (dial.getCentreX(), dial.getBottom() - 4.0f);
    const float needleLen = dial.getHeight() * 0.78f;
    const float a0 = -0.85f, a1 = 0.85f;

    // scale arc + ticks
    {
        juce::Path arcP;
        arcP.addCentredArc (pivot.x, pivot.y, needleLen, needleLen, 0.0f, a0, a1, true);
        g.setColour (juce::Colour (0xff5a5142));
        g.strokePath (arcP, juce::PathStrokeType (1.2f));
        for (int i = 0; i <= 8; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / 8.0f;
            const auto p1 = pivot.getPointOnCircumference (needleLen, a);
            const auto p2 = pivot.getPointOnCircumference (needleLen - (i % 4 == 0 ? 6.0f : 3.5f), a);
            g.drawLine ({ p1, p2 }, i % 4 == 0 ? 1.3f : 0.9f);
        }
        // red zone at the top of the range
        juce::Path red;
        red.addCentredArc (pivot.x, pivot.y, needleLen, needleLen, 0.0f,
                           a0 + (a1 - a0) * 0.75f, a1, true);
        g.setColour (col::meterRed.withAlpha (0.8f));
        g.strokePath (red, juce::PathStrokeType (2.4f));
    }

    // needle (ballistic smoothing for an analog feel)
    needle += 0.35f * (sag - needle);
    {
        const float a = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, needle);
        const auto tip = pivot.getPointOnCircumference (needleLen - 2.0f, a);
        g.setColour (juce::Colour (0xff9c2f1e));
        g.drawLine ({ pivot, tip }, 1.8f);
        g.setColour (juce::Colour (0xff3a2c1c));
        g.fillEllipse (pivot.x - 3.5f, pivot.y - 3.5f, 7.0f, 7.0f);
    }

    // auto-gain readout
    g.setColour (juce::Colour (0xff5a5142));
    g.setFont (stencilFont (9.0f, false));
    g.drawText ((comp >= 0.0f ? "+" : "") + juce::String (comp, 1) + " dB",
                dial.removeFromTop (12.0f), juce::Justification::centred);
}

} // namespace ttp
