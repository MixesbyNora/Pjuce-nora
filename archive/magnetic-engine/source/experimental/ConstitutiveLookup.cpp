#include "ConstitutiveLookup.h"

namespace MagneticPrototype
{
ConstitutiveLookup::ConstitutiveLookup() noexcept
{
    static const Table prepared = []
    {
        Table result {};
        for (int i = 0; i < segments; ++i)
        {
            const auto t = std::tanh ((static_cast<double> (i) + 0.5) * spacing);
            const auto square = t * t, slope = 1.0 - square;
            result[static_cast<std::size_t> (i)] = { t, slope, -t * slope,
                slope * (3.0 * square - 1.0) / 3.0,
                t * slope * (2.0 - 3.0 * square) / 3.0,
                slope * (2.0 - 15.0 * square + 15.0 * square * square) / 15.0 };
        }
        return result;
    }();
    table = &prepared;
}
}
