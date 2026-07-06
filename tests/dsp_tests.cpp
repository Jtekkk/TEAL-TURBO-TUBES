// TURBO TUBES — tests/dsp_tests.cpp
// Offline verification of the DSP core (no JUCE, no audio hardware).
// Every marketing claim the plugin makes is asserted here with a measurement:
//   * oversampling filter specs (passband ripple, stopband attenuation)
//   * integer latency, sample-exact dry alignment, delta = wet - dry exactly
//   * aliasing suppression vs oversampling factor
//   * per-model harmonic signatures (and that the five models actually differ)
//   * auto-gain loudness matching across the whole drive range
//   * programme-dependent (dynamic) behaviour: sag compresses, then recovers
//   * drift bounds; drift=0 → bit-identical channels
//   * block-size invariance, NaN/denormal safety fuzz
//   * CPU benchmark (informational + generous floor)

#define _USE_MATH_DEFINES   // MSVC: expose M_PI from <cmath> (must precede it)
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <complex>
#include <random>
#include <chrono>
#include <string>
#include <functional>

#include "../dsp/TurboTubesEngine.h"

using namespace tt;

//==============================================================================
static int failures = 0;
static std::string currentTest;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (! (cond)) {                                                         \
            ++failures;                                                         \
            std::printf ("  FAIL [%s]: %s (%s)\n", currentTest.c_str(), msg, #cond); \
        }                                                                       \
    } while (0)

static void beginTest (const char* name)
{
    currentTest = name;
    std::printf ("== %s\n", name);
}

//==============================================================================
// Minimal iterative radix-2 FFT
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Hann-windowed magnitude spectrum in dB (normalised so a full-scale sine
// reads ~0 dB at its bin).
static std::vector<double> spectrumDb (const float* x, size_t n)
{
    std::vector<std::complex<double>> buf (n);
    double wsum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) n);
        buf[i] = x[i] * w;
        wsum += w;
    }
    fft (buf);
    std::vector<double> mag (n / 2);
    for (size_t k = 0; k < n / 2; ++k)
        mag[k] = 20.0 * std::log10 (std::abs (buf[k]) / (0.5 * wsum) + 1.0e-15);
    return mag;
}

//==============================================================================
// Drive a fresh engine over a signal, fixed block size.
static std::vector<std::vector<float>> runEngine (const EngineParams& p, double fs,
                                                  const std::vector<std::vector<float>>& in,
                                                  int block = 512)
{
    TurboTubesEngine engine;
    const int numCh = (int) in.size();
    const int n = (int) in[0].size();
    engine.setParams (p);
    engine.prepare (fs, block, numCh);
    engine.setParams (p);

    auto out = in;
    for (int pos = 0; pos < n; pos += block)
    {
        const int m = std::min (block, n - pos);
        float* ptrs[2];
        for (int c = 0; c < numCh; ++c)
            ptrs[c] = out[c].data() + pos;
        engine.process (ptrs, m);
    }
    return out;
}

static std::vector<std::vector<float>> makeSine (int numCh, int n, double fs, double f0, float amp)
{
    std::vector<std::vector<float>> v (numCh, std::vector<float> (n));
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < n; ++i)
            v[c][i] = amp * (float) std::sin (2.0 * M_PI * f0 * i / fs);
    return v;
}

static double rmsOf (const float* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / n);
}

static EngineParams cleanParams()
{
    EngineParams p;
    p.driveDb = 0.0f;
    p.sag = 0.0f;
    p.drift = 0.0f;
    p.autoGain = false;
    p.mix = 1.0f;
    p.model = 0;
    p.osIndex = 1;
    p.pristine = true;
    return p;
}

