# Offline magnetic artifact refinement

This follows the [multitone/transient validation](MagneticMultitoneTransientValidation.md).
It targets close 19/20 kHz tone artifacts and the persistent magnetic-state
offset following the band-limited impulse. All changes are confined to Tests,
offline reporting and documentation. The live tanh transformer, favorite voicing,
compressor, controls, editor, meters and plugin latency remain unchanged.

## Experiment

The original point core and the existing three-sample quadratic turning-point
core remain intact. A separate `QuarticCore` reconstructs each internal field
interval from five actual field samples. It computes the quartic derivative at
both interval ends; when their signs differ, 20 bounded bisection steps locate
the stationary point. The core visits that estimated field extremum before
processing the interval endpoint. This refines the play states' history without
changing their radii, weights, tanh mapping or model parameters. No DC removal
or output correction conceals retained-state error.

Two internal samples of lookahead supply the right-hand data. Their delay is
included in both identity calibration and output alignment: 0.25 base samples
at 8x, 0.125 at 16x and 0.0625 at 32x, in addition to the existing FIR delay.
This is offline candidate latency; live plugin latency is unchanged. Startup
waits for real samples, avoiding the fabricated reset extrema found during the
earlier experiment. Fixed arrays and bounded loops require no sample-loop
allocation, locks or logging.

The quartic is local polynomial reconstruction, not exact bandlimited
reconstruction. It assumes at most one stationary point per internal interval.
The current stimuli/rates test that numerical candidate; arbitrary inputs and
different magnetic parameters require further validation.

Six comparisons separate reconstruction order from oversampling:

- Original point sampling at 16x.
- Existing quadratic turning points at 8x and 16x.
- New quartic turning points at 8x, 16x and 32x.

The original factors array remains 1/2/4/8/16 for historical tone reports. The
Tests-only processor additionally accepts 32x for this comparison. Validation's
old `render` defaults remain unchanged; an explicit candidate entry point selects
the additional methods.

## Validation and reporting

The final grid passes 108/108 screens with quartic 16x and quartic 32x, versus
96/108 for the previous quadratic 8x candidate and 94/108 for original point
16x. Quartic 8x improves to 104/108: all impulse cases pass, but four close-tone
cases remain. Quadratic 16x passes 107/108, missing one impulse case. The 16x
quartic's worst band residual is -89.89 dBr, against the -80 dBr screen.

At 48 kHz and H scale 1, the formerly worst impulse improves from -64.38 dBr
(quadratic 8x) to -99.53 dBr (quartic 16x). The raw late mean state error falls
from -8.31e-6 M to +4.80e-8 M. This impulse has periodic sidelobes, not a silent
tail; nonzero reference magnetization remains legitimate. Across the close-tone
family, the worst residual improves from -69.84 dBr to -89.89 dBr. No model
voicing was changed to obtain this.

Local Release profiling puts quartic 16x between 0.5% and 21.2% above original
point 16x across the three timing signals. Quartic 32x costs 2.03-2.12 times
original 16x. Keep quartic 16x as the next offline candidate; the extra 32x
margin costs substantially more and some residuals approach reference
convergence uncertainty. No candidate is integrated into live processing.

All 115 tests pass: 280,584 assertions in Debug and 281,382 in Release with
the full export. All three Debug plugin targets and Tests build without
warnings or errors; Release Tests also builds cleanly. Hashes of all 44 source
files are unchanged, and the rebuilt plugin binaries contain no magnetic
research symbols. Existing audio/parameter/meter tests continue to pass.

The focused pass uses all 24 combinations of the two target families, four
sample rates and three field scales. The final pass repeats the full 108-stimulus
grid across all six methods. References are independently evaluated analytic H
through the unchanged original core and real FIR decimation, adaptively extended
from 32x through 1024x using the prior convergence checks. Identical inputs,
warmup, alignment, thresholds and full-band diagnostics preserve comparability.

Primary residuals include DC through 20 kHz, so magnetic-state discrepancies
remain visible. Full-band reference agreement remains a separate diagnostic;
DC-20 kHz convergence does not imply full-Nyquist convergence. Off-grid power
excludes genuine coherent IMD but cannot detect aliases overlapping its lattice.
These are engineering reference-error screens, not hardware-fit or audibility
claims. This is still a constitutive H-to-M model, without a transformer voltage,
flux, source/load or measured hardware calibration.

The report retains all comparisons, Release timings, raw target traces, complex
spectral errors, plots and source/data provenance:
[magnetic_refinement_v1](measurements/magnetic_refinement_v1/README.md).
It also records the final results and remaining limitations.

Tests cover exact quartic extrema, startup history, invalid-input handling,
sign symmetry, field bounds, reset, stereo isolation, single-sample versus block
processing, independently calibrated lookahead, and the formerly worst close-tone
and impulse regressions. The exporter checks every output for finite/bounded
values and requires converged references, while reporting screen misses honestly
for each method.

## Reproduction

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
./Builds/Debug/Tests --reporter compact
cmake --build Builds --config Release --target Tests
LUMIERE_MAGNETIC_REFINEMENT_REPORT_DIR=/private/tmp/lumiere-refinement \
LUMIERE_MAGNETIC_REFINEMENT_FULL_GRID=1 \
./Builds/Release/Tests --reporter compact
```

Omit `LUMIERE_MAGNETIC_REFINEMENT_FULL_GRID` for the focused 24-stimulus export.
Without an export directory, the long research export is skipped. Release
timings are sequential medians of five runs on this machine, with actual -O3
and LTO flags; they include FIR/core/copies and exclude prepare/FFT. They are
local measurements, not a real-time performance guarantee. Do not run builds
or other test jobs concurrently with the profiling portion.

With ReportLab and NumPy available:
`python3 tools/render_magnetic_refinement_report.py <export-directory>`.
On macOS rasterize `refinement.pdf`, `impulse.pdf` and `spectrum.pdf` with
`sips -s format png <file>.pdf --out <file>.png`. No dependency was added to
the plugin or Tests target. Benchmarks is not built.
