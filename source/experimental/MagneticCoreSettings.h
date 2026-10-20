#pragma once

#include <cstddef>

namespace MagneticPrototype::Settings
{
inline constexpr const char* model = "preisach-play-core-v1";
inline constexpr std::size_t operatorCount = 16;
inline constexpr double minimumRadius = 0.02;
inline constexpr double maximumRadius = 1.2;
inline constexpr double weightFalloff = 0.5;
inline constexpr double saturationField = 1.0;
inline constexpr double defaultMemory = 0.35;
inline constexpr double defaultWidth = 1.0;
inline constexpr double minimumMemory = 0.0;
inline constexpr double maximumMemory = 1.0;
inline constexpr double minimumWidth = 0.05;
inline constexpr double maximumWidth = 4.0;
inline constexpr double maximumField = 32.0;
}
