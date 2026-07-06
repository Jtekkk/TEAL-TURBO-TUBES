// TURBO TUBES — plugin/ui/TubeBank.cpp

#include "TubeBank.h"

namespace ttp
{

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

int TubeBank::tubeIndexAt (juce::Point<float> pos) const
{
    for (int i = 0; i < tt::kNumTubeModels; ++i)
        if (tubeArea (i).expanded (6.0f).contains (pos))
            return i;
    return -1;
}

juce::Rectangle<float> TubeBank::tubeArea (int index) const
{
    const float w = (float) getWidth();
    const float slot = w / (float) tt::kNumTubeModels;
    const float tubeW = juce::jmin (slot * 0.62f, 86.0f);
    const float cx = slot * ((float) index + 0.5f);
    return { cx - tubeW * 0.5f, 4.0f, tubeW, (float) getHeight() - 24.0f };
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
    for (int i = 0; i < tt::kNumTubeModels; ++i)
    {
        // per-tube slow flicker so the bank feels alive
        flicker[i] += (flickerRnd.nextFloat() - 0.5f) * 0.25f;
        flicker[i] = juce::jlimit (-0.12f, 0.12f, flicker[i] * 0.9f);

        const bool sel = i == selected;
        const float amount = sel ? juce::jlimit (0.12f, 1.0f, 0.25f + 0.75f * glow + flicker[i])
                                 : 0.10f + flicker[i] * 0.3f;
        drawTube (g, i, tubeArea (i), amount, sel, i == hovered);
    }
}

void TubeBank::drawTube (juce::Graphics& g, int index, juce::Rectangle<float> r,
                         float amount, bool isSel, bool hoveredNow)
{
    const float cx = r.getCentreX();
    const float baseH = r.getHeight() * 0.14f;
    auto glass = r.withTrimmedBottom (baseH);
    const float gw = glass.getWidth(), gh = glass.getHeight();
    const float gx = glass.getX(), gy = glass.getY();

    // ---------- glass silhouette per model (mirrors the reference photo)
    juce::Path body;
    switch (index)
    {
        case 0: // VT-101: shouldered ST bottle
        {
            const float neckW = gw * 0.42f, shoulderY = gy + gh * 0.34f;
            body.startNewSubPath (cx - neckW * 0.5f, gy + gh * 0.06f);
            body.quadraticTo (cx, gy - gh * 0.04f, cx + neckW * 0.5f, gy + gh * 0.06f);
            body.quadraticTo (cx + neckW * 0.62f, gy + gh * 0.2f, cx + gw * 0.46f, shoulderY);
            body.quadraticTo (cx + gw * 0.52f, gy + gh * 0.5f, cx + gw * 0.44f, gh + gy);
            body.lineTo (cx - gw * 0.44f, gh + gy);
            body.quadraticTo (cx - gw * 0.52f, gy + gh * 0.5f, cx - gw * 0.46f, shoulderY);
            body.quadraticTo (cx - neckW * 0.62f, gy + gh * 0.2f, cx - neckW * 0.5f, gy + gh * 0.06f);
            body.closeSubPath();
            break;
        }
        case 1: // G-300: globe
        {
            const float neckW = gw * 0.34f;
            body.startNewSubPath (cx - neckW * 0.5f, gy + gh * 0.04f);
            body.quadraticTo (cx, gy - gh * 0.05f, cx + neckW * 0.5f, gy + gh * 0.04f);
            body.quadraticTo (cx + gw * 0.62f, gy + gh * 0.28f, cx + gw * 0.5f, gy + gh * 0.66f);
            body.quadraticTo (cx + gw * 0.4f, gy + gh * 0.95f, cx + gw * 0.3f, gy + gh);
            body.lineTo (cx - gw * 0.3f, gy + gh);
            body.quadraticTo (cx - gw * 0.4f, gy + gh * 0.95f, cx - gw * 0.5f, gy + gh * 0.66f);
            body.quadraticTo (cx - gw * 0.62f, gy + gh * 0.28f, cx - neckW * 0.5f, gy + gh * 0.04f);
            body.closeSubPath();
            break;
        }
        case 2: // SL-6: squat dome with metal skirt
        {
            body.startNewSubPath (cx - gw * 0.30f, gy + gh * 0.10f);
            body.quadraticTo (cx, gy - gh * 0.02f, cx + gw * 0.30f, gy + gh * 0.10f);
            body.quadraticTo (cx + gw * 0.52f, gy + gh * 0.30f, cx + gw * 0.48f, gy + gh * 0.72f);
            body.lineTo (cx - gw * 0.48f, gy + gh * 0.72f);
            body.quadraticTo (cx - gw * 0.52f, gy + gh * 0.30f, cx - gw * 0.30f, gy + gh * 0.10f);
            body.closeSubPath();
            break;
        }
        default: // BB-88 / R-90: straight cylinders, domed top (R-90 adds a tip)
        {
            const float half = gw * 0.40f;
            body.startNewSubPath (cx - half, gy + gh * 0.14f);
            body.quadraticTo (cx, gy - gh * 0.02f, cx + half, gy + gh * 0.14f);
            body.lineTo (cx + half, gy + gh);
            body.lineTo (cx - half, gy + gh);
            body.closeSubPath();
            break;
        }
    }

    // hover / selection halo behind the glass
    if (isSel || hoveredNow)
    {
        g.setColour ((isSel ? col::glow : col::cream).withAlpha (isSel ? 0.20f : 0.10f));
        g.fillRoundedRectangle (r.expanded (5.0f, 3.0f), 10.0f);
    }

    // inner glow (behind the internals)
    {
        const auto glowCol = col::glow.withAlpha (juce::jlimit (0.0f, 1.0f, amount));
        juce::ColourGradient gg (glowCol, cx, gy + gh * 0.55f,
                                 glowCol.withAlpha (0.0f), cx, gy + gh * 0.05f, true);
        gg.point2 = { cx + gw * 0.8f, gy + gh * 0.55f };
        g.setGradientFill (gg);
        g.fillPath (body);
    }

    // internals: plate + rods + filament
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (body);

        const float plateW = gw * 0.34f, plateH = gh * 0.5f;
        auto plate = juce::Rectangle<float> (cx - plateW / 2, gy + gh * 0.28f, plateW, plateH);
        g.setColour (juce::Colour (0xff33302b).withAlpha (0.92f));
        g.fillRoundedRectangle (plate, 3.0f);
        g.setColour (juce::Colour (0xff211f1c));
        g.drawRoundedRectangle (plate, 3.0f, 1.2f);

        // filament glow slit
        g.setColour (col::glowHot.withAlpha (juce::jlimit (0.0f, 1.0f, 0.25f + amount * 0.75f)));
        g.fillRoundedRectangle (cx - 1.5f, plate.getY() + 3.0f, 3.0f, plate.getHeight() - 6.0f, 1.5f);

        // support rods
        g.setColour (juce::Colour (0xff777069).withAlpha (0.8f));
        g.drawLine (cx - plateW * 0.42f, plate.getBottom(), cx - plateW * 0.32f, gy + gh, 1.4f);
        g.drawLine (cx + plateW * 0.42f, plate.getBottom(), cx + plateW * 0.32f, gy + gh, 1.4f);
        // top mica disc
        g.setColour (juce::Colour (0xffbbb4a4).withAlpha (0.55f));
        g.fillEllipse (cx - plateW * 0.5f, gy + gh * 0.16f, plateW, gh * 0.05f);
    }

