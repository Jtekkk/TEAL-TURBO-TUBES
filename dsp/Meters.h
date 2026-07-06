// TURBO TUBES — dsp/Meters.h
// Trustworthy metering. Sample-peak with instant attack / 20 dB/s decay and
// 300 ms RMS. RMS is displayed with the AES-17 +3 dB convention so a full
// scale sine reads 0 dBFS RMS — meaning a -18 dBFS sine shows -18 on BOTH
// bars, and the -18 reference tick on the UI means what it says.
//
// Audio thread writes, UI thread reads — everything crosses via atomics.

#pragma once

#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>
#include "FastMath.h"

namespace tt
{

class StereoMeter
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = (float) sampleRate;
        // 20 dB/s peak decay
        peakDecay = std::pow (10.0f, -20.0f / (20.0f * fs));
        // 300 ms RMS window (one-pole on x²)
        rmsCoef = 1.0f - std::exp (-1.0f / (0.3f * fs));
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            peakState[c] = 0.0f;
            rmsState[c] = 0.0f;
            peak[c].store (0.0f, std::memory_order_relaxed);
            rms[c].store (0.0f, std::memory_order_relaxed);
        }
        clipped.store (false, std::memory_order_relaxed);
    }

    void processBlock (const float* const* data, int numCh, int n) noexcept
    {
        bool clip = false;
        for (int c = 0; c < 2; ++c)
        {
            const float* x = data[std::min (c, numCh - 1)];
            float pk = peakState[c];
            float ms = rmsState[c];
            for (int i = 0; i < n; ++i)
            {
                const float a = std::fabs (x[i]);
                pk = a > pk ? a : pk * peakDecay;
                ms += rmsCoef * (x[i] * x[i] - ms);
                if (a > 1.0f) clip = true;
            }
            peakState[c] = flushDenorm (pk);
            rmsState[c] = flushDenorm (ms);
            peak[c].store (pk, std::memory_order_relaxed);
            // AES-17: multiply RMS by sqrt(2) so a full-scale sine reads 0 dBFS
            rms[c].store (std::sqrt (ms * 2.0f), std::memory_order_relaxed);
        }
        if (clip)
            clipped.store (true, std::memory_order_relaxed);
    }

    float getPeakDb (int c) const noexcept { return gainToDb (peak[c & 1].load (std::memory_order_relaxed)); }
    float getRmsDb (int c) const noexcept  { return gainToDb (rms[c & 1].load (std::memory_order_relaxed)); }

    bool consumeClip() noexcept { return clipped.exchange (false, std::memory_order_relaxed); }

private:
    float fs = 48000.0f, peakDecay = 0.9999f, rmsCoef = 0.0001f;
    float peakState[2] = {}, rmsState[2] = {};
    std::atomic<float> peak[2] {}, rms[2] {};
    std::atomic<bool> clipped { false };
};

//==============================================================================
// Simple integer delay for the dry path. Sized once for the worst-case
// oversampler latency; the read offset tracks the active latency so dry and
// wet stay sample-aligned in mix / delta / bypass.
class DryDelay
{
public:
    void prepare (int maxDelaySamples, int maxBlock)
    {
        size = maxDelaySamples + maxBlock + 1;
        for (int c = 0; c < 2; ++c)
            buf[c].assign ((size_t) size, 0.0f);
        pos = 0;
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
            std::fill (buf[c].begin(), buf[c].end(), 0.0f);
        pos = 0;
    }

    // Write n input samples then read n samples delayed by `delay`.
    void processBlock (int channel, const float* in, float* out, int n, int delay) noexcept
    {
        auto& b = buf[channel & 1];
        int w = pos;
        for (int i = 0; i < n; ++i)
        {
            b[(size_t) w] = in[i];
            int r = w - delay;
            if (r < 0) r += size;
            out[i] = b[(size_t) r];
            if (++w >= size) w = 0;
        }
        if (channel == lastChannel)
            pos = w;   // advance shared cursor after the last channel
    }

    void setChannelCount (int numCh) noexcept { lastChannel = numCh - 1; }

private:
    std::vector<float> buf[2];
    int size = 1, pos = 0, lastChannel = 1;
};

} // namespace tt
