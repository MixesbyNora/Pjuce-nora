// Frozen pre-optimization v1 circuit; keep independent of the candidate implementation.
#pragma once

#include <experimental/PlayPreisachCore.h>
#include "TransformerAudioReferenceSettings.h"
#include <cstdint>

namespace MagneticPrototype
{
// Tests-only normalized 1:1 voltage-driven magnetizing branch. Source/load
// insertion loss is normalized out; no leakage, capacitance or hardware fit.
// D*x = H + tau*d(H + k*M)/dt; induced output y = x - H/D.
class TransformerAudioReference
{
public:
    struct Parameters
    {
        double cornerHz = FrozenAudioSettings::cornerHz;
        double driveDb = FrozenAudioSettings::driveDb;
        double magnetizationWeight = FrozenAudioSettings::magnetizationWeight;
        PlayPreisachCore::Parameters core;
    };
    struct Trial { double magnetization = 0.0, derivative = 0.0; };
    struct Diagnostics
    {
        double maximumEquationResidual = 0.0;
        int maximumIterations = 0;
        std::uint64_t solverFailures = 0, invalidInputs = 0, limitedInputs = 0;
    };

    void prepare (double internalSampleRate, Parameters, bool linearReference = false) noexcept;
    void reset() noexcept;
    double processSample (double voltage) noexcept;
    // Non-mutating query of the canonical constitutive law and branch slope.
    Trial evaluateTrial (double field) const noexcept;
    double getField() const noexcept { return field; }
    double getMagnetization() const noexcept { return magnetization; }
    double getFlux() const noexcept { return linear ? linearSlope * field : field + parameters.magnetizationWeight * magnetization; }
    double getTimeConstant() const noexcept { return tau; }
    double getDrive() const noexcept { return drive; }
    Parameters getParameters() const noexcept { return parameters; }
    const Diagnostics& getDiagnostics() const noexcept { return diagnostics; }
    const PlayPreisachCore& getCore() const noexcept { return core; }

private:
    PlayPreisachCore core;
    Parameters parameters;
    Diagnostics diagnostics;
    double field = 0.0, magnetization = 0.0, previousVoltage = 0.0;
    double q = 0.0, tau = 0.0, drive = 1.0, linearSlope = 1.0;
    bool linear = false;
};
}
