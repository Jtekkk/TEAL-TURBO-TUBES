// TURBO TUBES — dsp/Filters.h
// Topology-preserving-transform (TPT / trapezoidal) filters. TPT structures are
// used everywhere instead of direct-form biquads: they stay numerically clean
// when cutoff is modulated per-sample (Miller rolloff, detector filters) and
// their prewarped bilinear mapping keeps the response analog-matched at the
// cutoff at any sample rate, including cutoffs close to Nyquist.

#pragma once

#include <cmath>
#include <algorithm>
#include "FastMath.h"

namespace tt
{

static constexpr float kPi = 3.14159265358979323846f;

//==============================================================================
// First-order TPT (Zavalishin) one-pole. LP and HP outputs share one state.
struct OnePoleTPT
{
    void prepare (double sampleRate) noexcept
    {
        fs = (float) sampleRate;
        reset();
    }

    void reset() noexcept { z = 0.0f; }

    void setCutoff (float hz) noexcept
    {
        hz = std::clamp (hz, 1.0f, 0.49f * fs);
        const float wd = kPi * hz / fs;      // prewarp
        const float g0 = std::tan (wd);
        G = g0 / (1.0f + g0);
    }

    float processLP (float x) noexcept
    {
        const float v = (x - z) * G;
        const float y = v + z;
        z = flushDenorm (y + v);
        return y;
    }

    float processHP (float x) noexcept
    {
        return x - processLP (x);
    }

    float fs = 48000.0f, G = 0.1f, z = 0.0f;
};

//==============================================================================
// Second-order TPT state-variable filter (Zavalishin). Provides LP / HP.
// Butterworth Q by default — used for the LowCut / HighCut tone filters and
// the sidechain detector filters.
struct SVF2
{
    void prepare (double sampleRate) noexcept
    {
        fs = (float) sampleRate;
        reset();
        setCutoff (1000.0f, 0.70710678f);
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void setCutoff (float hz, float Q = 0.70710678f) noexcept
    {
        hz = std::clamp (hz, 1.0f, 0.49f * fs);
        g  = std::tan (kPi * hz / fs);
        k  = 1.0f / std::max (Q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // Returns {lp, hp} pair via out parameters; band-pass derivable if needed.
    void process (float x, float& lp, float& hp) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenorm (2.0f * v1 - ic1);
        ic2 = flushDenorm (2.0f * v2 - ic2);
        lp = v2;
        hp = x - k * v1 - v2;
    }

    float processLP (float x) noexcept { float lp, hp; process (x, lp, hp); return lp; }
    float processHP (float x) noexcept { float lp, hp; process (x, lp, hp); return hp; }

    float fs = 48000.0f;
    float g = 0.1f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

//==============================================================================
// First-order shelving filters built on the TPT one-pole:
//   low shelf : y = x + (A - 1) · LP(x)
//   high shelf: y = x + (A - 1) · HP(x)
// The corner is gain-symmetric (placed at the geometric mid-gain point) so
// boost and cut are exact mirrors — this is what analog Baxandall tone stacks
// do, and it is what makes a tilt control feel symmetric.
struct ShelfTPT
{
    void prepare (double sampleRate) noexcept
    {
        pole.prepare (sampleRate);
        setParams (700.0f, 0.0f, false);
    }

    void reset() noexcept { pole.reset(); }

    // gainDb applied to the shelf side; `high` selects high-shelf.
    void setParams (float cornerHz, float gainDb, bool highShelf) noexcept
    {
        A    = dbToGain (gainDb);
        high = highShelf;
        // Mid-gain corner placement: shift the pole by sqrt(A) so the
        // half-gain point stays at cornerHz for both boost and cut.
        const float root = std::sqrt (std::max (A, 1.0e-3f));
        pole.setCutoff (high ? cornerHz * root : cornerHz / root);
    }

    float process (float x) noexcept
    {
        const float band = high ? pole.processHP (x) : pole.processLP (x);
        return x + (A - 1.0f) * band;
    }

    OnePoleTPT pole;
    float A = 1.0f;
    bool high = false;
};

//==============================================================================
// Tilt EQ: complementary low/high shelves pivoting around one frequency.
// tiltDb > 0 brightens (+high / -low), < 0 darkens.
struct TiltFilter
{
    void prepare (double sampleRate) noexcept
    {
        low.prepare (sampleRate);
        highS.prepare (sampleRate);
        setTilt (700.0f, 0.0f);
    }

    void reset() noexcept { low.reset(); highS.reset(); }

    void setTilt (float pivotHz, float tiltDb) noexcept
    {
        low.setParams  (pivotHz, -0.5f * tiltDb, false);
        highS.setParams (pivotHz,  0.5f * tiltDb, true);
    }

    float process (float x) noexcept { return highS.process (low.process (x)); }

    ShelfTPT low, highS;
};

//==============================================================================
// DC blocker: first-order TPT high-pass at a few Hz. Removes the DC offset the
// asymmetric tube bias produces without touching audible lows.
struct DCBlocker
{
    void prepare (double sampleRate, float hz = 5.0f) noexcept
    {
        pole.prepare (sampleRate);
        pole.setCutoff (hz);
    }

    void reset() noexcept { pole.reset(); }
    float process (float x) noexcept { return pole.processHP (x); }

    OnePoleTPT pole;
};

} // namespace tt
