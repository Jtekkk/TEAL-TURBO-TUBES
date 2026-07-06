// TURBO TUBES — dsp/TubeStage.h
// The nonlinear core for one channel, running at the oversampled rate.
//
// Cascade per sample:
//   grid conduction (soft one-sided limit, ADAA)
//   → supply-sag scaling
//   → plate curve with dynamic bias (ADAA)
//   → [TURBO: second, hotter plate pass (ADAA)]
//   → Miller rolloff (envelope-modulated one-pole)
//
// Every nonlinearity is first-order ADAA with an *exact* closed-form
// antiderivative (see FastMath.h), so aliasing is suppressed both by the
// oversampling AND analytically inside each stage. The dynamic elements
// (bias, sag, Miller) are driven by envelope followers at control rate, which
// is what separates "tube behavior" from "static waveshaper".

#pragma once

#include <cmath>
#include "FastMath.h"
#include "Filters.h"
#include "TubeModels.h"

namespace tt
{

//==============================================================================
// First-order ADAA state: y = (F(x1) - F(x0)) / (x1 - x0), midpoint fallback.
//
// The difference quotient is numerically ill-conditioned for small dx — in
// float it costs ~50 dB of spur floor. The antiderivative path therefore runs
// in DOUBLE precision (measured in the test suite: audible-band spurs drop
// below -90 dBFS). The forward function f stays in float.
struct AdaaState
{
    void reset() noexcept { x1 = 0.0; F1 = 0.0; primed = false; }

    template <typename Func, typename FuncAD>
    float process (float x, Func&& f, FuncAD&& F) noexcept
    {
        const double xd = (double) x;
        if (! primed)
        {
            x1 = xd;
            F1 = F (xd);
            primed = true;
            return f (x);
        }
        const double dx = xd - x1;
        const double Fx = F (xd);
        float y;
        if (std::fabs (dx) > 1.0e-6)
            y = (float) ((Fx - F1) / dx);
        else
            y = f ((float) (0.5 * (xd + x1)));
        x1 = xd;
        F1 = Fx;
        return y;
    }

    double x1 = 0.0, F1 = 0.0;
    bool primed = false;
};

//==============================================================================
// Plate curve parameters, derived from a TubeModel (+ user bias) once per
// parameter change — never in the per-sample loop.
struct PlateParams
{
    float k = 1.0f;       // curvature
    float wM = 1.0f;      // main sigmoid weight (slope-normalised at static bias)
    float wX = 0.0f;      // crossover amplitude (absolute)
    float m = 2.0f;       // crossover sharpness
    float d = 0.3f;       // crossover half-gap
    float invK = 1.0f;    // 1/k
    float inv2m = 0.25f;  // 1/(2m)

    void derive (const TubeModel& mdl, float staticBias, float driveHardness) noexcept
    {
        k = mdl.k * driveHardness;
        m = mdl.crossSharp;
        d = mdl.crossGap;
        // Normalise small-signal slope to 1 at the static bias point.
        const float kb = k * staticBias;
        const float mainSlope = k * sigS_D (kb);            // d/dw S(k(w+b)) at w=0
        const float xoSlope   = m * sigS_D (m * d);         // crossover slope at 0
        wM = 1.0f / std::max (mainSlope + 1.0e-6f, 1.0e-6f);
        wX = mdl.crossAmt * wM;
        // Fold the crossover's small slope back out of the normalisation so
        // total small-signal gain stays exactly 1.
        wM = (1.0f - wX * xoSlope) / std::max (mainSlope, 1.0e-6f);
        wX = mdl.crossAmt * wM;
        invK = 1.0f / k;
        inv2m = 1.0f / (2.0f * m);
    }

    // f(w) with (slow-moving) bias b and its cached S(k·b) offset.
    float f (float w, float b, float skb) const noexcept
    {
        const float main = sigS (k * (w + b)) - skb;
        const float xo   = 0.5f * (sigS (m * (w - d)) + sigS (m * (w + d)));
        return wM * main + wX * xo;
    }

