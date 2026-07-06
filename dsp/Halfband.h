// TURBO TUBES — dsp/Halfband.h
// Polyphase linear-phase half-band FIR resampling stages and the multistage
// oversampler built from them (1x / 2x / 4x / 8x / 16x, two quality tiers).
//
// Design goals:
//  * Linear phase (FIR) so the "Linear" quality claims are honest.
//  * Exact integer total latency at the BASE sample rate for every
//    factor/quality combination. Tap counts are constrained so that the
//    round-trip group delay of stage s (running at 2^(s+1) x) contributes an
//    integer number of base-rate samples. This allows a plain integer delay
//    line to align the dry path sample-exactly (perfect null in delta mode)
//    and lets the host be told the truth in getLatencySamples().
//  * All filters are designed once in prepare() (Kaiser windowed half-band
//    sinc); switching factor or quality at runtime is routing only — no
//    allocation, no design work on the audio thread.

#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "FastMath.h"

namespace tt
{

//==============================================================================
namespace kaiser
{
    inline double besselI0 (double x)
    {
        // Series expansion, converges quickly for the arguments we use.
        double sum = 1.0, term = 1.0;
        const double halfX = 0.5 * x;
        for (int k = 1; k < 64; ++k)
        {
            term *= (halfX / k) * (halfX / k);
            sum += term;
            if (term < 1.0e-14 * sum)
                break;
        }
        return sum;
    }

