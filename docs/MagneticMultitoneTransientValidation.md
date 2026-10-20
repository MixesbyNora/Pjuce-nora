# Offline multitone and transient validation

The subsequent [artifact refinement](MagneticArtifactRefinement.md) investigates
the close-tone and impulse misses using separate candidates. The original
validation data and kernels described here remain preserved.

This validates the existing 8x quadratic turning-point candidate against original
16x point sampling and an independent high-rate reference. It adds only Tests
helpers, tests, measurements and documentation. No live plugin processing,
parameters, voicing, editor, meters or latency are changed. The original
[antialiasing comparison](MagneticAntialiasingFollowup.md) remains intact.

## Stimuli

The grid contains 108 stimuli: nine signals x four base rates
(44.1/48/96/192 kHz) x three normalized field scales (0.05/1/4).
These are dimensionless H-to-M experiments, not dBFS or hardware voltage levels.
The 16,384-sample analysis window has a different duration at each base rate;
transient widths are specified in seconds.

Multitone signals:

- Low/high pair near 60 Hz and 7 kHz, 4:1 amplitude ratio.
- Equal-amplitude pair near 19 and 20 kHz.
- Six tones near 80/300/1000/3000/7000/15000 Hz, equal weights and fixed phases.

Tones snap to integer FFT bins divisible by three, with the highest input tone
capped at 20 kHz. Actual frequencies and sampled peak fields are exported in
stimuli.csv. Amplitude weights sum to one; the scale is a bound, not a claim
that all tones reach their maxima simultaneously. Coherent intermodulation
products lie on the same three-bin lattice. Off-lattice power over 20 Hz–20 kHz
captures some folded energy/history sidebands while excluding genuine lattice
products. Aliases overlapping genuine products remain invisible to this mask.
The total complex residual and selected product-line errors cover additional
differences. A new spectral line is not automatically classified as aliasing.

Transient signals:

- An 18 kHz band-limited impulse, a zero-DC finite Dirichlet sum with a fractional
  base-sample peak. Its periodic sidelobes are part of the stimulus; it has no
  declared silent tail.
- Gaussian-windowed 730 Hz / 10 kHz bursts, envelope sigma 1.5 / 0.35 ms.
- A unipolar Gaussian pulse, sigma 0.15 ms.
- Opposite-polarity Gaussian pulses separated by 4 ms, sigma 0.15 ms.
- A fast attack/decay pulse: an exponential with tau 1.5 ms convolved with a
  Gaussian of sigma 0.08 ms, with a preparatory peak normalization.

Smooth envelopes are truncated only at negligible amplitudes (nine Gaussian
sigmas / 32 exponential time constants). The final 1024 samples are exactly
zero input for the five declared silent-tail signals. Their output may retain
physical magnetic remanence. Tail error compares that output with the reference;
it does not subtract or require zero magnetization.

## Reference and timing alignment

Candidate input is sampled at the base rate and passes through the existing
interpolation/core/decimation harness. The 8x candidate and original 16x kernels
are unchanged. Reference input is evaluated analytically at 32x then 64x,
passed through the original core, and decimated with real double-precision
JUCE FIR stages using the existing transition/stopband settings. Reference
interpolation does not reuse the candidate's sampled input. This is a new
filtered reference, not the ideal FFT truncation used by the earlier tone grid.

If convergence is insufficient, the reference extends through 128x/256x/512x/1024x.
Convergence requires all of: DC-through-20-kHz residual <= -100 dBr, maximum
audio-band sample error <= 0.0001 of audio-band reference peak, and (when applicable) silent-tail mean
error <= -100 dBr relative to reference peak. The final comparison and every
extension are exported. Cases unresolved at 1024x are inconclusive. Full-band maximum disagreement
is exported separately and does not certify full-Nyquist convergence.

