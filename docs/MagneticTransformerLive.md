# Shared magnetic transformer

Lumiere now uses the voltage-driven play/Preisach magnetic transformer in VST3,
AU, Standalone and the offline measurement view. The old memoryless tanh and
residual-ADAA audio stage is removed. The internal constitutive tanh remains,
as requested; it is evaluated with a prepared double-precision polynomial table.
The generic JUCE controls, collapsible A/B and measurement panel, meters,
compressor, sidechain choices, program-dependent release and FB/FF remain.

## Numerical work

The circuit equations remain `B=H+k*M`, `D*x=H+tau*dB/dt`, `y=x-H/D`.
The constitutive table holds local degree-five Taylor polynomials over 8,192
segments of normalized field magnitude. A two-million-point test checks value
error against scalar tanh to 2e-15 and monotonicity within rounding precision.
The table is built once during construction; every circuit holds its pointer.
No table initialization, allocation or guard lock is encountered while rendering.

The implicit solve starts with a prediction using the previous accepted branch
slope. It retains the monotone bracket, positive derivative and safeguarded
Newton/bisection fallback. The scaled equation-residual target remains 1e-13.
The cap is now 96 evaluations, so bisection has enough steps after rejected
Newton steps. Only the accepted trial commits history. The 108 extreme-setting
trajectories that exposed the old 48-evaluation cap now require zero failures;
output is compared numerically with the frozen v1 and the actual committed-state
equation is checked separately. They are no longer required to reproduce old
failure counts or old floating-point roundoff bit-for-bit.

The numerical table and predictor accelerate the same constitutive law and
circuit, not a new saturation curve. The model is still normalized and
uncalibrated. It is not a VTB1148 hardware fit; leakage inductance, capacitance,
eddy currents and physical material/geometry calibration remain future work.
Input voltage safety limits and the extension retaining linear H beyond the
canonical M guard remain explicit numerical choices.

## Live path, rate and latency

The stage sits after compression/makeup and before OUT gain. Each channel has
independent magnetic and emphasis state, with identical controls. Detector and
compression processing remain upstream. The original host parameter IDs,
positions, types, ranges, steps and defaults remain unchanged (16 parameters).
Amount maps -24 to +12 dB of drive and smooths over 20 ms. The private LF shelf
pair retains its controls and double precision; the magnetic model receives the
emphasized voltage and its output passes through the inverse shelf.

Internal oversampling uses 16x at 44.1/48 kHz, 8x at 96 kHz and 4x at 192 kHz,
keeping the internal rate at 705.6-768 kHz. This is fixed internal implementation,
with no new host oversampling parameter. The shipped stage earns fresh quality
checks at these rates; this policy is not inferred from the old prescribed-H
experiments. FIR settings are in `source/dsp/MagneticOversampling.h`.

Slightly longer down filters give integer group delays without a fractional
allpass: up/down orders are 198/198, 98/102, 98/110 and 98/126. Stopband target
is -120 dB. The host receives 144/144/137/124 samples of latency at the four
rates, plus a conservative two-second tail. The untouched input used by Mix,
Delta and global bypass receives the same exact integer delay. This preserves
`Delta = mixed - dry` and silent Delta at Mix 0%, neutral settings and bypass.
Latency never changes when controls are automated; it changes only with sample
rate preparation.

Amount 0 or Xfrm Out skips and resets the magnetic solve, and outputs exact
original samples shifted by that delay. The FIR history stays warm using
identity processing to avoid a cold-filter step when re-engaging. Magnetic
history resets at the precise inactive sample, independent of host blocks.
Out/Amount transitions crossfade over 10 ms. Voicing changes retain the fixed
bank crossfade over 20 ms. Audition level matching operates after the magnetic
stage and never modifies the detector. All render storage is fixed or prepared;
there is no rendering allocation, locking, logging or UI work.

## Saved settings and offline workflow

Voicing export now identifies version 2, `transformer-magnetic-play-v2`.
Old version-1 tanh JSON migrates its numeric A/B calibration, LF emphasis,
corner, selection and audition flags. It does not select a legacy audio engine
or promise the same tone. Existing APVTS host automation/state retains its
identity and restores normally. The old favorite JSON, historical reports,
recordings and binary fixture remain archived for provenance.

