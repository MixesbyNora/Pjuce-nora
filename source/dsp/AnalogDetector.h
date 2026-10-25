#pragma once

#include "../modules/TriodeModel.h"
#include <array>

// The vari-mu's analog sidechain (owner decision 2026-10-24, docs/Detector.md):
// a sidechain amplifier drives 12AL5 diodes, full-wave (one per polarity), through
// the attack resistor into a timing network whose voltage biases the gain tubes'
// grids. Ratio and knee emerge from the diodes, the network and the gain tube's
// curve; nothing here is a ratio.
//
//   |sidechain| - delay --- Ra --- 12AL5 --+-- Cf || Rf --+-- Cs || Rs --- ground
//                                          control voltage = Vf + Vs
//
// A series pair (the classic program-dependent release): the diode current charges
// both capacitors. A short hit charges mostly the small Cf, which recovers with
// Rf Cf; sustained compression ends up mostly on Cs, which recovers with Rs Cs =
// Release. Release Mod m is the share of a short hit carried by Cf (Cf = C / m,
// Cs = C / (1 - m), C their series value); Rf Cf = Release / 10, at least 20 ms.
// At m = 0 it is one RC. Every value is inferred (no schematic is published):
// C = 1 uF, and Ra is set so Attack is the 63 % time of a 10 dB step over the
// threshold (bigger hits are caught faster, as a peak detector does).
// Threshold is the diodes' delay bias, Calibration the sidechain amplifier's gain.
// Both channels' diodes charge the one network (the stereo link).
class AnalogDetector
{
public:
    struct Settings
    {
        double thresholdDb = -18.0, attackMs = 5.0, releaseMs = 200.0, releaseMod = 0.5, sidechainGainDb = 0.0;
    };
    void prepare (double sampleRate) noexcept;
    void configure (const Settings&) noexcept;
    void reset (double controlVolts = 0.0) noexcept;
    // One sample: each channel's sidechain signal in full-scale units (filtered and
    // FB/FF-blended), with the sidechain amplifier's extra gain (Calibration).
    // Returns the control voltage (volts below the gain tubes' quiescent bias).
    double process (const double* sidechain, int channels, double calibrationGain) noexcept;
    double controlVolts() const noexcept { return fast + slow; }
    // Sidechain volts per full-scale unit at 0 dB sidechain gain: matched so the
    // 5670's static curve follows the Digital law's 4:1 near the default threshold
    // (owner decision; task detector-match, docs/Detector.md).
    static constexpr double sidechainVolts = 40.0;
    static constexpr double timingFarads = 1.0e-6;
    static constexpr double fastestReleaseMs = 20.0;
    // Ra C per millisecond of Attack (task detector-match).
    static constexpr double attackScale = 5.0;

private:
    double dt = 1.0 / 192000.0, fast = 0, slow = 0;
    double scale = sidechainVolts, delay = 0, attackOhms = 1;
    double fastCharge = 0, slowCharge = 0, fastLeak = 0, slowLeak = 0; // dt / C and dt / (R C) for each part.
    std::array<double, 2> diode {}; // Each channel's last diode voltage: the next solve's start.
};
