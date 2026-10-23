#pragma once

namespace TransformerSettings
{
inline constexpr double tailSeconds = 2.0;
// VTB1148 (variant C): Amount maps to extra drive over its calibrated nominal;
// 100% = +18 dB, the level its +17 dBu onset was chosen at.
inline constexpr double vtbMinimumDriveDb = -24.0;
inline constexpr double vtbMaximumDriveDb = 18.0;
inline constexpr double amountRampSeconds = 0.02;
inline constexpr double bypassRampSeconds = 0.01;
inline constexpr double voicingFadeSeconds = 0.02;
inline constexpr double voicingWarmSeconds = 0.02;
inline constexpr double voicingRestSeconds = 0.1;
inline constexpr double matchWindowSeconds = 0.25;
inline constexpr double matchGainSeconds = 0.1;
inline constexpr double matchMaximumDb = 12.0;
inline constexpr double matchSilencePower = 1.0e-12;

}
