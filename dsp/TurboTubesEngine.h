// TURBO TUBES — dsp/TurboTubesEngine.h
// The complete signal path, JUCE-free. One instance per plugin.
//
//   in ─ inTrim ─┬─ meter(in)
//                ├─ [M/S encode] ─ drive(+drift, +M/S tilt) ─┐
//                │                                            │ detector feed
//                │                        external SC ────────┤ (HP/LP → envB, envS)
//                │                                            ▼
//                │            up 2^N ─ GRID ─ SAG ─ PLATE(+bias) ─ [TURBO] ─ MILLER ─ down 2^N
//                │                                            │
//                │        DC block ─ LowCut ─ Tilt ─ HighCut ─ autoGain ─ drift trim ─ outTrim
//                │                                            │
//                └── dry delay (== reported latency) ──┐  [M/S decode + width]
//                                                      ▼      ▼
//                                        mix / delta / bypass crossfade ─ meter(out) ─ out
//
// Real-time rules honoured here: no allocation, no locks, no system calls in
// process(); all buffers sized in prepare(); configuration changes that would
// click (oversampling, model, turbo) go through short fades.

#pragma once

#include <vector>
#include <atomic>
#include <cstring>
#include "FastMath.h"
#include "Filters.h"
#include "Halfband.h"
#include "TubeModels.h"
#include "TubeStage.h"
#include "Detector.h"
#include "Meters.h"

namespace tt
{

struct EngineParams
{
    float inTrimDb  = 0.0f;     // -24..+24
    float driveDb   = 12.0f;    // 0..36 (DRIVE knob 0..10 → ×3.6)
    float biasKnob  = 0.0f;     // -1..+1
    float sag       = 0.35f;    // 0..1
    float inertia   = 0.5f;     // 0..1 (time-constant scale 0.25x..4x)
    float lowCutHz  = 20.0f;    // 20 = off .. 300
    float tiltDb    = 0.0f;     // -6..+6
    float highCutHz = 20000.0f; // 2k .. 20000 = off
    float mix       = 1.0f;     // 0..1
    float outTrimDb = 0.0f;     // -24..+24
    float width     = 1.0f;     // 0..2
    float msBal     = 0.0f;     // -1..+1 (drive tilt mid↔side, M/S mode)
    float drift     = 0.25f;    // 0..1 VARIANCE
    float scHpHz    = 20.0f;
    float scLpHz    = 20000.0f;

    int model    = 1;           // 0..4
    int osIndex  = 1;           // 0..4 → 1x..16x
    bool pristine = true;       // oversampling quality tier
    bool turbo    = false;
    bool autoGain = true;
    bool delta    = false;
    bool msMode   = false;
    bool scExternal = false;
    bool bypass   = false;

    uint32_t driftSeed = 0x54554245u;
};

//==============================================================================
class TurboTubesEngine
{
public:
    static constexpr int kChunk = 256;

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        fs = sampleRate;
        numCh = std::min (numChannels, 2);

        for (int c = 0; c < 2; ++c)
        {
            os[c].prepare (sampleRate, kChunk);
            tube[c].prepare (sampleRate * 16.0);  // Miller prewarp uses OS rate; worst case
            dc[c].prepare (sampleRate);
            lowCut[c].prepare (sampleRate);
            highCut[c].prepare (sampleRate);
            tilt[c].prepare (sampleRate);
        }
        detector.prepare (sampleRate);
        drift.prepare (sampleRate, maxBlockSize);
        drift.reseed (params.driftSeed);
        meterIn.prepare (sampleRate);
        meterOut.prepare (sampleRate);

        dryDelay.prepare (os[0].maxLatencySamples(), kChunk);
        dryDelay.setChannelCount (numCh);

