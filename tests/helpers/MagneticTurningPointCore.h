#pragma once

#include <experimental/PlayPreisachCore.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace MagneticOversampling
{
// Test-only reconstruction experiment. Three-sample quadratic extrema refine
// the magnetic history; the output remains a point evaluation at the current H.
// This estimates missed extrema, not a band-limited reconstruction guarantee.
class TurningPointCore
{
public:
    explicit TurningPointCore (MagneticPrototype::PlayPreisachCore::Parameters parameters = {}) noexcept : core (parameters) {}
    void reset() noexcept { core.reset(); previous = older = 0.0; historySamples = 0; }
    double processField (double field) noexcept
    {
        if ((std::bit_cast<std::uint64_t> (field) & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL)
            return core.getMagnetization();
        field = std::clamp (field, -MagneticPrototype::Settings::maximumField, MagneticPrototype::Settings::maximumField);
        // The reset sentinel is not an input sample. Require three actual
        // fields before fitting, or startup can invent an extremum/remanence.
        if (historySamples >= 2 && ((previous > older && previous > field) || (previous < older && previous < field)))
        {
            const auto curvature = (older - previous) + (field - previous);
            if (std::abs (curvature) > 1.0e-24)
            {
                const auto offset = std::clamp (0.5 * (older - field) / curvature, -0.5, 0.5);
                const auto extremum = previous - 0.25 * (older - field) * offset;
                core.processField (extremum);
            }
        }
        older = previous;
        previous = field;
        historySamples = std::min (historySamples + 1, 2);
        return core.processField (field);
    }
    double getMagnetization() const noexcept { return core.getMagnetization(); }

private:
    MagneticPrototype::PlayPreisachCore core;
    double previous = 0.0, older = 0.0;
    int historySamples = 0;
};
}