    // glass shading + rim
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (body);
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.30f), gx + gw * 0.22f, gy,
                                    juce::Colours::white.withAlpha (0.02f), gx + gw * 0.55f, gy + gh, false);
        g.setGradientFill (sheen);
        g.fillRect (juce::Rectangle<float> (gx, gy, gw * 0.38f, gh));
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (gx + gw * 0.72f, gy, gw * 0.28f, gh));
    }
    g.setColour (juce::Colours::white.withAlpha (isSel ? 0.75f : 0.45f));
    g.strokePath (body, juce::PathStrokeType (1.3f));

    // model-specific dressing
    if (index == 2) // silver perforated skirt
    {
        auto skirt = juce::Rectangle<float> (cx - gw * 0.48f, gy + gh * 0.72f, gw * 0.96f, gh * 0.28f);
        juce::ColourGradient metal (juce::Colour (0xffcfccc4), skirt.getX(), skirt.getY(),
                                    juce::Colour (0xff8d8a83), skirt.getRight(), skirt.getBottom(), false);
        g.setGradientFill (metal);
        g.fillRoundedRectangle (skirt, 2.5f);
        g.setColour (juce::Colour (0xff5f5c56));
        for (int hx = 0; hx < 7; ++hx)
            g.fillEllipse (skirt.getX() + 4 + hx * (skirt.getWidth() - 8) / 7.0f,
                           skirt.getCentreY() - 1.5f, 3.0f, 3.0f);
    }
    if (index == 3) // blue label band
    {
        auto band = juce::Rectangle<float> (cx - gw * 0.40f, gy + gh * 0.80f, gw * 0.80f, gh * 0.14f);
        g.setColour (juce::Colour (0xff3f7fae));
        g.fillRect (band);
        g.setColour (col::cream.withAlpha (0.9f));
        g.setFont (stencilFont (juce::jmin (9.0f, band.getHeight() * 0.62f), false));
        g.drawText ("BLUE 88", band, juce::Justification::centred);
    }
    if (index == 4) // red star
    {
        juce::Path star;
        star.addStar ({ cx, gy + gh * 0.5f }, 5, gw * 0.07f, gw * 0.16f, 0.0f);
        g.setColour (juce::Colour (0xffc84630).withAlpha (0.9f));
        g.fillPath (star);
    }
    if (index == 4) // nipple tip
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillEllipse (cx - 2.5f, gy - 3.0f, 5.0f, 6.0f);
    }

    // ---------- base
    {
        auto base = juce::Rectangle<float> (cx - gw * 0.34f, r.getBottom() - baseH, gw * 0.68f, baseH);
        const bool brown = index == 0;
        auto cTop = brown ? juce::Colour (0xff6b4632) : col::brassLight;
        auto cBot = brown ? juce::Colour (0xff42291c) : col::brassDark;
        juce::ColourGradient bg (cTop, base.getX(), base.getY(), cBot, base.getX(), base.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (base, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (base, 2.0f, 1.0f);
        // base sits on the shelf: contact shadow
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (cx - gw * 0.4f, r.getBottom() - 2.0f, gw * 0.8f, 4.0f);
    }

    // ---------- name plate
    {
        auto label = juce::Rectangle<float> (r.getX() - 8.0f, r.getBottom() + 2.0f, r.getWidth() + 16.0f, 15.0f);
        g.setColour (isSel ? col::cream : col::creamDim.withAlpha (0.85f));
        g.setFont (stencilFont (11.5f));
        g.drawText (tt::tubeModel (index).name, label, juce::Justification::centred);
        if (isSel)
        {
            g.setColour (col::glow.withAlpha (0.9f));
            g.fillRect (label.getCentreX() - 14.0f, label.getBottom() + 1.0f, 28.0f, 2.0f);
        }
    }
}

} // namespace ttp
