#pragma once

#include "Voicing.h"

// Detector workbench: the vari-mu's sidechain (owner decision 2026-10-24,
// docs/Detector.md).
namespace DetectorVoicing
{
using Descriptor = Voicing::Descriptor;
struct Schema
{
    static constexpr int version = 1;
    static constexpr const char* model = "lumiere-detector-workbench-v1";
    static constexpr std::array<const char*, 0> retiredModels {};
    static constexpr std::array descriptors {
        // 12AL5: diodes charge a timing network whose voltage biases the gain tube,
        // so ratio and knee come from the tube's curve (the default). Digital: the
        // original law, a fixed 4:1 with a 6 dB knee and a dB-domain envelope.
        Descriptor { "detector", "Detector", "", 0.0, 1.0, 1.0, 0.0, 1.0, "12AL5|Digital law" },
        // The sidechain amplifier's gain, 12AL5 only: how hard the control voltage
        // moves with level, so how steeply it compresses. 0 dB follows the Digital
        // law's 4:1 near the default threshold with the 5670; -12 dB is gentle
        // (Manley Compress), +12 dB limits.
        Descriptor { "sidechainGainDb", "Sidechain gain", "dB", -12.0, 12.0, 0.1, 0.0, 1.0 }
    };
};
inline constexpr std::size_t detector = 0, sidechainGain = 1;
using Word = Voicing::Word;
using Values = Voicing::Values<Schema>;
inline double dspValue (Values values, std::size_t i) noexcept { return Voicing::dspValue<Schema> (values, i); }
inline Word encode (Values values) noexcept { return Voicing::encode<Schema> (values); }
inline Values decode (Word word) noexcept { return Voicing::decode<Schema> (word); }
inline Values sanitized (Values values) noexcept { return Voicing::sanitized<Schema> (values); }
inline bool equal (Values a, Values b) noexcept { return Voicing::equal<Schema> (a, b); }
}
