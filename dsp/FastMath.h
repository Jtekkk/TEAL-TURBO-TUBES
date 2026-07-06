// TURBO TUBES — dsp/FastMath.h
// Small math utilities shared by the DSP core. Header-only, no JUCE, no allocations.
//
// The saturation building block used throughout the tube stages is the algebraic
// sigmoid  S(x) = x / sqrt(1 + x^2).  Its antiderivative is sqrt(1 + x^2) - 1,
// which is exact and cheap (one sqrt, one divide). Every nonlinearity in the
// plugin is composed from this family so that first-order ADAA
// (antiderivative anti-aliasing) is *exact*, not approximated.

#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace tt
{

//==============================================================================
// dB helpers
inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  noexcept   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

// Exact float comparison without tripping -Wfloat-equal — used where exact
// bit-identity is the intended semantic (change detection, not tolerance).
inline bool exactlyEqual (float a, float b) noexcept { return ! (a < b) && ! (a > b); }

//==============================================================================
// Algebraic sigmoid family (exact antiderivatives)
inline float sigS (float x) noexcept        { return x / std::sqrt (1.0f + x * x); }   // S(x)
inline float sigS_AD (float x) noexcept     { return std::sqrt (1.0f + x * x); }        // ∫S = sqrt(1+x²) (+C)
inline float sigS_D (float x) noexcept                                                   // S'(x)
{
    const float t = 1.0f + x * x;
    return 1.0f / (t * std::sqrt (t));
}

// Saturating soft clamp for envelope control values: 2e/(2+e), maps [0,inf)
// onto [0,2). Keeps the dynamic bias/sag excursions musical no matter how hot
// the detector runs.
inline float envClamp (float e) noexcept
{
    return 2.0f * e / (2.0f + e);
}

//==============================================================================
// Denormal guard for recursive filter states. With FTZ/DAZ enabled by the host
// wrapper this is redundant, but states must stay well-behaved even without it.
inline float flushDenorm (float x) noexcept
{
    return (std::fabs (x) < 1.0e-25f) ? 0.0f : x;
}

//==============================================================================
// Deterministic PRNG (xorshift32) for component drift. Seeded from the saved
// plugin state so a session always recalls the exact same "unit".
struct Rng32
{
    explicit Rng32 (uint32_t seedValue = 0x9E3779B9u) noexcept { reseed (seedValue); }

    void reseed (uint32_t seedValue) noexcept
    {
        state = seedValue == 0 ? 0xA341316Cu : seedValue;
    }

    uint32_t nextU32() noexcept
    {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return state = x;
    }

    // Uniform in [-1, 1]
    float nextBipolar() noexcept
    {
        return (float) (int32_t) nextU32() * (1.0f / 2147483648.0f);
    }

    uint32_t state;
};

//==============================================================================
// Per-block linear parameter smoother. setTarget() is called once per block on
// the audio thread; next() ramps sample-accurately toward the target.
struct Smoother
{
    void prepare (double sampleRate, float rampSeconds) noexcept
    {
        const double samples = std::max (1.0, sampleRate * (double) rampSeconds);
        step = (float) (1.0 / samples);
        stepsToTarget = 0;
    }

    void snap (float v) noexcept
    {
        current = target = v;
        stepsToTarget = 0;
    }

    void setTarget (float v) noexcept
    {
        if (exactlyEqual (v, target))
            return;
        target = v;
        const float dist = std::fabs (target - current);
        stepsToTarget = (int) std::ceil (dist > 0.0f ? 1.0f / step : 0.0f);
        increment = (target - current) * step;
    }

    float next() noexcept
    {
        if (stepsToTarget <= 0)
            return current = target;
        --stepsToTarget;
        current += increment;
        return current;
    }

    bool isSmoothing() const noexcept { return stepsToTarget > 0; }
    float getCurrent() const noexcept { return current; }
    float getTarget()  const noexcept { return target; }

    float current = 0.0f, target = 0.0f, increment = 0.0f, step = 0.001f;
    int stepsToTarget = 0;
};

} // namespace tt