    // Exact antiderivative of f in w (constant of integration irrelevant).
    // Double precision: the ADAA difference quotient needs the headroom.
    double F (double w, double b, double skb) const noexcept
    {
        const double kw = (double) k * (w + b);
        const double mwm = (double) m * (w - (double) d);
        const double mwp = (double) m * (w + (double) d);
        const double main = std::sqrt (1.0 + kw * kw) * (double) invK - skb * w;
        const double xo   = (std::sqrt (1.0 + mwm * mwm) + std::sqrt (1.0 + mwp * mwp)) * (double) inv2m;
        return (double) wM * main + (double) wX * xo;
    }
};

//==============================================================================
// Grid conduction: a one-sided C³ knee. Perfectly transparent below T-r
// (identity — zero coloration), Hermite knee across [T-r, T+r] with value,
// slope, curvature and third derivative continuous at both joints, conduction
// limit T above. The C³ continuity makes the generated harmonics fall off ~h⁻⁴, which
// keeps ultra-high harmonics (the ones that alias) tens of dB lower than a
// parabolic knee — measured directly in the test suite. Antiderivative is an
// exact piecewise polynomial, so ADAA stays exact, no transcendentals.
//
// Knee in normalised coords t = (u - a) / 2r, t ∈ [0,1] — C³ Hermite:
//   q(t)  = t - 2.5t⁴ + 3t⁵ - t⁶   (q(0)=0 q'(0)=1 q''(0)=q'''(0)=0,
//                                    q(1)=½ q'(1)=q''(1)=q'''(1)=0)
//   q'(t) = (1-t)³(6t²+3t+1) ≥ 0   (monotonic)
//   Q(t)  = ∫q = t²/2 - t⁵/2 + t⁶/2 - t⁷/7,  Q(1) = 5/14
struct GridParams
{
    float T = 1.3f, r = 0.7f;
    float kneeLo = 0.6f, kneeHi = 2.0f, inv2r = 0.7f;
    double twoR = 1.4, fourR2 = 1.96, Ck = 0.0, C3 = 0.0;

    void derive (const TubeModel& mdl) noexcept
    {
        T = mdl.gridT;
        r = std::max (mdl.gridR, 0.05f);
        kneeLo = T - r;
        kneeHi = T + r;
        inv2r = 1.0f / (2.0f * r);
        twoR = 2.0 * (double) r;
        fourR2 = twoR * twoR;
        const double a = (double) kneeLo;
        Ck = 0.5 * a * a - a * a;                 // joins F_knee to u²/2 at a
        // F continuity at the top joint (t = 1, Q(1) = 5/14):
        const double uHi = (double) kneeHi;
        const double FkneeHi = a * uHi + fourR2 * (5.0 / 14.0) + Ck;
        C3 = FkneeHi - (double) T * uHi;
    }

    float f (float u) const noexcept
    {
        if (u <= kneeLo) return u;
        if (u >= kneeHi) return T;
        const float t = (u - kneeLo) * inv2r;
        const float t3 = t * t * t;
        return kneeLo + 2.0f * r * (t + t3 * (-2.5f * t + 3.0f * t * t - t3));
    }

    double F (double u) const noexcept
    {
        if (u <= (double) kneeLo) return 0.5 * u * u;
        if (u >= (double) kneeHi) return (double) T * u + C3;
        const double a = (double) kneeLo;
        const double t = (u - a) / twoR;
        const double t2 = t * t;
        const double t5 = t2 * t2 * t;
        const double Q = 0.5 * t2 + t5 * (-0.5 + 0.5 * t - t * t / 7.0);
        return a * u + fourR2 * Q + Ck;
    }
};

//==============================================================================
// Symmetric C³ headroom clamp ahead of the plate. The algebraic sigmoid keeps
// curving (≈1/x²) arbitrarily far into saturation, which puts surprisingly fat
// tails on the ultra-high harmonics at extreme overdrive — its complex
// singularity sits only 1/k off the real axis, so harmonics of a swing A decay
// like exp(-h·asinh(1/(kA))). Capping the swing at Wc = 3.2/k pins that decay
// rate at a healthy ~2.7 dB/harmonic for EVERY model (the traversed curve
// stays below ~89% saturation, where the models live), and the C³ Hermite
// knee turns "absurd drive" into a true flat-top: zero curvature while
// pinned, controlled transitions. Below |w| = 0.6·Wc it is exactly identity.
struct ClampParams
{
    float Wc = 3.0f, Rc = 1.2f;
    float kneeLo = 1.8f;
    double C3 = 0.0;

