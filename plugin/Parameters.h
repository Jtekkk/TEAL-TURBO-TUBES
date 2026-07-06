// TURBO TUBES — plugin/Parameters.h
// Parameter IDs, ranges and the APVTS layout. Ranges are deliberately musical:
// frequency parameters run on log skews, DRIVE is a 0-10 knob that maps to
// 0-36 dB into the stage, and everything defaults to a usable starting point
// (4x pristine oversampling, auto-gain on, a touch of sag and variance).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/TubeModels.h"

namespace ttp
{

namespace id
{
    inline constexpr const char* inTrim    = "in_trim";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* tube      = "tube";
    inline constexpr const char* turbo     = "turbo";
    inline constexpr const char* bias      = "bias";
    inline constexpr const char* sag       = "sag";
    inline constexpr const char* inertia   = "inertia";
    inline constexpr const char* lowCut    = "low_cut";
    inline constexpr const char* tilt      = "tilt";
    inline constexpr const char* highCut   = "high_cut";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* outTrim   = "out_trim";
    inline constexpr const char* autoGain  = "auto_gain";
    inline constexpr const char* delta     = "delta";
    inline constexpr const char* os        = "os";
    inline constexpr const char* osQuality = "os_quality";
    inline constexpr const char* procMode  = "proc_mode";
    inline constexpr const char* width     = "width";
    inline constexpr const char* msBal     = "ms_bal";
    inline constexpr const char* variance  = "variance";
    inline constexpr const char* scExt     = "sc_ext";
    inline constexpr const char* scHp      = "sc_hp";
    inline constexpr const char* scLp      = "sc_lp";
    inline constexpr const char* bypass    = "bypass";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using P  = juce::AudioParameterFloat;
    using Pc = juce::AudioParameterChoice;
    using Pb = juce::AudioParameterBool;
    using Rng = juce::NormalisableRange<float>;

    auto dbRange   = Rng (-24.0f, 24.0f, 0.01f);
    auto pctRange  = Rng (0.0f, 100.0f, 0.1f);
    auto bipRange  = Rng (-100.0f, 100.0f, 0.1f);

    auto freqRange = [] (float lo, float hi)
    {
        Rng r (lo, hi, 0.01f);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    };

    auto db  = [] (float v, int) { return juce::String (v, 1) + " dB"; };
    auto pct = [] (float v, int) { return juce::String (v, 0) + " %"; };
    auto hz  = [] (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (v, 0) + " Hz";
    };

    juce::StringArray tubeNames;
    for (int i = 0; i < tt::kNumTubeModels; ++i)
        tubeNames.add (tt::tubeModel (i).name);

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    auto add = [&] (auto p) { params.push_back (std::move (p)); };

    add (std::make_unique<P> (juce::ParameterID (id::inTrim, 1), "Input Trim", dbRange, 0.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    add (std::make_unique<P> (juce::ParameterID (id::drive, 1), "Drive", Rng (0.0f, 10.0f, 0.01f), 3.3f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
             [] (float v, int) { return juce::String (v, 2); })));
    add (std::make_unique<Pc> (juce::ParameterID (id::tube, 1), "Tube", tubeNames, 1));
    add (std::make_unique<Pb> (juce::ParameterID (id::turbo, 1), "Turbo", false));
    add (std::make_unique<P> (juce::ParameterID (id::bias, 1), "Bias", bipRange, 0.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<P> (juce::ParameterID (id::sag, 1), "Sag", pctRange, 35.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<P> (juce::ParameterID (id::inertia, 1), "Inertia", pctRange, 50.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<P> (juce::ParameterID (id::lowCut, 1), "Low Cut", freqRange (20.0f, 300.0f), 20.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
             [hz] (float v, int n) { return v <= 21.0f ? juce::String ("Off") : hz (v, n); })));
    add (std::make_unique<P> (juce::ParameterID (id::tilt, 1), "Tilt", Rng (-6.0f, 6.0f, 0.01f), 0.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    add (std::make_unique<P> (juce::ParameterID (id::highCut, 1), "High Cut", freqRange (2000.0f, 20000.0f), 20000.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
             [hz] (float v, int n) { return v >= 19500.0f ? juce::String ("Off") : hz (v, n); })));
    add (std::make_unique<P> (juce::ParameterID (id::mix, 1), "Mix", pctRange, 100.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<P> (juce::ParameterID (id::outTrim, 1), "Output Trim", dbRange, 0.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    add (std::make_unique<Pb> (juce::ParameterID (id::autoGain, 1), "Auto Gain", true));
    add (std::make_unique<Pb> (juce::ParameterID (id::delta, 1), "Delta Listen", false));
    add (std::make_unique<Pc> (juce::ParameterID (id::os, 1), "Oversampling",
                               juce::StringArray { "1x", "2x", "4x", "8x", "16x" }, 2));
    add (std::make_unique<Pc> (juce::ParameterID (id::osQuality, 1), "OS Quality",
                               juce::StringArray { "Punchy", "Pristine" }, 1));
    add (std::make_unique<Pc> (juce::ParameterID (id::procMode, 1), "Processing Mode",
                               juce::StringArray { "Stereo", "Mid/Side" }, 0));
    add (std::make_unique<P> (juce::ParameterID (id::width, 1), "Width", Rng (0.0f, 200.0f, 0.1f), 100.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<P> (juce::ParameterID (id::msBal, 1), "M/S Drive Tilt", bipRange, 0.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<P> (juce::ParameterID (id::variance, 1), "Variance", pctRange, 25.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<Pb> (juce::ParameterID (id::scExt, 1), "External Sidechain", false));
    add (std::make_unique<P> (juce::ParameterID (id::scHp, 1), "SC Low Cut", freqRange (20.0f, 500.0f), 20.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
             [hz] (float v, int n) { return v <= 21.0f ? juce::String ("Off") : hz (v, n); })));
    add (std::make_unique<P> (juce::ParameterID (id::scLp, 1), "SC High Cut", freqRange (200.0f, 20000.0f), 20000.0f,
         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
             [hz] (float v, int n) { return v >= 19500.0f ? juce::String ("Off") : hz (v, n); })));
    add (std::make_unique<Pb> (juce::ParameterID (id::bypass, 1), "Bypass", false));

    return { params.begin(), params.end() };
}

} // namespace ttp
