#pragma once

#include <array>

namespace SidechainFilterSettings
{
enum class Type { bypass, highPass, bandPass, lowShelf, highShelf, peak };
inline constexpr std::size_t maxStages = 3;
inline constexpr int defaultMode = 0;

struct Stage
{
    Type type = Type::bypass;
    double frequencyHz = 0.0;
    double gainDb = 0.0;
    double q = 1.0;
};

struct Mode
{
    const char* name;
    std::array<Stage, maxStages> stages;
};

// All detector EQ types, frequencies, gains, and Q values live here.
// Mode order is the stable scFilter parameter choice order.
inline constexpr std::array modes {
    Mode { "Off", {} },
    Mode { "HP 80 Hz", { Stage { Type::highPass, 80.0, 0.0, 0.7071 } } },
    Mode { "HP 120 Hz", { Stage { Type::highPass, 120.0, 0.0, 0.7071 } } },
    Mode { "Tilt Wide", { Stage { Type::lowShelf, 250.0, -5.76, 0.50 },
                            Stage { Type::highShelf, 2041.0, 5.76, 0.50 } } },
    Mode { "Tilt Focused", { Stage { Type::lowShelf, 160.0, -5.76, 0.68 },
                               Stage { Type::highShelf, 3499.0, 5.76, 0.68 } } },
    Mode { "Band 730 Hz", { Stage { Type::bandPass, 730.0, 0.0, 0.15 } } },
    Mode { "Custom", { Stage { Type::peak, 712.0, 5.76, 0.13 },
                         Stage { Type::peak, 15800.0, 5.36, 1.57 },
                         Stage { Type::lowShelf, 88.0, -11.09, 0.05 } } }
};
}