    void derive (float /*plateK*/) noexcept
    {
        // Fixed generous headroom: measured best across the OS ladder, and the
        // identity region (|w| ≤ 3) covers everything music-level signals do.
        Wc = 4.5f;
        Rc = 1.5f;
        kneeLo = Wc - Rc;
        const double a = (double) kneeLo;
        const double twoR = 2.0 * (double) Rc;
        // F over knee at t=1: a·(a+2R) + 4R²·Q(1) - a²/2, then slope Wc above
        C3 = a * (a + twoR) + twoR * twoR * (5.0 / 14.0) - 0.5 * a * a;
    }

    float f (float u) const noexcept
    {
        const float au = std::fabs (u);
        if (au <= kneeLo) return u;
        const float s = u < 0.0f ? -1.0f : 1.0f;
        if (au >= Wc + Rc) return s * Wc;
        const float t = (au - kneeLo) / (2.0f * Rc);
        const float t3 = t * t * t;
        return s * (kneeLo + 2.0f * Rc * (t + t3 * (-2.5f * t + 3.0f * t * t - t3)));
    }

    double F (double u) const noexcept
    {
        const double a = (double) kneeLo;
        const double au = std::fabs (u);
        if (au <= a) return 0.5 * u * u;
        const double twoR = 2.0 * (double) Rc;
        if (au >= (double) (Wc + Rc))
            return (double) Wc * (au - (a + twoR)) + C3;
        const double t = (au - a) / twoR;
        const double t2 = t * t;
        const double t5 = t2 * t2 * t;
        const double Q = 0.5 * t2 + t5 * (-0.5 + 0.5 * t - t * t / 7.0);
        return a * au + twoR * twoR * Q - 0.5 * a * a;   // even function of u
    }
};

//==============================================================================
class TubeChannel
{
public:
    void prepare (double osSampleRate) noexcept
    {
        fsOS = osSampleRate;
        miller.prepare (osSampleRate);
        reset();
    }

    void reset() noexcept
    {
        gridAdaa.reset();
        clampAdaa.reset();
        plateAdaa.reset();
        turboClampAdaa.reset();
        turboAdaa.reset();
        miller.reset();
        millerCounter = 0;
    }

    // Called whenever model / bias knob / turbo change. Cheap; audio-safe.
    void configure (const TubeModel& mdl, float biasKnob, bool turboOn) noexcept
    {
        model = &mdl;
        turbo = turboOn;
        staticBias = mdl.bias0 + 0.35f * biasKnob;
        grid.derive (mdl);
        plate.derive (mdl, staticBias, 1.0f);
        // TURBO: hotter second pass — more curvature, tighter bias, more grit.
        platTurbo.derive (mdl, staticBias * 0.6f, 1.7f);
        clampMain.derive (plate.k);
        clampTurbo.derive (platTurbo.k);
        millerBase = mdl.millerHz;
        millerDepth = mdl.millerEnvDepth;
    }