//==============================================================================
static void testHalfbandSpecs()
{
    beginTest ("half-band filter specs");
    const double att[2] = { 65.0, 110.0 };
    const char* qn[2] = { "Punchy", "Pristine" };

    for (int q = 0; q < 2; ++q)
    {
        Oversampler osamp;
        osamp.prepare (48000.0, 256);
        osamp.setConfig (1, q == 1); // engage stage 0 only

        // Impulse through the upsampling path = the filter's impulse response
        // at the 2x rate (plus the pure-delay branch).
        const int N = 256;
        std::vector<float> in (N, 0.0f);
        in[0] = 1.0f;
        float* up = osamp.processUp (in.data(), N);

        const size_t fftN = 8192;
        std::vector<float> ir (fftN, 0.0f);
        for (int i = 0; i < 2 * N && i < (int) fftN; ++i)
            ir[i] = up[i] * 0.5f; // upsample path has 2x gain by design

        std::vector<std::complex<double>> buf (fftN);
        for (size_t i = 0; i < fftN; ++i) buf[i] = ir[i];
        fft (buf);

        const double passEdge = (q == 1 ? 0.4535 : 0.408) / 2.0; // rel. 2x rate
        double maxPassDevDb = 0.0, maxStopDb = -300.0;
        for (size_t k = 1; k < fftN / 2; ++k)
        {
            const double f = (double) k / fftN;      // normalised to 2x rate
            const double db = 20.0 * std::log10 (std::abs (buf[k]) + 1e-15);
            if (f < passEdge)
                maxPassDevDb = std::max (maxPassDevDb, std::fabs (db));
            else if (f > 0.5 - passEdge)
                maxStopDb = std::max (maxStopDb, db);
        }
        std::printf ("  %s stage0: passband dev %.4f dB, stopband %.1f dB (target -%.0f dB)\n",
                     qn[q], maxPassDevDb, maxStopDb, att[q]);
        CHECK (maxPassDevDb < 0.1, "passband ripple under 0.1 dB");
        CHECK (maxStopDb < - (att[q] - 8.0), "stopband attenuation near design target");
    }
}