    inline double betaForAttenuation (double attDb)
    {
        if (attDb > 50.0)  return 0.1102 * (attDb - 8.7);
        if (attDb >= 21.0) return 0.5842 * std::pow (attDb - 21.0, 0.4) + 0.07886 * (attDb - 21.0);
        return 0.0;
    }
} // namespace kaiser

//==============================================================================
// One 2x half-band stage. Handles both directions (each direction has its own
// state). The centre tap is an exact 0.5, even taps are exactly zero, so both
// polyphase branches reduce to: [pure delay of c/2] + [short convolution with
// the odd taps]. Tap length L satisfies L ≡ 1 (mod congruence) with c=(L-1)/2
// even, so the pure-delay branch always lands on integer sample positions.
class HalfbandStage
{
public:
    // attDb: stopband attenuation target. passEdge: passband edge normalized
    // to the *filter's* running rate (the 2x rate). congruence: required
    // divisor of (L-1) so the multistage latency stays integer at base rate.
    void design (double attDb, double passEdge, int congruence, int maxBlockAtInputRate)
    {
        const double stopEdge  = 0.5 - passEdge;
        const double deltaOmega = 2.0 * kPi * (stopEdge - passEdge);
        const double beta = kaiser::betaForAttenuation (attDb);

        int L = (int) std::ceil ((attDb - 7.95) / (2.285 * deltaOmega)) | 1; // odd
        congruence = std::max (congruence, 4);          // keeps c=(L-1)/2 even
        while ((L - 1) % congruence != 0)
            ++L;

        const int c = (L - 1) / 2;
        numOdd = c;                                     // odd-index taps 1,3,..,L-2
        centreDelay = c / 2;                            // integer by construction

        oddTaps.assign ((size_t) numOdd, 0.0f);
        const double i0beta = kaiser::besselI0 (beta);
        double sum = 0.0;
        for (int r = 0; r < numOdd; ++r)
        {
            const int n = 2 * r + 1;                    // absolute tap index
            const double m = (double) (n - c);          // offset from centre
            const double t = m / (double) c;
            const double window = kaiser::besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - t * t))) / i0beta;
            const double x = 0.5 * m;                   // sinc(m/2) argument
            const double sinc = std::sin (kPi * x) / (kPi * x); // m odd => x != 0
            const double tap = 0.5 * sinc * window;
            oddTaps[(size_t) r] = (float) tap;
            sum += tap;
        }
        // Normalise so odd taps sum to exactly 0.5 => unity DC gain overall.
        const float norm = (float) (0.5 / sum);
        for (auto& t : oddTaps)
            t *= norm;

        upHist.assign   ((size_t) numOdd, 0.0f);
        upCentre.assign ((size_t) std::max (1, centreDelay), 0.0f);
        downHistOdd.assign ((size_t) numOdd, 0.0f);
        downCentre.assign  ((size_t) std::max (1, centreDelay), 0.0f);
        upScratch.assign   ((size_t) (numOdd + maxBlockAtInputRate), 0.0f);
        downScratch.assign ((size_t) (numOdd + maxBlockAtInputRate), 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (upHist.begin(),      upHist.end(),      0.0f);
        std::fill (upCentre.begin(),    upCentre.end(),    0.0f);
        std::fill (downHistOdd.begin(), downHistOdd.end(), 0.0f);
        std::fill (downCentre.begin(),  downCentre.end(),  0.0f);
        upCentrePos = downCentrePos = 0;
    }

    int tapLength() const noexcept { return 2 * numOdd + 1; }

    // Round-trip (up+down) group delay of this stage, in samples at the
    // stage's INPUT rate: (L-1)/2.
    int roundTripDelayAtInputRate() const noexcept { return numOdd; }

    // in: n samples at rate R  ->  out: 2n samples at rate 2R.
    void processUp (const float* in, float* out, int n) noexcept
    {
        // ext = history ++ block, so ext[i + numOdd - 1 - r] == x[i - r]
        float* ext = upScratch.data();
        std::memcpy (ext, upHist.data(), (size_t) numOdd * sizeof (float));
        std::memcpy (ext + numOdd, in, (size_t) n * sizeof (float));

        const float* taps = oddTaps.data();
        const int no = numOdd;
        for (int i = 0; i < n; ++i)
        {
            float acc = 0.0f;
            const float* x = ext + i; // x[no + j] == input[i + j]
            for (int r = 0; r < no; ++r)
                acc += taps[r] * x[no - r];       // taps[r] · input[i - r]

            // even output: centre branch (2 * 0.5 gain cancels): pure delay
            float& slot = upCentre[(size_t) upCentrePos];
            out[2 * i]     = slot;
            slot           = in[i];
            if (++upCentrePos >= centreDelay) upCentrePos = 0;

            out[2 * i + 1] = 2.0f * acc;
        }

        if (n >= no)
            std::memcpy (upHist.data(), in + n - no, (size_t) no * sizeof (float));
        else
        {
            std::memmove (upHist.data(), upHist.data() + n, (size_t) (no - n) * sizeof (float));
            std::memcpy  (upHist.data() + (no - n), in, (size_t) n * sizeof (float));
        }
    }

    // in: 2n samples at rate 2R  ->  out: n samples at rate R.
    void processDown (const float* in, float* out, int n) noexcept
    {
        // z[i] = 0.5 * vEven[i - c/2] + sum_r taps[r] * vOdd[i - 1 - r]
        float* extOdd = downScratch.data();
        std::memcpy (extOdd, downHistOdd.data(), (size_t) numOdd * sizeof (float));
        for (int i = 0; i < n; ++i)
            extOdd[numOdd + i] = in[2 * i + 1];

        const float* taps = oddTaps.data();
        const int no = numOdd;
        for (int i = 0; i < n; ++i)
        {
            float acc = 0.0f;
            const float* v = extOdd + i; // v[no - 1 - r] == vOdd[i - 1 - r]
            for (int r = 0; r < no; ++r)
                acc += taps[r] * v[no - 1 - r];

            float& slot = downCentre[(size_t) downCentrePos];
            const float even = slot;
            slot = in[2 * i];
            if (++downCentrePos >= centreDelay) downCentrePos = 0;

            out[i] = acc + 0.5f * even;
        }

        if (n >= no)
        {
            for (int r = 0; r < no; ++r)
                downHistOdd[(size_t) r] = in[2 * (n - no + r) + 1];
        }
        else
        {
            std::memmove (downHistOdd.data(), downHistOdd.data() + n, (size_t) (no - n) * sizeof (float));
            for (int i = 0; i < n; ++i)
                downHistOdd[(size_t) (no - n + i)] = in[2 * i + 1];
        }
    }

private:
    std::vector<float> oddTaps;
    std::vector<float> upHist, upCentre, downHistOdd, downCentre;
    std::vector<float> upScratch, downScratch;
    int numOdd = 0, centreDelay = 1;
    int upCentrePos = 0, downCentrePos = 0;
};

//==============================================================================
// Multistage oversampler for ONE channel. All stage/quality combinations are
// designed up-front; changing configuration at runtime is just routing.
class Oversampler
{
public:
    static constexpr int kMaxStages = 4;      // up to 16x
    static constexpr int kNumFactors = 5;     // 1,2,4,8,16

    struct QualitySpec
    {
        double attDb;
        double passEdgeBase;   // passband edge relative to BASE fs
    };

