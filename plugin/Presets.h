// TURBO TUBES - plugin/Presets.h
// Factory presets. These are the manual: each one demonstrates a workflow the
// plugin is built for (parallel crush, sidechain bloom, M/S drive, zero-latency
// tracking, 16x mastering...). Values are in plain parameter units.

#pragma once

#include <vector>
#include <juce_core/juce_core.h>
#include "Parameters.h"

namespace ttp
{

struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // unspecified params reset to default
};

inline const std::vector<Preset>& factoryPresets()
{
    using namespace id;
    static const std::vector<Preset> presets = {
        { "Init - Clean Warmth", {
            { tube, 0 }, { drive, 2.5f }, { sag, 25 }, { os, 2 } } },

        { "Vocal Silk (VT-101)", {
            { tube, 0 }, { drive, 2.8f }, { bias, 10 }, { sag, 20 },
            { highCut, 16000 }, { variance, 20 } } },

        { "Vocal Attitude (R-90)", {
            { tube, 4 }, { drive, 5.5f }, { sag, 45 }, { lowCut, 90 },
            { tilt, 1.5f }, { mix, 70 } } },

        { "Mix Bus Glue (G-300)", {
            { tube, 1 }, { drive, 1.8f }, { sag, 55 }, { inertia, 65 },
            { width, 105 }, { variance, 35 }, { os, 3 } } },

        { "Master Sheen (VT-101 16x)", {
            { tube, 0 }, { drive, 1.2f }, { sag, 15 }, { os, 4 },
            { mix, 60 }, { tilt, 0.5f } } },

        { "Drum Bus Punch (BB-88)", {
            { tube, 3 }, { drive, 4.5f }, { sag, 60 }, { inertia, 30 },
            { os, 1 }, { osQuality, 0 }, { mix, 85 } } },

        { "Parallel Drum Crush (R-90 TURBO)", {
            { tube, 4 }, { drive, 7.5f }, { turbo, 1 }, { mix, 35 },
            { lowCut, 120 }, { sag, 50 } } },

        { "Bass Thickener (G-300)", {
            { tube, 1 }, { drive, 4.2f }, { bias, 25 }, { sag, 40 },
            { highCut, 8000 }, { width, 80 } } },

        { "Kick-Triggered Bloom (ext SC)", {
            { tube, 1 }, { drive, 3.5f }, { sag, 85 }, { inertia, 60 },
            { scExt, 1 }, { scLp, 200 } } },

        { "Side Shimmer (M/S)", {
            { tube, 2 }, { drive, 4.0f }, { procMode, 1 }, { msBal, 60 },
            { width, 130 }, { sag, 30 } } },

        { "Mid Focus (M/S)", {
            { tube, 0 }, { drive, 3.0f }, { procMode, 1 }, { msBal, -50 },
            { width, 90 }, { sag, 30 } } },

        { "Tape-ish Motion (BB-88)", {
            { tube, 3 }, { drive, 3.2f }, { sag, 70 }, { inertia, 80 },
            { variance, 60 }, { highCut, 15000 } } },

        { "Edge & Air (SL-6)", {
            { tube, 2 }, { drive, 5.0f }, { tilt, 2.0f }, { lowCut, 60 },
            { osQuality, 0 } } },

        { "Fuzz Box (R-90 TURBO 16x)", {
            { tube, 4 }, { drive, 9.5f }, { turbo, 1 }, { os, 4 },
            { sag, 65 } } },

        { "Zero-Latency Tracking", {
            { tube, 0 }, { drive, 3.0f }, { os, 0 }, { sag, 30 } } },
    };
    return presets;
}

} // namespace ttp