//==============================================================================
static void testLatencyAndAlignment()
{
    beginTest ("latency reporting + dry-path alignment + delta identity");
    const double fs = 48000.0;
    const int n = 16384;

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-0.25f, 0.25f);
    std::vector<std::vector<float>> noise (2, std::vector<float> (n));
    for (auto& chn : noise)
        for (auto& s : chn) s = dist (rng);

    for (int osi = 0; osi < 5; ++osi)
        for (int q = 0; q < 2; ++q)
        {
            EngineParams p = cleanParams();
            p.osIndex = osi;
            p.pristine = q == 1;
            p.mix = 0.0f;          // dry only → must be a pure delay

            TurboTubesEngine engine;
            engine.setParams (p);
            engine.prepare (fs, 512, 2);
            engine.setParams (p);
            const int lat = engine.latencySamples();

            auto out = noise;
            for (int pos = 0; pos < n; pos += 512)
            {
                float* ptrs[2] = { out[0].data() + pos, out[1].data() + pos };
                engine.process (ptrs, std::min (512, n - pos));
            }

            double maxErr = 0.0;
            for (int i = lat + 100; i < n; ++i)
                maxErr = std::max (maxErr, (double) std::fabs (out[0][i] - noise[0][i - lat]));
            std::printf ("  os=%dx %s: latency %d smp, dry-path max err %.2e\n",
                         1 << osi, q ? "pristine" : "punchy", lat, maxErr);
            CHECK (maxErr < 1.0e-6, "dry path is an exact integer delay of the reported latency");
        }

    // delta identity: wet == dry + delta, sample-exact
    {
        EngineParams p = cleanParams();
        p.driveDb = 24.0f;
        p.sag = 0.6f;
        p.model = 4;
        p.osIndex = 2;

        auto sine = makeSine (2, n, fs, 997.0, 0.25f);
        p.delta = false; p.mix = 1.0f;
        auto wet = runEngine (p, fs, sine);
        p.mix = 0.0f;
        auto dry = runEngine (p, fs, sine);
        p.delta = true; p.mix = 1.0f;
        auto del = runEngine (p, fs, sine);

        double maxErr = 0.0;
        for (int i = 0; i < n; ++i)
            maxErr = std::max (maxErr, (double) std::fabs (wet[0][i] - (dry[0][i] + del[0][i])));
        std::printf ("  delta identity max err %.2e\n", maxErr);
        CHECK (maxErr < 1.0e-5, "delta mode is exactly wet minus dry");
    }

    // Switching oversampling mid-stream must not click. With mix<100% the dry
    // path contributes, so a mis-timed config switch (dry read offset jumping a
    // chunk late relative to the wet latency change) shows up as a per-sample
    // discontinuity. Feed a smooth low tone and bound the sample-to-sample jump.
    {
        EngineParams p = cleanParams();
        p.model = 1;
        p.driveDb = 12.0f;
        p.mix = 0.5f;                 // dry + wet: exposes any misalignment
        p.osIndex = 0;                // start at 1x

        auto in = makeSine (2, n, fs, 90.0, 0.3f);   // smooth, low slew
        TurboTubesEngine engine;
        engine.setParams (p);
        engine.prepare (fs, 128, 2);
        engine.setParams (p);

        auto out = in;
        const float refJump = 0.3f * 2.0f * (float) M_PI * 90.0f / (float) fs; // ~1 sample of the tone
        double worstJump = 0.0;
        for (int pos = 0; pos < n; pos += 128)
        {
            if (pos == 4096) { p.osIndex = 2; engine.setParams (p); }   // 1x → 4x
            if (pos == 8192) { p.osIndex = 4; engine.setParams (p); }   // 4x → 16x
            const int m = std::min (128, n - pos);
            float* ptrs[2] = { out[0].data() + pos, out[1].data() + pos };
            engine.process (ptrs, m);
        }
        // Warm up past the initial primer; scan the switch regions.
        for (int i = 2050; i < n; ++i)
            worstJump = std::max (worstJump, (double) std::fabs (out[0][i] - out[0][i - 1]));
        std::printf ("  OS-switch continuity: worst per-sample jump %.4f (tone step ~%.4f)\n",
                     worstJump, refJump);
        CHECK (worstJump < 8.0 * refJump, "mid-stream oversampling switch does not click");
    }
}

//==============================================================================
// Measures the worst non-harmonic spur below 19 kHz (the half-band transition
// sliver right at Nyquist is inherent to every half-band design and sits above
// the audible band; harmonics themselves are the intended signal).
static double aliasFloor (float driveDb, int osIndex, double fs, int k0, size_t N)
{
    EngineParams p = cleanParams();
    p.model = 4;                            // R-90: nastiest
    p.driveDb = driveDb;
    p.sag = 0.0f;                           // static → pure aliasing view
    p.osIndex = osIndex;
    p.pristine = true;

    const int total = (int) N + 8192;
    const double f0 = (double) k0 * fs / (double) N;
    auto in = makeSine (1, total, fs, f0, 0.5f);
    auto out = runEngine (p, fs, in);
    auto mag = spectrumDb (out[0].data() + 8192, N);

    const size_t kAud = (size_t) (19000.0 * N / fs);
    double worst = -300.0;
    for (size_t k = 64; k < kAud; ++k)
    {
        bool nearHarm = false;
        for (int h = 1; h <= 3; ++h)
            if (std::labs ((long) k - (long) h * k0) <= 6) { nearHarm = true; break; }
        if (! nearHarm && mag[k] > worst)
            worst = mag[k];
    }
    return worst;
}

