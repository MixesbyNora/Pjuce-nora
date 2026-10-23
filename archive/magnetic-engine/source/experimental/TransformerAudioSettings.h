#pragma once

namespace MagneticPrototype::AudioSettings
{
inline constexpr const char* model = "voltage-driven-play-transformer-v1";
inline constexpr double cornerHz = 5.0;
inline constexpr double fieldScale = 40.0;
inline constexpr double magnetizationWeight = 4.0;
inline constexpr double driveDb = 0.0;
inline constexpr double minimumCornerHz = 0.5, maximumCornerHz = 30.0;
inline constexpr double minimumDriveDb = -24.0, maximumDriveDb = 12.0;
inline constexpr double maximumMagnetizationWeight = 16.0;
inline constexpr double maximumInput = 16.0;
inline constexpr double minimumRate = 8000.0, maximumRate = 100000000.0;
inline constexpr double rootTolerance = 1.0e-13;
inline constexpr int newtonIterations = 12;
inline constexpr int maximumIterations = 96;
inline constexpr int oversamplingFactor = 16;
inline constexpr double targetInternalRate = 768000.0;
inline int factorForRate (double rate) noexcept
{
    auto factor = oversamplingFactor;
    while (factor > 1 && rate * (factor / 2) >= targetInternalRate)
        factor /= 2;
    return factor;
}
}
