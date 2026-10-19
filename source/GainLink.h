#pragma once

#include <algorithm>

// Pure compensation rule. The existing offset is retained until a range limit
// is reached; it works identically for IN driving OUT or OUT driving IN.
struct GainLink
{
    static float compensate (float otherGainDb, float changedByDb, bool linked,
                            float minimumDb, float maximumDb) noexcept
    {
        return linked ? std::clamp (otherGainDb - changedByDb, minimumDb, maximumDb) : otherGainDb;
    }
};
