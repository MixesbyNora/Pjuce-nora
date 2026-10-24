#pragma once

#include <array>

namespace CompressorParameters
{
struct Specification
{
    const char* id;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    float step;
};

// Edit parameter ranges, defaults, and resolution here. The attack-coefficient
// table uses these same bounds and steps, so it stays in sync with automation.
inline constexpr Specification input { "input", "Input", "dB", -12.0f, 12.0f, 0.0f, 0.01f };
inline constexpr Specification threshold { "threshold", "Threshold", "dB", -40.0f, 0.0f, -18.0f, 0.01f };
inline constexpr Specification attack { "attack", "Attack", "ms", 0.2f, 30.0f, 5.0f, 0.1f };
inline constexpr Specification release { "release", "Release", "ms", 50.0f, 2000.0f, 200.0f, 1.0f };
inline constexpr Specification makeup { "makeup", "Makeup", "dB", 0.0f, 24.0f, 0.0f, 0.01f };
inline constexpr std::array specifications { input, threshold, attack, release, makeup };
// Appended to the host layout separately to preserve all existing parameter indices.
inline constexpr Specification fbff { "fbff", "FB/FF", "", 0.0f, 100.0f, 100.0f, 0.1f };
// Level into the detector only (docs/GainTube.md); appended with version hint 8.
inline constexpr Specification calibration { "calibration", "Calibration", "dB", -24.0f, 24.0f, 0.0f, 0.1f };
inline constexpr double fbffSmoothingSeconds = 0.02;

// Program-dependent release: from fullSpeedGrDb of reduction, Release Mod at 100%
// speeds release from the Release time down to fastestReleaseMs (owner decision,
// 2026-10-24: never faster, so the gain cannot follow the waveform; docs/Overdrive.md).
inline constexpr float fullSpeedGrDb = 12.0f;
inline constexpr double fastestReleaseMs = 20.0;
inline constexpr Specification releaseMod { "releaseMod", "Release Mod", "%", 0.0f, 100.0f, 50.0f, 0.1f };
inline constexpr const char* scFilterId = "scFilter";

constexpr std::size_t stepCount (const Specification& spec) noexcept
{
    return static_cast<std::size_t> ((spec.maximum - spec.minimum) / spec.step + 0.5f) + 1;
}

struct Values
{
    float inputDb = input.defaultValue;
    float thresholdDb = threshold.defaultValue;
    float attackMs = attack.defaultValue;
    float releaseMs = release.defaultValue;
    float makeupDb = makeup.defaultValue;
    int scFilterMode = 0;
    float fbffPercent = fbff.defaultValue;
    float calibrationDb = calibration.defaultValue;
    float releaseModPercent = releaseMod.defaultValue;
};
}
