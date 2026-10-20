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
}