static void testAliasing()
{
    beginTest ("aliasing suppression");
    const double fs = 44100.0;
    const size_t N = 32768;
    const int k0 = 4458;                       // ≈6 kHz, exact bin

    // Tier 1: hot musical use — post-drive peaks ≈ +14 dBFS into the stage.
    std::printf ("  hot (drive 18, R-90, 6 kHz):\n");
    const double hot1  = aliasFloor (18.0f, 0, fs, k0, N);
    const double hot4  = aliasFloor (18.0f, 2, fs, k0, N);
    const double hot8  = aliasFloor (18.0f, 3, fs, k0, N);
    std::printf ("    1x %.1f | 4x %.1f | 8x %.1f dBFS\n", hot1, hot4, hot8);

    // Tier 2: torture — +24 dB overdrive, effectively a fuzz pedal.
    std::printf ("  torture (drive 30, R-90, 6 kHz):\n");
    const double tor1  = aliasFloor (30.0f, 0, fs, k0, N);
    const double tor4  = aliasFloor (30.0f, 2, fs, k0, N);
    const double tor16 = aliasFloor (30.0f, 4, fs, k0, N);
    std::printf ("    1x %.1f | 4x %.1f | 16x %.1f dBFS\n", tor1, tor4, tor16);

    CHECK (hot4 < -70.0,  "4x audible alias floor below -70 dBFS at +14 dB overdrive");
    CHECK (hot8 < -80.0,  "8x audible alias floor below -80 dBFS at +14 dB overdrive");
    CHECK (tor4 < tor1 - 20.0, "4x reduces audible aliasing vs 1x by >20 dB under torture");
    CHECK (tor4 < -45.0,  "4x floor bounded even at +24 dB overdrive");
    CHECK (tor16 < -75.0, "16x stays clean even at +24 dB overdrive");
}

//==============================================================================
static void testHarmonicSignatures()
{
    beginTest ("per-model harmonic signatures");
    const double fs = 48000.0;
    const size_t N = 16384;
    const int total = (int) N + 8192;
    const int k0 = 341;                         // ≈999 Hz exact bin
    const double f0 = (double) k0 * fs / (double) N;

    double profile[kNumTubeModels][4];          // H2..H5 relative to H1, dB
    for (int m = 0; m < kNumTubeModels; ++m)
    {
        EngineParams p = cleanParams();
        p.model = m;
        p.driveDb = 22.0f;
        p.sag = 0.4f;
        p.osIndex = 2;

        auto in = makeSine (1, total, fs, f0, 0.25f); // -12 dBFS
        auto out = runEngine (p, fs, in);
        auto mag = spectrumDb (out[0].data() + 8192, N);

        auto peakNear = [&] (int bin)
        {
            double best = -300.0;
            for (int d = -3; d <= 3; ++d)
                best = std::max (best, mag[(size_t) (bin + d)]);
            return best;
        };
        const double h1 = peakNear (k0);
        std::printf ("  %-7s H1 %6.1f | rel:", tubeModel (m).name, h1);
        for (int h = 2; h <= 5; ++h)
        {
            profile[m][h - 2] = peakNear (h * k0) - h1;
            std::printf (" H%d %6.1f", h, profile[m][h - 2]);
        }
        std::printf ("\n");
        CHECK (profile[m][0] > -80.0 || profile[m][1] > -80.0, "model actually distorts at drive 22");
    }

    // Models must be meaningfully different from each other.
    for (int a = 0; a < kNumTubeModels; ++a)
        for (int b = a + 1; b < kNumTubeModels; ++b)
        {
            double dist = 0.0;
            for (int h = 0; h < 4; ++h)
                dist += std::fabs (profile[a][h] - profile[b][h]);
            CHECK (dist > 6.0, "models are sonically distinct (harmonic profiles differ)");
        }

    // Character checks: triodes leans 2nd, pentode/beam lean odd.
    CHECK (profile[1][0] > profile[1][1] - 3.0, "G-300 has strong 2nd harmonic");
    CHECK (profile[2][1] > profile[2][0] - 6.0, "SL-6 has prominent 3rd harmonic");
}

