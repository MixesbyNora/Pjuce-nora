#pragma once

#include "Voicing.h"

namespace TransformerVoicing
{
using Descriptor = Voicing::Descriptor;
using Word = Voicing::Word;
struct Schema
{
    static constexpr int version = 5;
    static constexpr const char* model = "lumiere-vtb1148-workbench-v1";
    // Workbench files of the retired Magnetic engine (versions 1-4). They are
    // recognized so a session can load, but their values do not apply to VTB1148.
    static constexpr std::array<const char*, 4> retiredModels { "transformer-tanh-residual-adaa-v1",
        "transformer-magnetic-play-v2", "lumiere-voicing-workbench-v3", "lumiere-voicing-workbench-v4" };
    // The workbench schema is independent of host automation and DSP implementation.
    // VTB1148 fine-tuning, each relative to the published-data calibration
    // (vtb1148OutputC), so defaults reproduce it exactly. New controls require an
    // explicit DSP/state migration decision.
    static constexpr std::array descriptors {
        // Moves where saturation starts; + is later (cleaner). Scales flux capacity only.
        Descriptor { "onsetDb", "Saturation onset", "dB", -12.0, 12.0, 0.25, 0.0, 1.0 },
        // Second-harmonic level via hysteresis asymmetry; H2 scales about 1:1 in dB.
        Descriptor { "evenHarmonicsDb", "Even harmonics", "dB", -24.0, 24.0, 0.25, 0.0, 1.0 },
        // Hysteresis width: the amount of low-level distortion that falls with level.
        Descriptor { "grainDb", "Low-level grain", "dB", -12.0, 12.0, 0.25, 0.0, 1.0 },
        // Hysteretic share of the core, as a percentage of the calibrated 0.881 (114% reaches 1).
        Descriptor { "memoryPercent", "Hysteresis memory", "%", 0.0, 114.0, 1.0, 100.0, 1.0 }
    };
};
inline constexpr int version = Schema::version;
inline constexpr const char* model = Schema::model;
inline constexpr const auto& retiredModels = Schema::retiredModels;
inline constexpr const auto& descriptors = Schema::descriptors;
inline constexpr std::size_t onset = 0, evenHarmonics = 1, grain = 2, memory = 3;
using Voicing::steps;
using Voicing::bits;
using Voicing::bounded;
inline constexpr unsigned valueBits = Voicing::valueBits<Schema>;
inline constexpr Word valueMask = Voicing::valueMask<Schema>;
using Values = Voicing::Values<Schema>;
inline double dspValue (Values values, std::size_t i) noexcept { return Voicing::dspValue<Schema> (values, i); }
inline Word encode (Values values) noexcept { return Voicing::encode<Schema> (values); }
inline Values decode (Word word) noexcept { return Voicing::decode<Schema> (word); }
inline Values sanitized (Values values) noexcept { return Voicing::sanitized<Schema> (values); }
inline bool equal (Values a, Values b) noexcept { return Voicing::equal<Schema> (a, b); }
}
