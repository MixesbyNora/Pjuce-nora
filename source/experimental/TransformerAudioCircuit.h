#pragma once

#include "PlayPreisachCore.h"
#include "TransformerAudioSettings.h"
#include "ConstitutiveLookup.h"
#include <cstdint>

namespace MagneticPrototype
{
// Shared normalized 1:1 voltage-driven magnetizing branch. Source/load
// insertion loss is normalized out; no leakage, capacitance or hardware fit.
// D*x = H + tau*d(H + k*M)/dt; induced output y = x - H/D.
class TransformerAudioCircuit
{
public:
    struct Parameters
    {
        double cornerHz = AudioSettings::cornerHz;
        double driveDb = AudioSettings::driveDb;
        double magnetizationWeight = AudioSettings::magnetizationWeight;
        PlayPreisachCore::Parameters core;
    };
    struct Trial { double magnetization = 0.0, derivative = 0.0; };
    struct Diagnostics
    {
        double maximumEquationResidual = 0.0;
        int maximumIterations = 0;
        std::uint64_t solverFailures = 0, invalidInputs = 0, limitedInputs = 0;
    };

    void prepare (double internalSampleRate, Parameters, bool linearReference = false, bool exactConstitutive = false) noexcept;
    void setDriveGain (double linearGain) noexcept { drive = AudioSettings::fieldScale * linearGain; }
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
    struct Evaluation
    {
        Trial trial;
        double reversible = 0.0;
        std::array<double, Settings::operatorCount> positions {}, values {};
    };
    void evaluate (double field, Evaluation&) const noexcept;
    void commit (const Evaluation&) noexcept;
    PlayPreisachCore core;
    ConstitutiveLookup density;
    Parameters parameters;
    Diagnostics diagnostics;
    double field = 0.0, magnetization = 0.0, previousVoltage = 0.0;
    double q = 0.0, tau = 0.0, drive = 1.0, linearSlope = 1.0;
    // Cached constitutive values at the last committed coordinates.
    std::array<double, Settings::operatorCount> committedValues {};
    double committedReversible = 0.0, committedClippedField = 0.0, committedTrialMagnetization = 0.0;
    bool linear = false;
    bool exact = false;
    double lastSlope = 0.0;
};
}