//==============================================================================
static void testAutoGain()
{
    beginTest ("auto gain compensation");
    const double fs = 48000.0;
    const int n = 48000;

    for (int m = 0; m < kNumTubeModels; ++m)
        for (float drive : { 0.0f, 12.0f, 24.0f, 36.0f })
        {
            EngineParams p = cleanParams();
            p.model = m;
            p.driveDb = drive;
            p.sag = 0.5f;
            p.autoGain = true;
            p.osIndex = 1;

            auto in = makeSine (2, n, fs, 1000.0, 0.125f); // -18 dBFS
            auto out = runEngine (p, fs, in);

            const double rin = rmsOf (in[0].data() + n / 2, n / 2);
            const double rout = rmsOf (out[0].data() + n / 2, n / 2);
            const double err = 20.0 * std::log10 (rout / rin);
            if (std::fabs (err) > 1.5)
                std::printf ("  model %d drive %4.1f: level error %+.2f dB\n", m, drive, err);
            CHECK (std::fabs (err) < 1.5, "auto-gain holds loudness within +/-1.5 dB");
        }
    std::printf ("  all models x drives within +/-1.5 dB\n");
}

//==============================================================================
static void testDynamicBehaviour()
{
    beginTest ("programme-dependent dynamics (sag/bias)");
    const double fs = 48000.0;
    const int n = (int) fs * 3;

    // Loud burst then a quiet probe tone: if the model is dynamic, the probe's
    // gain right after the burst differs from its gain later (recovery).
    std::vector<std::vector<float>> in (1, std::vector<float> (n));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const float probe = 0.05f * (float) std::sin (2.0 * M_PI * 997.0 * i / fs);
        const float burst = (t > 0.5 && t < 1.0) ? 0.7f * (float) std::sin (2.0 * M_PI * 220.0 * i / fs) : 0.0f;
        in[0][i] = probe + burst;
    }

    EngineParams p = cleanParams();
    p.model = 1;             // G-300, generous sag
    p.driveDb = 24.0f;
    p.sag = 1.0f;
    p.osIndex = 1;

    auto out = runEngine (p, fs, in);

    const int wEarly = (int) (1.05 * fs);      // right after burst
    const int wLate  = (int) (2.5 * fs);       // fully recovered
    const int wLen   = (int) (0.2 * fs);
    const double gEarly = rmsOf (out[0].data() + wEarly, wLen);
    const double gLate  = rmsOf (out[0].data() + wLate, wLen);
    const double diffDb = 20.0 * std::log10 (gLate / gEarly);
    std::printf ("  probe gain: after burst vs recovered: %+.2f dB\n", diffDb);
    CHECK (diffDb > 0.2, "sag compresses after the burst and recovers (programme-dependent)");

    // With sag + dynamics off (sag 0), behaviour must be near-static.
    p.sag = 0.0f;
    auto out2 = runEngine (p, fs, in);
    const double d2 = 20.0 * std::log10 (rmsOf (out2[0].data() + wLate, wLen)
                                       / rmsOf (out2[0].data() + wEarly, wLen));
    std::printf ("  same with sag=0: %+.2f dB\n", d2);
    CHECK (std::fabs (d2) < std::fabs (diffDb), "sag knob controls the dynamic depth");
}

//==============================================================================
static void testDrift()
{
    beginTest ("component variation / drift");
    const double fs = 48000.0;
    const int n = 48000;
    auto in = makeSine (2, n, fs, 500.0, 0.25f);

    EngineParams p = cleanParams();
    p.driveDb = 18.0f;
    p.model = 2;

    p.drift = 0.0f;
    auto out0 = runEngine (p, fs, in);
    double chDiff = 0.0;
    for (int i = 0; i < n; ++i)
        chDiff = std::max (chDiff, (double) std::fabs (out0[0][i] - out0[1][i]));
    std::printf ("  drift=0: max channel difference %.2e\n", chDiff);
    CHECK (chDiff < 1.0e-7, "drift=0 leaves channels bit-identical");

    p.drift = 1.0f;
    auto out1 = runEngine (p, fs, in);
    const double r0 = rmsOf (out1[0].data() + n / 2, n / 2);
    const double r1 = rmsOf (out1[1].data() + n / 2, n / 2);
    const double lvlDiff = std::fabs (20.0 * std::log10 (r0 / r1));
    double diff = 0.0;
    for (int i = n / 2; i < n; ++i)
        diff += std::fabs (out1[0][i] - out1[1][i]);
    std::printf ("  drift=1: channel level diff %.2f dB, waveform difference present: %s\n",
                 lvlDiff, diff > 0.01 ? "yes" : "no");
    CHECK (diff > 0.01, "drift=1 decorrelates channels");
    CHECK (lvlDiff < 1.2, "drift keeps channel balance within 1.2 dB (mono-safe)");
}

