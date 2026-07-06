// TURBO TUBES — dsp/Detector.h
// Sidechain detector: source select (internal / external), HP+LP detector
// filtering, then two attack/release envelope followers — a fast one for the
// bias (bloom) and a slow one for the supply sag. Runs at BASE rate; the tube
// core consumes the resulting control arrays at the oversampled rate.
//
// Envelope outputs are normalised to the -18 dBFS reference (0.125 peak), so
// model depth constants mean the same thing at any gain staging.

#pragma once

#include <cmath>
#include <algorithm>
#include "FastMath.h"
#include "Filters.h"

namespace tt
{

struct EnvFollower
{
    void prepare (double sampleRate) noexcept
    {
        fs = (float) sampleRate;
        setTimes (5.0f, 200.0f);
        env = 0.0f;
    }

    void setTimes (float attackMs, float releaseMs) noexcept
    {
        aAtk = 1.0f - std::exp (-1.0f / (fs * std::max (attackMs, 0.05f) * 0.001f));
        aRel = 1.0f - std::exp (-1.0f / (fs * std::max (releaseMs, 1.0f) * 0.001f));
    }

    void reset() noexcept { env = 0.0f; }

    float process (float rectified) noexcept
    {
        const float a = rectified > env ? aAtk : aRel;
        env += a * (rectified - env);
        env = flushDenorm (env);
        return env;
    }

    float fs = 48000.0f, aAtk = 0.1f, aRel = 0.01f, env = 0.0f;
};

//==============================================================================
class Detector
{
public:
    static constexpr float kInvRef = 8.0f;   // 1 / 0.125 (-18 dBFS peak)

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        hp.prepare (sampleRate);
        lp.prepare (sampleRate);
        envBias.prepare (sampleRate);
        envSag.prepare (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        hp.reset(); lp.reset();
        envBias.reset(); envSag.reset();
    }

    void setFilters (float hpHz, float lpHz) noexcept
    {
        hpActive = hpHz > 21.0f;
        lpActive = lpHz < 19500.0f;
        if (hpActive) hp.setCutoff (hpHz);
        if (lpActive) lp.setCutoff (lpHz);
    }

    // inertia knob (0..1) scales the model time constants 0.25x .. 4x
    void setTimes (float biasAtkMs, float biasRelMs,
                   float sagAtkMs, float sagRelMs, float inertia) noexcept
    {
        const float scale = std::pow (4.0f, 2.0f * std::clamp (inertia, 0.0f, 1.0f) - 1.0f);
        envBias.setTimes (biasAtkMs * scale, biasRelMs * scale);
        envSag.setTimes (sagAtkMs * scale, sagRelMs * scale);
    }

    // src: mono detector feed (already summed / max'd across channels).
    // Writes normalised control values into envB / envS.
    void processBlock (const float* src, float* envB, float* envS, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float d = src[i];
            if (hpActive) d = hp.processHP (d);
            if (lpActive) d = lp.processLP (d);
            const float r = std::min (std::fabs (d) * kInvRef, 24.0f);
            envB[i] = envBias.process (r);
            envS[i] = envSag.process (r);
        }
    }

    // Steady-state envelope for a sine of given peak amplitude — used by the
    // auto-gain probe (mean of |a·sin| = 2a/π).
    static float steadyStateEnv (float sinePeak) noexcept
    {
        return sinePeak * 0.63661977f * kInvRef;
    }

private:
    double fs = 48000.0;
    SVF2 hp, lp;
    EnvFollower envBias, envSag;
    bool hpActive = false, lpActive = false;
};

//==============================================================================
// Component variation / analog drift. Deterministic per seed: static
// channel-to-channel offsets plus an ultra-slow bounded wander (heater and
// supply never sit perfectly still). Everything scales with the VARIANCE knob;
// at 0 both channels are bit-identical.
class DriftEngine
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int blockSize) noexcept
    {
        // Wander LP at ~0.15 Hz, advanced once per block.
        const double blocksPerSecond = sampleRate / std::max (1, blockSize);
        wanderCoef = (float) (1.0 - std::exp (-2.0 * kPi * 0.15 / std::max (1.0, blocksPerSecond)));
    }

    void reseed (uint32_t seed) noexcept
    {
        rng.reseed (seed);
        for (int c = 0; c < kMaxChannels; ++c)
        {
            biasStatic[c]   = 0.045f * rng.nextBipolar();
            driveDbStatic[c] = 0.40f  * rng.nextBipolar();
            millerStatic[c] = 0.12f  * rng.nextBipolar();
            outDbStatic[c]  = 0.25f  * rng.nextBipolar();
            wander[c] = 0.0f;
        }
    }

    // Advance the slow wander once per audio block.
    void tick() noexcept
    {
        for (int c = 0; c < kMaxChannels; ++c)
        {
            wander[c] += wanderCoef * (rng.nextBipolar() - wander[c]);
            wander[c] = flushDenorm (wander[c]);
        }
    }

    float biasOffset (int c, float amt) const noexcept
    {
        return amt * (biasStatic[c] + 0.02f * wander[c]);
    }

    float driveGainMul (int c, float amt) const noexcept
    {
        return dbToGain (amt * (driveDbStatic[c] + 0.05f * wander[c]));
    }

    float millerScale (int c, float amt) const noexcept
    {
        return 1.0f + amt * millerStatic[c];
    }

    float outGainMul (int c, float amt) const noexcept
    {
        return dbToGain (amt * outDbStatic[c]);
    }

private:
    Rng32 rng { 0x54554245 }; // "TUBE"
    float biasStatic[kMaxChannels] = {}, driveDbStatic[kMaxChannels] = {};
    float millerStatic[kMaxChannels] = {}, outDbStatic[kMaxChannels] = {};
    float wander[kMaxChannels] = {};
    float wanderCoef = 0.01f;
};

} // namespace tt
