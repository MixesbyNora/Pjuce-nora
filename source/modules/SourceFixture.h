#pragma once
#include "TransformerModule.h"

namespace ModuleSpike
{
// TEST FIXTURE ONLY. Behavioral source; not a tube model or a product part.
class SourceFixture final : public Module
{
public:
    bool prepare (Spec spec) noexcept override { return spec.valid(); }
    void reset() noexcept override {}
    void configure (double ohms, double loadAwareAmount = 0) noexcept
    {
        resistance = bounded (ohms, 0.01, 1.0e8, 1000);
        drive = 4.0 * (1.0 + 0.5 * bounded (loadAwareAmount, 0, 1, 0));
    }
    double processSample (double x, double supply = 1) const noexcept
    {
        const auto rail = bounded (supply, 0.1, 10, 1);
        return rail * std::tanh (drive * sanitized (x) / rail) / drive;
    }
    void process (Block block, Modulation modulation = {}) noexcept override
    {
        for (int c = 0; c < block.channels; ++c)
            for (std::size_t n = 0; n < block.samples; ++n)
                block.data[static_cast<std::size_t> (c)][n] = processSample (block.data[static_cast<std::size_t> (c)][n], modulation.supplyRail);
    }
    Info info() const noexcept override
    {
        Info result; result.partId = "fixture.saturating-source"; result.version = "1-spike";
        result.nonlinear = true; result.preferredFactor = 4; result.output = Impedance::scalar (resistance); return result;
    }
private:
    double resistance = 1000, drive = 4;
};
}