        const float rampSec = 0.03f;
        smInGain.prepare (sampleRate, rampSec);
        smDrive.prepare (sampleRate, rampSec);
        smOutGain.prepare (sampleRate, rampSec);
        smMix.prepare (sampleRate, rampSec);
        smWidth.prepare (sampleRate, rampSec);
        smComp.prepare (sampleRate, 0.08f);
        smBypass.prepare (sampleRate, 0.01f);
        // Snap smoothers to the CURRENT parameter targets — no ghost ramps on
        // the first buffer after (re)preparation.
        smInGain.snap (dbToGain (params.inTrimDb));
        smDrive.snap (dbToGain (params.driveDb + tubeModel (params.model).inputGainDb));
        smOutGain.snap (dbToGain (params.outTrimDb));
        smMix.snap (params.mix);
        smWidth.snap (params.width);
        smComp.snap (1.0f);
        smBypass.snap (params.bypass ? 1.0f : 0.0f);

        activeOsIndex = params.osIndex;
        activePristine = params.pristine;
        activeModel = params.model;
        activeTurbo = params.turbo;
        applyStaticConfig();
        fullFade = 1.0f; wetFade = 1.0f;
        probeDirty = true;
        prepared = true;
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            os[c].reset();
            tube[c].reset();
            dc[c].reset();
            lowCut[c].reset();
            highCut[c].reset();
            tilt[c].reset();
        }
        detector.reset();
        dryDelay.reset();
        meterIn.reset();
        meterOut.reset();
    }

    void setParams (const EngineParams& p) noexcept
    {
        if (p.driftSeed != params.driftSeed)
            drift.reseed (p.driftSeed);

        if (! exactlyEqual (p.driveDb, params.driveDb) || p.model != params.model
            || ! exactlyEqual (p.biasKnob, params.biasKnob)
            || ! exactlyEqual (p.sag, params.sag)
            || p.turbo != params.turbo)
            probeDirty = true;

        params = p;
    }

    int latencySamples() const noexcept { return os[0].latencySamples(); }

    // io: numCh channel pointers, in-place. sc: external sidechain (may be
    // null / fewer channels). n: any block size the host throws at us.
    void process (float* const* io, int n, const float* const* sc = nullptr, int scChannels = 0) noexcept
    {
        if (! prepared)
            return;

        updateControls();

        int done = 0;
        while (done < n)
        {
            const int m = std::min (kChunk, n - done);
            float* ch[2];
            const float* scCh[2] = { nullptr, nullptr };
            for (int c = 0; c < numCh; ++c)
                ch[c] = io[c] + done;
            if (sc != nullptr)
                for (int c = 0; c < scChannels && c < 2; ++c)
                    scCh[c] = sc[c] + done;
            processChunk (ch, m, scCh, scChannels);
            done += m;
        }

        drift.tick();
    }

    // Telemetry for the UI (lock-free)
    StereoMeter meterIn, meterOut;
    std::atomic<float> glowAmount { 0.0f };   // 0..1 tube glow drive
    std::atomic<float> sagAmount  { 0.0f };   // 0..1 supply squish
    std::atomic<float> compGainDb { 0.0f };   // current auto-gain in dB