    void prepare (double /*sampleRate*/, int maxBaseBlock)
    {
        maxBlock = maxBaseBlock;

        const QualitySpec specs[2] = {
            { 65.0,  0.408  },   // Punchy : short, ~0.5 ms @2x, pass to 0.408*fs (18 kHz @ 44.1k)
            { 110.0, 0.4535 },   // Pristine: pass to 0.4535*fs (20 kHz @ 44.1k), 110 dB stop
        };

        for (int q = 0; q < 2; ++q)
            for (int s = 0; s < kMaxStages; ++s)
            {
                // Stage s runs at 2^(s+1) x. Its passband edge relative to its
                // own running rate shrinks by 2^(s+1); (L-1) must divide by
                // 2^(s+1) (min 4) for integer base-rate latency.
                const double passEdge = specs[q].passEdgeBase / std::pow (2.0, s + 1);
                const int congruence  = std::max (4, 1 << (s + 1));
                const int blockAtInputRate = maxBlock << s;
                stages[q][s].design (specs[q].attDb, passEdge, congruence, blockAtInputRate);
            }

        for (int s = 0; s <= kMaxStages; ++s)
            osBuf[s].assign ((size_t) std::max (1, maxBlock << s), 0.0f);

        setConfig (1, true);
        computeLatencies();
    }

    // factorIndex: 0..4 -> 1x..16x. Never allocates.
    void setConfig (int factorIndex, bool pristineQuality) noexcept
    {
        const int newStages = std::clamp (factorIndex, 0, 4);
        const int newQ = pristineQuality ? 1 : 0;
        if (newStages == activeStages && newQ == quality)
            return;
        activeStages = newStages;
        quality = newQ;
        reset();
    }

    void reset() noexcept
    {
        for (int q = 0; q < 2; ++q)
            for (int s = 0; s < kMaxStages; ++s)
                stages[q][s].reset();
    }

    int factor() const noexcept { return 1 << activeStages; }

    // Integer latency at base rate for the ACTIVE configuration.
    int latencySamples() const noexcept { return latency[quality][activeStages]; }

    // Worst-case latency across all configs (handy for sizing the dry delay).
    int maxLatencySamples() const noexcept
    {
        int m = 0;
        for (int q = 0; q < 2; ++q)
            for (int f = 0; f < kNumFactors; ++f)
                m = std::max (m, latency[q][f]);
        return m;
    }

    // Upsample n base samples; returns pointer to n * factor() samples.
    float* processUp (const float* in, int n) noexcept
    {
        if (activeStages == 0)
        {
            std::memcpy (osBuf[0].data(), in, (size_t) n * sizeof (float));
            return osBuf[0].data();
        }
        const float* src = in;
        int len = n;
        for (int s = 0; s < activeStages; ++s)
        {
            stages[quality][s].processUp (src, osBuf[s + 1].data(), len);
            src = osBuf[s + 1].data();
            len *= 2;
        }
        return osBuf[activeStages].data();
    }

    // Downsample n * factor() samples (in the buffer returned by processUp)
    // back to n base samples written to out.
    void processDown (const float* inOS, float* out, int n) noexcept
    {
        if (activeStages == 0)
        {
            if (inOS != out)
                std::memmove (out, inOS, (size_t) n * sizeof (float));
            return;
        }
        const float* src = inOS;
        int len = n << activeStages;
        for (int s = activeStages - 1; s >= 0; --s)
        {
            len /= 2;
            float* dst = (s == 0) ? out : osBuf[s].data();
            stages[quality][s].processDown (src, dst, len);
            src = dst;
        }
    }

    int tapLengthForTest (int q, int s) const { return stages[q][s].tapLength(); }

private:
    void computeLatencies() noexcept
    {
        for (int q = 0; q < 2; ++q)
        {
            latency[q][0] = 0;
            int acc = 0;
            for (int s = 0; s < kMaxStages; ++s)
            {
                // Stage s round trip: (L-1)/2 samples at its input rate 2^s x
                // => (L-1)/2 / 2^s base samples. Integer by design.
                acc += stages[q][s].roundTripDelayAtInputRate() >> s;
                latency[q][s + 1] = acc;
            }
        }
    }

    HalfbandStage stages[2][kMaxStages];
    std::vector<float> osBuf[kMaxStages + 1];
    int latency[2][kNumFactors] = {};
    int activeStages = 0, quality = 1;
    int maxBlock = 0;
};

} // namespace tt