//==============================================================================
static void testBlockSizeInvariance()
{
    beginTest ("block-size invariance");
    const double fs = 48000.0;
    const int n = 16384;
    auto in = makeSine (2, n, fs, 1234.5, 0.3f);

    EngineParams p = cleanParams();
    p.driveDb = 20.0f;
    p.sag = 0.7f;
    p.model = 3;
    p.osIndex = 2;

    auto a = runEngine (p, fs, in, 512);

    // irregular chopping
    TurboTubesEngine engine;
    engine.setParams (p);
    engine.prepare (fs, 512, 2);
    engine.setParams (p);
    auto b = in;
    const int sizes[] = { 1, 7, 64, 100, 3, 256, 512, 33 };
    int pos = 0, si = 0;
    while (pos < n)
    {
        const int m = std::min (sizes[si++ % 8], n - pos);
        float* ptrs[2] = { b[0].data() + pos, b[1].data() + pos };
        engine.process (ptrs, m);
        pos += m;
    }

    double maxErr = 0.0;
    for (int i = 0; i < n; ++i)
        maxErr = std::max (maxErr, (double) std::fabs (a[0][i] - b[0][i]));
    std::printf ("  512-block vs irregular chop: max diff %.2e\n", maxErr);
    CHECK (maxErr < 1.0e-6, "output independent of host buffer slicing");
}

//==============================================================================
static void testFuzz()
{
    beginTest ("stability fuzz (params x rates x block sizes)");
    std::mt19937 rng (1234);
    auto frand = [&] (float lo, float hi)
    {
        return lo + (hi - lo) * (float) rng() / (float) rng.max();
    };

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    bool allFinite = true;
    float maxAbs = 0.0f;

    for (int iter = 0; iter < 60; ++iter)
    {
        EngineParams p;
        p.inTrimDb = frand (-24, 24);
        p.driveDb = frand (0, 36);
        p.biasKnob = frand (-1, 1);
        p.sag = frand (0, 1);
        p.inertia = frand (0, 1);
        p.lowCutHz = frand (20, 300);
        p.tiltDb = frand (-6, 6);
        p.highCutHz = frand (2000, 20000);
        p.mix = frand (0, 1);
        p.outTrimDb = frand (-24, 24);
        p.width = frand (0, 2);
        p.msBal = frand (-1, 1);
        p.drift = frand (0, 1);
        p.model = (int) (rng() % 5);
        p.osIndex = (int) (rng() % 5);
        p.pristine = (rng() & 1) != 0;
        p.turbo = (rng() & 1) != 0;
        p.autoGain = (rng() & 1) != 0;
        p.delta = (rng() % 8) == 0;
        p.msMode = (rng() & 1) != 0;
        p.scHpHz = frand (20, 500);
        p.scLpHz = frand (1000, 20000);

        const double fs = rates[rng() % 4];
        const int n = 4096;
        std::vector<std::vector<float>> in (2, std::vector<float> (n));
        for (auto& chn : in)
            for (auto& s : chn)
                s = frand (-1.5f, 1.5f); // hotter than full scale on purpose

        TurboTubesEngine engine;
        engine.setParams (p);
        engine.prepare (fs, 1024, 2);
        engine.setParams (p);

        int pos = 0;
        while (pos < n)
        {
            const int m = std::min ((int) (1 + rng() % 1024), n - pos);
            float* ptrs[2] = { in[0].data() + pos, in[1].data() + pos };
            engine.process (ptrs, m);
            // mutate a param mid-stream sometimes (including config switches)
            if ((rng() & 3) == 0)
            {
                p.driveDb = frand (0, 36);
                p.osIndex = (int) (rng() % 5);
                p.model = (int) (rng() % 5);
                p.bypass = (rng() & 1) != 0;
                engine.setParams (p);
            }
            pos += m;
        }
        for (const auto& chn : in)
            for (float s : chn)
            {
                if (! std::isfinite (s)) allFinite = false;
                maxAbs = std::max (maxAbs, std::fabs (s));
            }
    }
    std::printf ("  60 randomized runs: finite=%s, max |out| = %.2f\n",
                 allFinite ? "yes" : "NO", maxAbs);
    CHECK (allFinite, "no NaN/Inf ever");
    // Worst-case *linear* gain of the settings above is 1.5 in × +24 dB trim
    // × +24 dB trim × width 2 ≈ 750. The guard is "never exceeds what a
    // linear gain chain would do" — i.e. bounded, not exploding.
    CHECK (maxAbs < 768.0f, "output bounded under absurd settings");
}