The reference decimator's delay is measured from its impulse centroid. Candidate
delay uses the existing FIR-reported value. Offline 257-tap Kaiser-windowed sinc
interpolation removes fractional delays. Its complex response is tested through
20 kHz at all four sample rates and multiple fractional positions, with a
-100 dB complex-error goal. Independent identity runs measure reference-vs-analytic
input and candidate-vs-reference reconstruction errors. No gain normalization,
DC removal, fitted delay or fitted gain is applied to output comparisons.

All pipelines first settle their filters for one identity window, then reset
only the core. Multitones warm history for another full window; transients have
1024 samples of preroll. An additional 512 samples flush delayed output past
the analysis window. These guards are outside the reported measurements.

## Metrics and provisional screens

- Band residual: RMS error from DC through 20 kHz / reference band RMS. Including
  DC exposes persistent magnetic-state discrepancies. Difference samples are
  formed in double before a float FFT. This includes input reconstruction, filter
  response, aliasing, harmonic and history errors; it is not pure alias power.
- Full residual and maximum error: unbandlimited time-domain diagnostics, with
  maximum error normalized to the full-band reference peak. These include
  ultrasonic differences. Full-band reference agreement is recorded separately.
- Off-grid power: multitone-only, 20 Hz–20 kHz, relative to reference band RMS.
- Primary peak and maximum-error measurements: both traces are restricted to
  DC through 20 kHz by an identical FFT mask/inverse transform. The peak error
  is 20 log10(output band peak / reference band peak); maximum error uses the
  band-limited difference. Unbandlimited peak/error values are also exported.
  These finite-window FFT measurements can ring at boundaries, so they compare
  identically treated windows and do not describe an instantaneous analog peak.
- Tail mean error: absolute mean difference over the last 1024 samples / reference
  peak; tail drift: largest deviation from that output mean / reference peak.
- Selected two-tone product levels and complex errors use one-sided RMS values
  relative to reference full-window RMS. Near-zero products are not evaluated by
  unstable output/product ratios.

Provisional comparison screens are band residual <= -80 dBr, applicable off-grid
power <= -80 dBr, absolute audio-band peak error <= 0.1 dB, tail mean error <= -80 dBr, and
late drift <= -100 dBr. Reference convergence is required. Identity reconstruction
must be <= -95 dBr for both candidate and reference, leaving a 15 dB margin to
the residual screen; the actual calibration values are retained. These are
engineering comparisons, not audibility standards or hardware-realism claims.
A screen miss is reported as a measurement outcome; the candidate is unchanged.
The first development pass mixed a full-band peak-convergence requirement with
an audio-band residual screen. Near-Nyquist FIR transition aliases could then
fail convergence despite very close DC–20 kHz agreement. The final method
matches primary peak/convergence measurements to that band while preserving
full-band disagreement explicitly; it makes no full-Nyquist convergence claim
when the separate full-band check fails.

## Tests, artifacts and reproduction

Tests cover stimulus bounds/coherence, alignment and independent identity
calibration, genuine IMD versus off-grid metrics, and deliberate persistent-state
and late-oscillation faults. The long grid checks every output for finite/bounded
samples and records outcome data rather than requiring every candidate to pass
the provisional quality screen. No Benchmarks target is built.

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Release --target Tests
LUMIERE_MAGNETIC_VALIDATION_REPORT_DIR=/private/tmp/lumiere-validation ./Builds/Release/Tests --reporter compact
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
./Builds/Debug/Tests --reporter compact
```

Without the environment variable, the long export is skipped. Checked-in data,
plots, verdicts and provenance are in
[magnetic_validation_v1](measurements/magnetic_validation_v1/README.md).
Worst-case plots select converged cases by the candidate's largest band residual,
not by visual appearance. These tests do not qualify the model for live use or
replace later voltage/flux/circuit coupling and hardware calibration.

With ReportLab available, regenerate README/SVG/PDF from an export using
`python3 tools/render_magnetic_validation_report.py <export-directory>`.
On macOS, `sips -s format png <plot>.pdf --out <plot>.png` rasterizes each plot
(`validation`, `transient`, `spectrum`). No project dependency was added.