    // Process nOS samples in place. envB / envS are base-rate control arrays;
    // shift converts an OS index to a base index (osIndex >> shift).
    // sagAmt: 0..1 knob * model depth. biasDyn: model dyn depth (pre-scaled).
    // biasOffset/millerScale: per-channel drift.
    void process (float* buf, int nOS, const float* envB, const float* envS,
                  int shift, float sagAmt, float biasDyn,
                  float biasOffset, float millerScale) noexcept
    {
        const PlateParams& P = plate;
        const PlateParams& PT = platTurbo;

        for (int i = 0; i < nOS; ++i)
        {
            const int bi = i >> shift;
            const float eB = envClamp (envB[bi]);   // bounded 0..2: musical range
            const float eS = envClamp (envS[bi]);

            // Grid conduction (static params → exact ADAA)
            float u = gridAdaa.process (buf[i],
                        [this] (float x) noexcept { return grid.f (x); },
                        [this] (double x) noexcept { return grid.F (x); });

            // Supply sag: headroom shrinks as the reservoir droops
            const float sigma = 1.0f / (1.0f + sagAmt * eS);
            const float invSigma = 1.0f + sagAmt * eS;

            // Dynamic bias: conduction charges the coupling cap → the
            // operating point slides further into the curve with programme
            // level, growing the even-harmonic content ("bloom")
            const float b = staticBias + biasOffset + biasDyn * eB;
            const float skb = sigS (P.k * b);

            float w = clampAdaa.process (u * invSigma,
                        [this] (float x) noexcept { return clampMain.f (x); },
                        [this] (double x) noexcept { return clampMain.F (x); });
            float y = plateAdaa.process (w,
                        [&P, b, skb] (float x) noexcept { return P.f (x, b, skb); },
                        [&P, b, skb] (double x) noexcept { return P.F (x, (double) b, (double) skb); });
            y *= sigma;

            if (turbo)
            {
                const float bT = b * 0.6f;
                const float skbT = sigS (PT.k * bT);
                const float wT = turboClampAdaa.process (y * 2.0f * invSigma, // slam the second stage
                            [this] (float x) noexcept { return clampTurbo.f (x); },
                            [this] (double x) noexcept { return clampTurbo.F (x); });
                float yT = turboAdaa.process (wT,
                            [&PT, bT, skbT] (float x) noexcept { return PT.f (x, bT, skbT); },
                            [&PT, bT, skbT] (double x) noexcept { return PT.F (x, (double) bT, (double) skbT); });
                y = yT * sigma * 0.62f;                 // rough static level match; the probe trues it up
            }

            // Miller rolloff, envelope-modulated. tan() only every 8 samples.
            if ((millerCounter++ & 7) == 0)
            {
                const float drop = 1.0f - millerDepth * eB * 0.5f;
                miller.setCutoff (millerBase * millerScale * drop);
            }
            buf[i] = miller.processLP (y);
        }
    }

    // Static (memoryless) evaluation of the full nonlinear chain at steady
    // state — used by the auto-gain probe so compensation is *deterministic*.
    float staticEval (float x, float envBss, float envSss,
                      float sagAmt, float biasDyn) const noexcept
    {
        const float eB = envClamp (envBss);
        const float eS = envClamp (envSss);
        const float u = grid.f (x);
        const float sigma = 1.0f / (1.0f + sagAmt * eS);
        const float invSigma = 1.0f + sagAmt * eS;
        const float b = staticBias + biasDyn * eB;
        const float skb = sigS (plate.k * b);
        float y = sigma * plate.f (clampMain.f (u * invSigma), b, skb);
        if (turbo)
        {
            const float bT = b * 0.6f;
            const float skbT = sigS (platTurbo.k * bT);
            y = platTurbo.f (clampTurbo.f (y * 2.0f * invSigma), bT, skbT) * sigma * 0.62f;
        }
        return y;
    }

private:
    const TubeModel* model = nullptr;
    GridParams grid;
    PlateParams plate, platTurbo;
    ClampParams clampMain, clampTurbo;
    AdaaState gridAdaa, clampAdaa, plateAdaa, turboClampAdaa, turboAdaa;
    OnePoleTPT miller;
    double fsOS = 96000.0;
    float staticBias = 0.1f;
    float millerBase = 15000.0f, millerDepth = 0.3f;
    int millerCounter = 0;
    bool turbo = false;
};

} // namespace tt