//==============================================================================
static void testSilenceAndDenormals()
{
    beginTest ("silence / denormal safety");
    EngineParams p = cleanParams();
    p.driveDb = 36.0f;
    p.sag = 1.0f;
    p.model = 4;
    p.turbo = true;

    const int n = 48000;
    std::vector<std::vector<float>> in (2, std::vector<float> (n, 0.0f));
    // a vanishing tail
    for (int i = 0; i < 1000; ++i)
        in[0][i] = in[1][i] = 1.0e-30f;

    auto out = runEngine (p, 48000.0, in);
    float maxTail = 0.0f;
    bool finite = true;
    for (int i = n / 2; i < n; ++i)
    {
        if (! std::isfinite (out[0][i])) finite = false;
        maxTail = std::max (maxTail, std::fabs (out[0][i]));
    }
    std::printf ("  tail max %.3e\n", maxTail);
    CHECK (finite, "silence stays finite");
    CHECK (maxTail < 1.0e-6f, "engine settles to true silence (no denormal ring-on)");
}

//==============================================================================
static void testToneAndWidth()
{
    beginTest ("tone filters + width sanity");
    const double fs = 48000.0;
    const int n = 32768;

    { // high cut
        EngineParams p = cleanParams();
        p.highCutHz = 5000.0f;
        auto in = makeSine (1, n, fs, 10000.0, 0.25f);
        auto out = runEngine (p, fs, in);
        const double att = 20.0 * std::log10 (rmsOf (out[0].data() + n / 2, n / 2)
                                            / rmsOf (in[0].data() + n / 2, n / 2));
        std::printf ("  10 kHz through 5 kHz high-cut: %.1f dB\n", att);
        CHECK (att < -9.0, "12 dB/oct high-cut bites");
    }
    { // width = 0 folds to mono
        EngineParams p = cleanParams();
        p.width = 0.0f;
        std::mt19937 rng (7);
        std::vector<std::vector<float>> in (2, std::vector<float> (n));
        for (int i = 0; i < n; ++i)
        {
            in[0][i] = 0.3f * ((float) rng() / rng.max() - 0.5f);
            in[1][i] = 0.3f * ((float) rng() / rng.max() - 0.5f);
        }
        auto out = runEngine (p, fs, in);
        double d = 0.0;
        for (int i = n / 2; i < n; ++i)
            d = std::max (d, (double) std::fabs (out[0][i] - out[1][i]));
        std::printf ("  width=0 max L-R difference: %.2e\n", d);
        CHECK (d < 1.0e-5, "width=0 collapses the wet path to mono");
    }
}

