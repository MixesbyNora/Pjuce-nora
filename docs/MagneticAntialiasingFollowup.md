# Offline magnetic antialiasing follow-up

This experiment Release-profiles the existing filtered oversampling comparison
and tests two alternatives. Everything is confined to Tests. The live compressor,
tanh transformer, saved favorite voicing, generic editor, meters, parameters and
host latency are unchanged. No production oversampling mode is selected.

The next [multitone/transient validation](MagneticMultitoneTransientValidation.md)
tests these unchanged candidates beyond steady single tones.

## Baseline and comparison

The baseline is the original point-sampled `preisach-play-core-v1` with the same
1x/2x/4x/8x/16x double-precision FIR chains. This follow-up retains all 48 stimuli,
coherent windows, filter warmup, analytic 32x/64x references with adaptive 128x
checks, and both full-Nyquist and 20 Hz–20 kHz metrics described in
[MagneticOversamplingComparison.md](MagneticOversamplingComparison.md).

16x point sampling remains the practical comparison reference. Every candidate
is also measured against the higher-rate analytic reference; matching 16x alone
is not sufficient. The -80 dBr line is a provisional numerical screen, not an
audibility threshold. Nonharmonic bins also contain history discretization
sidebands; harmonic overlap and float FFT precision limit this metric.

## Candidate 1: interval integration

`tests/helpers/MagneticIntervalCore.*` computes the mean magnetization over a
linearly reconstructed field segment H0 to H1. For each play operator, the state
first holds, then follows H plus/minus its radius after the activation crossing.
Its contribution is the duration-weighted sum of the held density and the
moving density's mean. The latter uses the log(cosh) primitive of tanh. Stable
log(cosh), a midpoint fallback for tiny differences, and fixed-size state/caches
avoid numerical singularities and per-sample allocation.

The endpoint play states and magnetization match the original core; tests check
reversals and parameter extremes and compare interval outputs with independent
32,768-step midpoint quadrature. This is a custom integration of this specific
rate-independent constitutive law. It is not a general stateful ADAA solution.

A linear nonlinearity would produce (H[n-1] + H[n])/2. Therefore the candidate
adds half an internal sample of delay and a cos(pi*f/internalRate) amplitude
response. Delay is included in the reported latency and removed in spectral
phase alignment. **No gain compensation or response correction is applied.**
Identity measurements include the averaging response, and gain/residual metrics
retain its effects. Averaging does not restore extrema missed by input sampling.

## Candidate 2: quadratic turning-point reconstruction

`tests/helpers/MagneticTurningPointCore.h` uses the previous two internal field
samples and the current one to detect a strict local maximum or minimum. For
samples a, b, c, the vertex offset and field estimate are:

```
curvature = (a - b) + (c - b)
u = clamp(0.5 * (a - c) / curvature, -0.5, 0.5)
Hvertex = b - 0.25 * (a - c) * u
```

At a reversal it advances the original core to that estimate, then to c. This
refines history without smoothing the output: output remains the original
constitutive law evaluated at c. Only constant-size history, arithmetic and one
additional core update per detected reversal are used. A tiny-curvature guard
avoids division on subnormal differences; the original field clamp is retained.
After reset, it waits for three actual input samples before fitting an extremum;
the reset sentinel must not create a spurious reversal or permanent remanence.
No new filter, parameter, sample lookahead or fixed output delay is added.

This is a local reconstruction approximation, not exact band-limited extrema
recovery. It deliberately changes sampled magnetic history and can overestimate
extrema on irregular material. Single-tone results do not validate it for music,
multitone material, a voltage/flux circuit or hardware. It must remain offline.

## Cost and provenance

Actual Xcode compile/link commands use `-O3` and `-flto`; build settings have
not been changed. These flags were checked in the generated response files and
build logs rather than inferred from the CMake utility's `-Ofast` declaration. All kernels use double state/math and the same FIR design.
Each grid export retains its initial median of five 32,768-base-sample mono
renders, including FIRs, core processing, copies and a checksum, excluding
preparation/FFT. The final cost comparison uses a separate matched-binary export:
three sweeps, each a median of five renders, with factor order alternated and
method order rotated. It tests a slow H-peak-4 sine, a fast H-peak-0.05 sine,
and seeded H-peak-4 noise. All are repeated 256-sample sequences; sine frequencies
are 1/256 and 53/256 cycles per base sample (187.5 and 9937.5 Hz at 48 kHz).
Tables/plots use the median across sweeps. Stereo host scheduling, full-plugin
overhead and arbitrary input workloads are not covered. Local timings are not
realtime deadline guarantees.

The original Debug report is preserved. New Release baseline and candidate CSVs,
plots, interpretation and source/data hashes live in
[magnetic_antialiasing_v1](measurements/magnetic_antialiasing_v1/README.md).

## Reproduce

Run these report commands sequentially so timing runs do not compete:

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Release --target Tests
LUMIERE_MAGNETIC_OS_REPORT_DIR=/private/tmp/lumiere-point ./Builds/Release/Tests '[magnetic-os-report]' --reporter compact
LUMIERE_MAGNETIC_INTERVAL_REPORT_DIR=/private/tmp/lumiere-interval ./Builds/Release/Tests '[magnetic-os-report]' --reporter compact
LUMIERE_MAGNETIC_TURNING_REPORT_DIR=/private/tmp/lumiere-turning ./Builds/Release/Tests '[magnetic-os-report]' --reporter compact
./Builds/Release/Tests --reporter compact
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
./Builds/Debug/Tests --reporter compact
LUMIERE_MAGNETIC_COST_REPORT_DIR=/private/tmp/lumiere-profile ./Builds/Release/Tests '[magnetic-cost-report]' --reporter compact
```

Set only one report-directory environment variable at a time. Without these
variables the long grid/cost export is skipped, but regular tests exercise all
kernels. No Benchmarks target is used. Copy the three exports into point/,
interval/, turning/ under a report directory, and profiling.csv into its root, then run
`tools/render_magnetic_antialiasing_report.py <report-directory>` with ReportLab
available. It creates README, SVG and PDF; on macOS sips can rasterize the PDF.

## Related primary literature

- [Holters, Antiderivative Antialiasing for Stateful Systems, DAFx 2019](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_4.pdf)
  discusses extension to a class of stateful systems and averaging's response/delay.
- [La Pastina and D'Angelo, Antiderivative Antialiasing with Frequency Compensation for Stateful Systems, DAFx 2022](https://dafx.de/paper-archive/2022/papers/DAFx20in22_paper_4.pdf)
  examines compensation and stability conditions. No compensation filter or claim
  of general applicability from that work is introduced here.