private:
    //==========================================================================
    void applyStaticConfig() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            os[c].setConfig (activeOsIndex, activePristine);
            tube[c].prepare (fs * (double) os[c].factor());
            tube[c].configure (tubeModel (activeModel), params.biasKnob, activeTurbo);
        }
        osShift = 0;
        for (int f = os[0].factor(); f > 1; f >>= 1)
            ++osShift;
    }

    void updateControls() noexcept
    {
        const auto& mdl = tubeModel (params.model);

        smInGain.setTarget (dbToGain (params.inTrimDb));
        smDrive.setTarget (dbToGain (params.driveDb + mdl.inputGainDb));
        smOutGain.setTarget (dbToGain (params.outTrimDb));
        smMix.setTarget (params.mix);
        smWidth.setTarget (params.width);
        smBypass.setTarget (params.bypass ? 1.0f : 0.0f);

        detector.setFilters (params.scHpHz, params.scLpHz);
        detector.setTimes (mdl.biasAttackMs, mdl.biasReleaseMs,
                           mdl.sagAttackMs, mdl.sagReleaseMs, params.inertia);

        for (int c = 0; c < 2; ++c)
        {
            lowCut[c].setCutoff (params.lowCutHz, 0.70710678f);
            highCut[c].setCutoff (params.highCutHz, 0.70710678f);
            tilt[c].setTilt (700.0f, params.tiltDb);
        }
        lowCutActive  = params.lowCutHz > 21.0f;
        highCutActive = params.highCutHz < 19500.0f;
        tiltActive    = std::fabs (params.tiltDb) > 0.01f;

        // Continuous bias-knob changes re-derive the plate curve (cheap).
        for (int c = 0; c < 2; ++c)
            tube[c].configure (tubeModel (activeModel), params.biasKnob, activeTurbo);

        if (params.autoGain)
        {
            if (probeDirty)
            {
                probeAutoGain();
                probeDirty = false;
            }
        }
        else
            smComp.setTarget (1.0f);

        compGainDb.store (gainToDb (smComp.getTarget()), std::memory_order_relaxed);
    }

    // Deterministic loudness probe: pass one cycle of a -18 dBFS sine through
    // the static tube chain at steady-state envelopes; compensate RMS exactly.
    void probeAutoGain() noexcept
    {
        const auto& mdl = tubeModel (params.model);
        const float gD = dbToGain (params.driveDb + mdl.inputGainDb);
        const float a  = 0.125f * gD;
        const float ess = std::min (Detector::steadyStateEnv (a), 24.0f);
        const float eB = ess * params.sag;      // engine scales envB by SAG
        const float eS = ess;                   // sagAmt below carries the knob
        const float sagAmt = params.sag * mdl.sagDepth;
        const float biasDyn = mdl.biasDynDepth;

        // One cycle of a sine through the static chain. The real signal path
        // DC-blocks the asymmetric distortion, so measure AC power only.
        float y[64];
        double mean = 0.0, sx = 0.0;
        for (int i = 0; i < 64; ++i)
        {
            const float x = std::sin (2.0f * kPi * (float) i / 64.0f);
            y[i] = tube[0].staticEval (a * x, eB, eS, sagAmt, biasDyn);
            mean += y[i];
            sx += (double) (a * x) * (a * x);
        }
        mean /= 64.0;
        double sy = 0.0;
        for (int i = 0; i < 64; ++i)
            sy += ((double) y[i] - mean) * ((double) y[i] - mean);

        // sqrt(sy/sx) is the tubes' own gain (sx is post-drive); multiplying
        // by gD gives plugin-input → wet-output gain. Undoing it makes DRIVE
        // a "how much tube" control at constant loudness, deterministically —
        // A/B levels stay matched regardless of programme.
        const float g = (float) std::sqrt (std::max (sy, 1.0e-12) / std::max (sx, 1.0e-12)) * gD;
        smComp.setTarget (std::clamp (1.0f / g, 0.02f, 8.0f));
    }

    //==========================================================================
    void processChunk (float* const* ch, int n, const float* const* sc, int scChannels) noexcept
    {
        const bool stereo = numCh == 2;
        const bool useMS = params.msMode && stereo;

        // ---- input trim (per-sample smoothed), dry capture, input meter
        for (int i = 0; i < n; ++i)
        {
            const float g = smInGain.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] *= g;
        }
        for (int c = 0; c < numCh; ++c)
            dryDelay.processBlock (c, ch[c], dryBuf[c], n, os[0].latencySamples());

        {
            const float* mp[2] = { ch[0], ch[std::min (1, numCh - 1)] };
            meterIn.processBlock (mp, numCh, n);
        }

        // ---- optional M/S encode (wet path only)
        for (int c = 0; c < numCh; ++c)
            std::memcpy (wetBuf[c], ch[c], (size_t) n * sizeof (float));

        if (useMS)
            for (int i = 0; i < n; ++i)
            {
                const float l = wetBuf[0][i], r = wetBuf[1][i];
                wetBuf[0][i] = 0.70710678f * (l + r);
                wetBuf[1][i] = 0.70710678f * (l - r);
            }

        // ---- drive gain (+ drift, + M/S tilt)
        const float balTiltM = useMS ? dbToGain (-4.0f * params.msBal) : 1.0f;
        const float balTiltS = useMS ? dbToGain ( 4.0f * params.msBal) : 1.0f;
        const float dm0 = drift.driveGainMul (0, params.drift);
        const float dm1 = drift.driveGainMul (1, params.drift);
        for (int i = 0; i < n; ++i)
        {
            const float g = smDrive.next();
            wetBuf[0][i] *= g * dm0 * balTiltM;
            if (stereo)
                wetBuf[1][i] *= g * dm1 * balTiltS;
        }

        // ---- detector feed → control envelopes
        if (params.scExternal && sc[0] != nullptr)
        {
            const float* s0 = sc[0];
            const float* s1 = scChannels > 1 && sc[1] != nullptr ? sc[1] : sc[0];
            for (int i = 0; i < n; ++i)
                detBuf[i] = std::max (std::fabs (s0[i]), std::fabs (s1[i]));
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                float a = std::fabs (wetBuf[0][i]);
                if (stereo) a = std::max (a, std::fabs (wetBuf[1][i]));
                detBuf[i] = a;
            }
        }
        detector.processBlock (detBuf, envB, envS, n);

        // Glow telemetry reads the raw envelope (lights follow the signal even
        // when SAG is at 0)…
        const float drive01 = std::clamp (params.driveDb / 36.0f, 0.0f, 1.0f);
        glowAmount.store (std::clamp (envClamp (envB[n - 1]) * (0.25f + 0.75f * drive01), 0.0f, 1.0f),
                          std::memory_order_relaxed);

        // …then SAG becomes the master "how alive is this tube" control: it
        // scales the bias/Miller dynamics as well as the supply squish.
        for (int i = 0; i < n; ++i)
            envB[i] *= params.sag;

        // ---- config switching fades
        stepFades();

        // ---- the tubes (oversampled)
        const auto& mdl = tubeModel (activeModel);
        const float sagAmt = params.sag * mdl.sagDepth;
        const float biasDyn = mdl.biasDynDepth;
        // Miller rolloff opens up as drive comes down: transparent when clean,
        // increasingly band-limited as the stage is pushed (like the real thing).
        const float drive01Now = std::clamp (params.driveDb / 36.0f, 0.0f, 1.0f);
        const float millerOpen = 1.0f + 2.0f * (1.0f - drive01Now);
        for (int c = 0; c < numCh; ++c)
        {
            float* up = os[c].processUp (wetBuf[c], n);
            tube[c].process (up, n << osShift, envB, envS, osShift,
                             sagAmt, biasDyn,
                             drift.biasOffset (c, params.drift),
                             drift.millerScale (c, params.drift) * millerOpen);
            os[c].processDown (up, wetBuf[c], n);
        }

        // ---- post chain at base rate
        const float outDrift0 = drift.outGainMul (0, params.drift);
        const float outDrift1 = drift.outGainMul (1, params.drift);
        for (int c = 0; c < numCh; ++c)
        {
            float* w = wetBuf[c];
            const float od = c == 0 ? outDrift0 : outDrift1;
            for (int i = 0; i < n; ++i)
            {
                float y = dc[c].process (w[i]);
                if (lowCutActive)  y = lowCut[c].processHP (y);
                if (tiltActive)    y = tilt[c].process (y);
                if (highCutActive) y = highCut[c].processLP (y);
                w[i] = y * od;
            }
        }

        // comp gain + out trim (shared ramps, applied equally to both channels)
        for (int i = 0; i < n; ++i)
        {
            const float g = smComp.next() * smOutGain.next() * wetFade;
            wetBuf[0][i] *= g;
            if (stereo) wetBuf[1][i] *= g;
            wetFadeStep();
        }

        // ---- M/S decode + width (wet only)
        if (stereo)
        {
            if (useMS)
                for (int i = 0; i < n; ++i)
                {
                    const float wd = smWidth.next();
                    const float m = wetBuf[0][i], s = wetBuf[1][i] * wd;
                    wetBuf[0][i] = 0.70710678f * (m + s);
                    wetBuf[1][i] = 0.70710678f * (m - s);
                }
            else
                for (int i = 0; i < n; ++i)
                {
                    const float wd = smWidth.next();
                    const float m = 0.5f * (wetBuf[0][i] + wetBuf[1][i]);
                    const float s = 0.5f * (wetBuf[0][i] - wetBuf[1][i]) * wd;
                    wetBuf[0][i] = m + s;
                    wetBuf[1][i] = m - s;
                }
        }

        // ---- mix / delta / bypass / full fade → output
        for (int i = 0; i < n; ++i)
        {
            const float mix = smMix.next();
            const float byp = smBypass.next();
            for (int c = 0; c < numCh; ++c)
            {
                const float dry = dryBuf[c][i];
                const float wet = wetBuf[c][i];
                float y = params.delta ? (wet - dry) * mix
                                       : dry + (wet - dry) * mix;
                y = y + (dry - y) * byp;      // latency-aligned soft bypass
                ch[c][i] = y * fullFade;
            }
            fullFadeStep();
        }

        {
            const float* mp[2] = { ch[0], ch[std::min (1, numCh - 1)] };
            meterOut.processBlock (mp, numCh, n);
        }

        // ---- telemetry (glow was stored pre-scaling, next to the detector)
        const float squish = sagAmt * envClamp (envS[n - 1]);
        sagAmount.store (std::clamp (squish / (1.0f + squish), 0.0f, 1.0f),
                         std::memory_order_relaxed);
    }

    //==========================================================================
    // Config fade machinery. OS/quality changes fade the FULL output (latency
    // jumps); model/turbo changes fade the WET path only.
    void stepFades() noexcept
    {
        const bool osChange = params.osIndex != activeOsIndex || params.pristine != activePristine;
        const bool wetChange = params.model != activeModel || params.turbo != activeTurbo;

        if (osChange && fullTarget > 0.5f)
            fullTarget = 0.0f;
        if (wetChange && wetTarget > 0.5f && ! osChange)
            wetTarget = 0.0f;

        if (fullTarget < 0.5f && fullFade <= 0.0f)
        {
            activeOsIndex = params.osIndex;
            activePristine = params.pristine;
            activeModel = params.model;
            activeTurbo = params.turbo;
            applyStaticConfig();
            for (int c = 0; c < 2; ++c) { os[c].reset(); tube[c].reset(); }
            latencyChanged.store (true, std::memory_order_release);
            fullTarget = 1.0f;
        }
        if (wetTarget < 0.5f && wetFade <= 0.0f)
        {
            activeModel = params.model;
            activeTurbo = params.turbo;
            for (int c = 0; c < 2; ++c)
            {
                tube[c].configure (tubeModel (activeModel), params.biasKnob, activeTurbo);
                tube[c].reset();
            }
            probeDirty = true;
            wetTarget = 1.0f;
        }
    }

    void fullFadeStep() noexcept
    {
        constexpr float step = 1.0f / 128.0f;
        fullFade += fullTarget > fullFade ? step : (fullTarget < fullFade ? -step : 0.0f);
        fullFade = std::clamp (fullFade, 0.0f, 1.0f);
    }

    void wetFadeStep() noexcept
    {
        constexpr float step = 1.0f / 128.0f;
        wetFade += wetTarget > wetFade ? step : (wetTarget < wetFade ? -step : 0.0f);
        wetFade = std::clamp (wetFade, 0.0f, 1.0f);
    }

public:
    std::atomic<bool> latencyChanged { false };

private:
    double fs = 48000.0;
    int numCh = 2, osShift = 1;
    bool prepared = false;

    EngineParams params;
    Oversampler os[2];
    TubeChannel tube[2];
    DCBlocker dc[2];
    SVF2 lowCut[2], highCut[2];
    TiltFilter tilt[2];
    Detector detector;
    DriftEngine drift;
    DryDelay dryDelay;

    Smoother smInGain, smDrive, smOutGain, smMix, smWidth, smComp, smBypass;

    float dryBuf[2][kChunk] = {}, wetBuf[2][kChunk] = {};
    float detBuf[kChunk] = {}, envB[kChunk] = {}, envS[kChunk] = {};

    int activeOsIndex = 1, activeModel = 1;
    bool activePristine = true, activeTurbo = false;
    bool lowCutActive = false, highCutActive = false, tiltActive = false;
    bool probeDirty = true;

    float fullFade = 1.0f, fullTarget = 1.0f;
    float wetFade = 1.0f, wetTarget = 1.0f;
};

} // namespace tt