//==============================================================================
static void testSidechain()
{
    beginTest ("external sidechain drives the dynamics");
    const double fs = 48000.0;
    const int n = (int) fs * 2;

    // Programme: steady moderate tone (in the nonlinear region). SC: pumping bursts.
    auto in = makeSine (1, n, fs, 997.0, 0.125f);
    std::vector<std::vector<float>> sc (1, std::vector<float> (n, 0.0f));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const bool on = std::fmod (t, 0.5) < 0.25;
        sc[0][i] = on ? 0.8f * (float) std::sin (2.0 * M_PI * 80.0 * i / fs) : 0.0f;
    }

    EngineParams p = cleanParams();
    p.model = 1;
    p.driveDb = 12.0f;   // mid-curve: bias shifts are clearly audible here
    p.sag = 1.0f;
    p.inertia = 0.25f;   // faster envelopes so the off-window is truly recovered
    p.scExternal = true;

    TurboTubesEngine engine;
    engine.setParams (p);
    engine.prepare (fs, 512, 1);
    engine.setParams (p);
    auto out = in;
    for (int pos = 0; pos < n; pos += 512)
    {
        const int m = std::min (512, n - pos);
        float* ptrs[1] = { out[0].data() + pos };
        const float* scp[1] = { sc[0].data() + pos };
        engine.process (ptrs, m, scp, 1);
    }

    // The SC bursts push the bias, which blooms the 2nd harmonic of the
    // programme tone. Compare H2 during SC-on vs SC-off windows.
    auto h2At = [&] (double tSec)
    {
        const size_t N = 4096;
        auto mag = spectrumDb (out[0].data() + (int) (tSec * fs), N);
        const size_t bin = (size_t) std::llround (2.0 * 997.0 * N / fs);
        double best = -300.0;
        for (int d = -4; d <= 4; ++d)
            best = std::max (best, mag[bin + (size_t) (d + 4) - 4]);
        return best;
    };
    const double h2On  = h2At (1.55);   // inside an SC burst
    const double h2Off = h2At (1.90);   // SC silent, tube recovered
    std::printf ("  H2 with SC burst %.1f dB vs recovered %.1f dB (bloom %.1f dB)\n",
                 h2On, h2Off, h2On - h2Off);
    CHECK (h2On - h2Off > 1.0, "external sidechain blooms the tube's 2nd harmonic");
}

//==============================================================================
static void benchmark()
{
    beginTest ("CPU benchmark (informational)");
    const double fs = 48000.0;
    const int n = (int) fs * 10;
    auto in = makeSine (2, n, fs, 220.0, 0.4f);

    EngineParams p = cleanParams();
    p.driveDb = 24.0f;
    p.sag = 0.6f;
    p.model = 3;
    p.turbo = true;
    p.osIndex = 2; // 4x
    p.autoGain = true;
    p.drift = 0.5f;

    TurboTubesEngine engine;
    engine.setParams (p);
    engine.prepare (fs, 512, 2);
    engine.setParams (p);

    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < n; pos += 512)
    {
        const int m = std::min (512, n - pos);
        float* ptrs[2] = { in[0].data() + pos, in[1].data() + pos };
        engine.process (ptrs, m);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double> (t1 - t0).count();
    const double xrt = 10.0 / sec;
    std::printf ("  4x pristine + TURBO, stereo 48k: %.2f s for 10 s audio → %.1fx realtime\n", sec, xrt);
    CHECK (xrt > 8.0, "light enough for many instances");
}

//==============================================================================
int main()
{
    std::printf ("TURBO TUBES dsp test suite\n==========================\n");

    testHalfbandSpecs();
    testLatencyAndAlignment();
    testAliasing();
    testHarmonicSignatures();
    testAutoGain();
    testDynamicBehaviour();
    testDrift();
    testBlockSizeInvariance();
    testSidechain();
    testToneAndWidth();
    testSilenceAndDenormals();
    testFuzz();
    benchmark();

    if (failures == 0)
        std::printf ("\nALL TESTS PASSED\n");
    else
        std::printf ("\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
