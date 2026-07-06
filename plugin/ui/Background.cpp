// TURBO TUBES — plugin/ui/Background.cpp

#include "Background.h"

namespace ttp
{

void Background::paint (juce::Graphics& g)
{
    if (cache.getWidth() != getWidth() || cache.getHeight() != getHeight())
        renderPanel (getWidth(), getHeight());
    g.drawImageAt (cache, 0, 0);
}

void Background::renderPanel (int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    cache = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (cache);
    juce::Random rnd (0x7EA1); // fixed seed: every unit is the same battered box
    const float fw = (float) w, fh = (float) h;

    // ---- base teal steel with a soft top light
    {
        juce::ColourGradient base (col::tealMid.brighter (0.10f), fw * 0.35f, 0.0f,
                                   col::tealDark, fw * 0.6f, fh, false);
        base.addColour (0.45, col::tealMid);
        g.setGradientFill (base);
        g.fillAll();
    }

    // ---- large mottled tonal variation (weathered paint)
    for (int i = 0; i < 26; ++i)
    {
        const float cx = rnd.nextFloat() * fw, cy = rnd.nextFloat() * fh;
        const float rr = 60.0f + rnd.nextFloat() * 190.0f;
        auto c = (rnd.nextBool() ? col::tealLight : col::tealDeep).withAlpha (0.05f + rnd.nextFloat() * 0.07f);
        juce::ColourGradient blob (c, cx, cy, c.withAlpha (0.0f), cx + rr, cy + rr * 0.6f, true);
        g.setGradientFill (blob);
        g.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
    }

    // ---- rust patches: clustered, denser near edges and bottom
    auto rustPatch = [&] (float cx, float cy, float scale)
    {
        const int layers = 4 + rnd.nextInt (4);
        for (int l = 0; l < layers; ++l)
        {
            const float rr = scale * (0.35f + rnd.nextFloat());
            const float ox = (rnd.nextFloat() - 0.5f) * scale * 1.6f;
            const float oy = (rnd.nextFloat() - 0.5f) * scale * 1.2f;
            juce::Colour rc = (l % 3 == 0 ? col::rustDark : (l % 3 == 1 ? col::rustMid : col::rustLight))
                                  .withAlpha (0.16f + rnd.nextFloat() * 0.30f);
            juce::Path blob;
            const int pts = 7 + rnd.nextInt (5);
            for (int p = 0; p < pts; ++p)
            {
                const float a = (float) p / (float) pts * juce::MathConstants<float>::twoPi;
                const float pr = rr * (0.55f + rnd.nextFloat() * 0.65f);
                const juce::Point<float> pt (cx + ox + pr * std::cos (a), cy + oy + pr * std::sin (a));
                if (p == 0) blob.startNewSubPath (pt); else blob.lineTo (pt);
            }
            blob.closeSubPath();
            g.setColour (rc);
            g.fillPath (blob.createPathWithRoundedCorners (rr * 0.5f));
        }
        // speckles around the patch
        g.setColour (col::rustDark.withAlpha (0.5f));
        for (int s = 0; s < 24; ++s)
        {
            const float sx = cx + (rnd.nextFloat() - 0.5f) * scale * 3.2f;
            const float sy = cy + (rnd.nextFloat() - 0.5f) * scale * 2.4f;
            const float sr = 0.6f + rnd.nextFloat() * 1.8f;
            g.fillEllipse (sx, sy, sr, sr);
        }
    };

    for (int i = 0; i < 14; ++i)
    {
        float cx, cy;
        if (rnd.nextBool())
        {   // hug an edge
            cx = rnd.nextBool() ? rnd.nextFloat() * fw * 0.22f : fw * (0.78f + rnd.nextFloat() * 0.22f);
            cy = rnd.nextFloat() * fh;
        }
        else
        {
            cx = rnd.nextFloat() * fw;
            cy = rnd.nextBool() ? fh * (0.7f + rnd.nextFloat() * 0.3f) : rnd.nextFloat() * fh * 0.3f;
        }
        rustPatch (cx, cy, 18.0f + rnd.nextFloat() * 42.0f);
    }
    for (int i = 0; i < 10; ++i) // small chips anywhere
        rustPatch (rnd.nextFloat() * fw, rnd.nextFloat() * fh, 5.0f + rnd.nextFloat() * 12.0f);

    // ---- scratches
    for (int i = 0; i < 22; ++i)
    {
        const float x1 = rnd.nextFloat() * fw, y1 = rnd.nextFloat() * fh;
        const float len = 20.0f + rnd.nextFloat() * 130.0f;
        const float a = rnd.nextFloat() * juce::MathConstants<float>::pi;
        const float x2 = x1 + len * std::cos (a), y2 = y1 + len * std::sin (a) * 0.35f;
        g.setColour ((rnd.nextBool() ? col::tealLight.brighter (0.4f) : col::rustDark)
                         .withAlpha (0.07f + rnd.nextFloat() * 0.12f));
        g.drawLine (x1, y1, x2, y2, 0.8f + rnd.nextFloat());
    }

    // ---- the tube shelf: dark recessed band across the top
    {
        auto shelf = juce::Rectangle<float> (0, 0, fw, fh * 0.235f);
        juce::ColourGradient sg (juce::Colour (0xff10312e), 0, 0,
                                 col::tealDeep, 0, shelf.getBottom(), false);
        g.setGradientFill (sg);
        g.fillRect (shelf);
        // lip highlight where the shelf meets the panel
        g.setColour (col::tealLight.withAlpha (0.5f));
        g.fillRect (0.0f, shelf.getBottom() - 2.0f, fw, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (0.0f, shelf.getBottom(), fw, 3.0f);
    }

    // ---- rivets along the edges
    auto rivet = [&] (float cx, float cy)
    {
        const float rr = 5.5f;
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (cx - rr, cy - rr + 1.5f, rr * 2, rr * 2);
        juce::ColourGradient dome (col::rustLight.brighter (0.2f), cx - rr * 0.4f, cy - rr * 0.5f,
                                   col::rustDark.darker (0.3f), cx + rr * 0.6f, cy + rr * 0.8f, true);
        g.setGradientFill (dome);
        g.fillEllipse (cx - rr, cy - rr, rr * 2, rr * 2);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (cx - rr * 0.45f, cy - rr * 0.55f, rr * 0.6f, rr * 0.6f);
    };

    const float inset = 14.0f;
    const int nAcross = juce::jmax (2, (int) (fw / 110.0f));
    const int nDown   = juce::jmax (2, (int) (fh / 110.0f));
    for (int i = 0; i <= nAcross; ++i)
    {
        const float x = inset + (fw - 2 * inset) * (float) i / (float) nAcross;
        rivet (x, inset);
        rivet (x, fh - inset);
    }
    for (int i = 1; i < nDown; ++i)
    {
        const float y = inset + (fh - 2 * inset) * (float) i / (float) nDown;
        rivet (inset, y);
        rivet (fw - inset, y);
    }

    // ---- vignette
    {
        juce::ColourGradient vig (juce::Colours::transparentBlack, fw * 0.5f, fh * 0.45f,
                                  juce::Colours::black.withAlpha (0.38f), fw * 0.5f, 0.0f, true);
        vig.point2 = { fw * 1.05f, fh * 1.05f };
        g.setGradientFill (vig);
        g.fillAll();
    }
}

} // namespace ttp