The existing measurement worker runs this same shared magnetic DSP, captures
A/B snapshots, supports cancellation/progress, and exports the new model name.
Its .25-second warmup and one-second Fourier window are retained. Measurements
and synthetic listening renders do not call UI code from the audio callback.
The first listening preset is the current magnetic default, rather than a
renamed execution of the old preferred tanh model.

## Validation and CPU

The shipped float stage, including default 12 dB / 120 Hz LF shelves and Amount
100%, passes all 108 waveform/rate/level cases. References evaluate analytic
voltage at 32x/64x and extend through 512x if needed, use scalar constitutive
values, independent emphasis states and independent FIR decimation. Reference
DC-20 kHz convergence must be at most -100 dBr and max error at most 1e-4;
applicable silent-tail differences must also reach -100 dBr. Candidate screens
retain -80 dBr band/off-grid/applicable tail thresholds and 0.1 dB peak-error
limits. FIR delay is removed explicitly; magnetic phase and real decay remain.
Full-band and absolute tail drift are exported as diagnostics.

Worst shipped-stage band residual is -103.73 dBr; worst selected reference
convergence is -105.19 dBr. Worst applicable off-grid power is -139.21 dBr and
worst tail mean difference -159.55 dBr. Maximum shipped-stage iterations is four,
with zero failures. These are default-voicing finite-grid results, not guarantees
for arbitrary material or every private tuning.

[Captured validation, CPU and listening results](measurements/magnetic_live_v2/README.md).
All 129 functional tests pass in Debug and Release (282,010 assertions in each).
The standalone extended probe covers 432 trajectories across legal host rates,
corner/memory/width extremes and combined drive/calibration from -36 to +24 dB,
including harder instantaneous drive reversals. It records zero failures; the
maximum is 61 iterations, below the new 96 cap. Its worst root diagnostic is
9.999981e-14; reassembling the committed equation in a different floating-point
order gives at most 1.046779e-13. The target is unchanged.

The measured full stereo processor uses 26.2% of one core at 48 kHz, 24.8% at
96 kHz and 25.6% at 192 kHz on the stated low-tone workload. Both Debug and
Release plugin targets and Tests build without warnings or errors.

Complete-plugin profiling includes the compressor, transformer, BA routing,
meters and stimulus generation. Its local medians are not deadline guarantees.
Use Release for music; Debug retains debugging overhead.

## Reproduce

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
ctest --test-dir Builds -C Debug --verbose --output-on-failure -E '^Benchmarks'
cmake --build Builds --config Release --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
ctest --test-dir Builds -C Release --verbose --output-on-failure -E '^Benchmarks'
LUMIERE_MAGNETIC_LIVE_REPORT_DIR=/private/tmp/lumiere-magnetic \
./Builds/Release/Tests '[magnetic-live]' --reporter compact
LUMIERE_TRANSFORMER_AUDIO_REPORT_DIR=/private/tmp/lumiere-magnetic \
LUMIERE_TRANSFORMER_AUDIO_SKIP_TIMING=1 \
./Builds/Release/Tests '[transformer-audio-report]' --reporter compact
LUMIERE_MAGNETIC_CPU_REPORT_DIR=/private/tmp/lumiere-magnetic \
./Builds/Release/Tests '[magnetic-live-cpu]' --reporter compact
python3 tools/render_magnetic_live_report.py /private/tmp/lumiere-magnetic
```

The CTest exclusion removes only the generated unbuilt-Benchmarks placeholder;
Benchmarks is not built. Do not overlap CPU export with builds or other tests.
The listening/raw-core export can skip its legacy timing section when run during
accuracy checks. It supplies supplementary core comparisons at drive 0 dB and
live-stage renders at +12 dB drive/default shelves; those settings differ and do
not isolate hysteresis alone. All sources are synthetic, not hardware recordings.

The extended solver probe is reproducible independently of JUCE/UI:

```bash
clang++ -std=c++23 -O3 -flto -Wall -Wextra -Wconversion -Isource \
  tools/check_magnetic_solver.cpp source/experimental/TransformerAudioCircuit.cpp \
  source/experimental/ConstitutiveLookup.cpp source/experimental/PlayPreisachCore.cpp \
  -o /private/tmp/lumiere-check-magnetic-solver
/private/tmp/lumiere-check-magnetic-solver /private/tmp/lumiere-magnetic/solver_extremes.csv
```
