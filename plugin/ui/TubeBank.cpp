// TURBO TUBES — plugin/ui/TubeBank.cpp

#include "TubeBank.h"

namespace ttp
{

constexpr float TubeBank::kCentre[];

TubeBank::TubeBank (juce::RangedAudioParameter& tubeParam, juce::UndoManager* um)
    : param (tubeParam),
      attachment (tubeParam, [this] (float v)
                  {
                      selected = juce::jlimit (0, tt::kNumTubeModels - 1, (int) std::lround (v));
                      repaint();
                  }, um)
{
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> TubeBank::hitBox (int i) const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float slot = w / (float) tt::kNumTubeModels;
    return { centreX (i) - slot * 0.42f, 6.0f, slot * 0.84f, h - 10.0f };
}

int TubeBank::tubeIndexAt (juce::Point<float> pos) const
{
    for (int i = 0; i < tt::kNumTubeModels; ++i)
        if (hitBox (i).contains (pos))
            return i;
    return -1;
}

void TubeBank::mouseDown (const juce::MouseEvent& e)
{
    const int idx = tubeIndexAt (e.position);
    if (idx >= 0)
        attachment.setValueAsCompleteGesture ((float) idx);
}

void TubeBank::mouseMove (const juce::MouseEvent& e)
{
    const int idx = tubeIndexAt (e.position);
    if (idx != hovered)
    {
        hovered = idx;
        setTooltip (idx >= 0 ? juce::String (tt::tubeModel (idx).tagline) : juce::String());
        repaint();
    }
}

void TubeBank::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void TubeBank::paint (juce::Graphics& g)
{
    const float h = (float) getHeight();
    const float glowY   = h * 0.50f;   // roughly the lit centre of the bottles
    const float tagY    = h - 27.0f;   // on the box lip, just under the sockets
    const float slot    = (float) getWidth() / (float) tt::kNumTubeModels;

    // gentle shimmer so the live tube feels alive
    flicker += (flickerRnd.nextFloat() - 0.5f) * 0.2f;
    flicker = juce::jlimit (-0.1f, 0.1f, flicker * 0.9f);

    for (int i = 0; i < tt::kNumTubeModels; ++i)
    {
        const float cx = centreX (i);
        const bool sel = i == selected;
        const bool hov = i == hovered;

        // --- glow halo over the selected bottle (tracks the signal)
        if (sel)
        {
            const float amt = juce::jlimit (0.12f, 1.0f, 0.28f + 0.72f * glow + flicker);
            const float r = slot * 0.62f;
            juce::ColourGradient halo (col::glow.withAlpha (0.55f * amt), cx, glowY,
                                       col::glow.withAlpha (0.0f), cx, glowY - r, true);
            halo.point2 = { cx + r, glowY };
            g.setGradientFill (halo);
            g.fillEllipse (cx - r, glowY - r, r * 2.0f, r * 2.0f);

            // hotter core
            const float rc = r * 0.45f;
            g.setColour (col::glowHot.withAlpha (0.45f * amt));
            g.fillEllipse (cx - rc, glowY - rc * 1.3f, rc * 2.0f, rc * 2.6f);
        }
        else if (hov)
        {
            const float r = slot * 0.5f;
            g.setColour (col::cream.withAlpha (0.10f));
            g.fillEllipse (cx - r, glowY - r, r * 2.0f, r * 2.0f);
        }

        // --- name tag on the box lip
        const float tw = juce::jmin (slot * 0.86f, 104.0f), th = 26.0f;
        auto tag = juce::Rectangle<float> (cx - tw * 0.5f, tagY, tw, th);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (tag.translated (0, 1.5f), 5.0f);
        g.setColour (sel ? col::tealMid : juce::Colour (0xd0143f3b));
        g.fillRoundedRectangle (tag, 5.0f);
        g.setColour (sel ? col::glowHot : (hov ? col::brassLight : col::brassDark));
        g.drawRoundedRectangle (tag, 5.0f, sel ? 2.0f : 1.1f);

        g.setColour (sel ? col::cream : col::creamDim);
        g.setFont (stencilFont (13.0f));
        g.drawText (tt::tubeModel (i).name, tag, juce::Justification::centred);

        if (sel)
        {
            g.setColour (col::glow.withAlpha (0.95f));
            g.fillRect (cx - 15.0f, tag.getBottom() + 2.0f, 30.0f, 2.5f);
        }
    }
}

} // namespace ttp
