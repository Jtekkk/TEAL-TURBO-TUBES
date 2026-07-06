// TURBO TUBES — dsp/TubeModels.h
// The five tube voicings — one per bottle on the box. Each model is a full
// dynamic description (static curve + bias/sag/Miller behavior), not just a
// transfer function. Values are voiced by ear against the test-suite harmonic
// analyzer; the *relationships* between models are the point:
//
//   VT-101 WHISPER     small-signal triode. Gentle, 2nd-harmonic dominant,
//                      barely any sag. The "always safe" vocal/bus tube.
//   G-300  GLOBE       directly-heated big-bottle triode. Deep 2nd + 3rd,
//                      slow generous sag, bias bloom. Romantic, wide.
//   SL-6   SILVERBIRD  pentode. Harder knee, odd-heavy (3rd/5th), quick sag,
//                      bright and forward. Edge without fizz.
//   BB-88  BLUE BOTTLE beam tetrode. Punchy, fast sag that grabs transients,
//                      crossover grit that appears as you push it. Drums.
//   R-90   RED STAR    surplus military bottle. Heavily asymmetric, strong
//                      bias drift, dark Miller rolloff, gnarly. Character.

#pragma once

#include <cmath>
#include "FastMath.h"

namespace tt
{

struct TubeModel
{
    const char* name;
    const char* tagline;

    // Plate curve: f(w) = wM·[S(k(w+b)) − S(k·b)] + wXamp·XO(w)
    float k;            // curvature (how hard the sigmoid bends)
    float bias0;        // static operating-point offset (asymmetry → even harmonics)
    float biasDynDepth; // how far the envelope pushes the bias (bloom / duty-cycle shift)
    float crossAmt;     // crossover-grit amplitude, relative to plate ceiling
    float crossSharp;   // crossover sharpness m
    float crossGap;     // crossover half-gap d

    // Grid conduction (soft one-sided limit before the plate)
    float gridT;        // conduction threshold
    float gridR;        // knee softness

    // Power-supply sag
    float sagDepth;     // max supply squish at full SAG knob
    float sagAttackMs;
    float sagReleaseMs;

    // Bias envelope (coupling-cap charge)
    float biasAttackMs;
    float biasReleaseMs;

    // Miller / stage bandwidth
    float millerHz;     // static stage rolloff
    float millerEnvDepth; // how far the envelope pulls the rolloff down (0..1)

    float inputGainDb;  // per-model trim so the DRIVE knob feels consistent
};

inline constexpr int kNumTubeModels = 5;

inline const TubeModel& tubeModel (int index) noexcept
{
    static const TubeModel models[kNumTubeModels] = {
        //  name        tagline                       k     bias0  bDyn   xAmt  xM    xD    gridT gridR  sagD  sagA  sagR   bA    bR    milHz   milD  inDb
        { "VT-101",  "WHISPER — small-signal glow",  0.60f, 0.30f, 0.10f, 0.00f, 2.0f, 0.30f, 1.50f, 0.85f, 0.20f, 25.f, 350.f, 4.0f, 140.f, 16000.f, 0.22f, 0.0f },
        { "G-300",   "GLOBE — romantic heat",        0.55f, 0.52f, 0.30f, 0.00f, 2.0f, 0.30f, 1.30f, 0.70f, 0.55f, 18.f, 520.f, 5.0f, 240.f, 12500.f, 0.35f, 0.5f },
        { "SL-6",    "SILVERBIRD — pentode edge",    2.00f, 0.05f, 0.05f, 0.10f, 2.6f, 0.30f, 1.05f, 0.45f, 0.30f,  8.f, 180.f, 3.0f, 110.f, 18000.f, 0.18f, 0.0f },
        { "BB-88",   "BLUE BOTTLE — beam punch",     1.10f, 0.12f, 0.12f, 0.32f, 3.4f, 0.55f, 1.20f, 0.55f, 0.45f,  6.f, 260.f, 3.5f, 150.f, 14000.f, 0.30f, 0.0f },
        { "R-90",    "RED STAR — surplus fury",      1.45f, 0.45f, 0.30f, 0.16f, 4.0f, 0.28f, 0.85f, 0.40f, 0.62f, 10.f, 420.f, 4.5f, 320.f,  9000.f, 0.50f, 2.0f },
    };
    return models[index < 0 ? 0 : (index >= kNumTubeModels ? kNumTubeModels - 1 : index)];
}

} // namespace tt
