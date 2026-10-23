#pragma once

#include "Voicing.h"

// Gain tube workbench: the vari-mu gain element (docs/GainTube.md).
namespace GainTubeVoicing
{
using Descriptor = Voicing::Descriptor;
struct Schema
{
    static constexpr int version = 1;
    static constexpr const char* model = "lumiere-gain-tube-workbench-v1";
    static constexpr std::array<const char*, 0> retiredModels {};
    static constexpr std::array descriptors {
        // GE 5670 (the standard Manley tube; sharper, distortion climbs with GR) or
        // GE 6386 (remote cut-off, the original Manley and Fairchild 670 tube; smoother).
        Descriptor { "tube", "Tube", "", 0.0, 1.0, 1.0, 0.0, 1.0, "5670|6386" },
        // Signal swing on the grids relative to 1 V peak per full-scale unit: more
        // swing, more tube distortion at every gain reduction. Level is unchanged.
        Descriptor { "gridLevelDb", "Grid level", "dB", -18.0, 18.0, 0.25, 0.0, 1.0 },
        // Current mismatch between the pair's two sections, trimmed at idle: even
        // harmonics, and thump as the bias moves.
        Descriptor { "imbalancePercent", "Pair imbalance", "%", 0.0, 20.0, 0.1, 0.0, 1.0 }
    };
};
inline constexpr std::size_t tube = 0, gridLevel = 1, imbalance = 2;
using Word = Voicing::Word;
using Values = Voicing::Values<Schema>;
inline double dspValue (Values values, std::size_t i) noexcept { return Voicing::dspValue<Schema> (values, i); }
inline Word encode (Values values) noexcept { return Voicing::encode<Schema> (values); }
inline Values decode (Word word) noexcept { return Voicing::decode<Schema> (word); }
inline Values sanitized (Values values) noexcept { return Voicing::sanitized<Schema> (values); }
inline bool equal (Values a, Values b) noexcept { return Voicing::equal<Schema> (a, b); }
}
