#pragma once

namespace TransformerSettings
{
inline constexpr double shelfFrequencyHz = 120.0;
inline constexpr double shelfGainDb = 12.0;
inline constexpr double shelfQ = 0.5;
inline constexpr double minimumDriveDb = -24.0;
inline constexpr double maximumDriveDb = 12.0;
inline constexpr double amountRampSeconds = 0.02;
inline constexpr double bypassRampSeconds = 0.01;
inline constexpr double adaaDifferenceThreshold = 1.0e-6;
inline constexpr double voicingFadeSeconds = 0.02;
inline constexpr double matchWindowSeconds = 0.25;
inline constexpr double matchGainSeconds = 0.1;
inline constexpr double matchMaximumDb = 12.0;
inline constexpr double matchSilencePower = 1.0e-12;

inline constexpr float calibrationMinimumDb = -12.0f;
inline constexpr float calibrationMaximumDb = 12.0f;
inline constexpr float emphasisMinimumDb = 0.0f;
inline constexpr float emphasisMaximumDb = 24.0f;
inline constexpr float cornerMinimumHz = 30.0f;
inline constexpr float cornerMaximumHz = 500.0f;
}
