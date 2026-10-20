# Offline filtered oversampling comparison

This follow-up compares the unchanged `preisach-play-core-v1` at 1x, 2x, 4x,
8x and 16x. It is confined to the Tests target. The live plugin, its tanh
transformer, favorite voicing, UI, host parameters and latency are unchanged.

## Candidate pipeline

Each candidate uses double-precision JUCE half-band equiripple FIR stages for
interpolation and decimation. Every play state advances at the internal rate.
Filters and buffers are prepared before processing; the processing helper uses
preallocated storage. There is no new plugin oversampling control.

Both directions use a -120 dB stopband **design target**, with normalized
transition width 0.04 for the first stage and 0.08 for subsequent stages. These
comparison settings are in `tests/helpers/MagneticOversampling.h`. No JUCE or
module sources were modified. This JUCE coefficient generator rounds its initial
FIR coefficients through float even when processing/state are double. Coefficient design/precision and the full chain
are evaluated through actual processing; a requested stopband is not treated
as proof of measured performance.

The filters are linear phase. Their reported fractional base-rate latency is
removed from complex tone FFT bins for comparison. No fractional-delay filter
is inserted. An independent identity/impulse measurement checks passband gain
and phase after delay removal, including 20 kHz at 44.1/48/96/192 kHz.

## Stimuli, warmup and references

The grid retains four sample rates (44.1/48/96/192 kHz), four nominal tone regions
(50/1000/7000/10000 Hz), and three normalized field peaks (0.05/1/4), with all
five candidate factors. That produces 48 stimuli and 240 candidate rows.
These are H-to-M tests, not voltage-to-audio measurements or dBFS drive levels.

A 16,384-sample coherent window uses the nearest odd carrier bin; actual
frequencies are recorded. Warmup has two parts: one window of identity processing
settles the filters, then the core alone is reset and another window warms its
magnetic history. The third window is analyzed. This avoids cold interpolation
filter transients leaving a different remanent state in otherwise inactive
play operators.

For every stimulus, separate 32x and 64x evaluations of the analytical sine drive
the same core directly. Their spectra are restricted to bins below base Nyquist.
The 32x/64x difference is exported as a convergence check. If it exceeds -100 dBr,
the harness additionally computes a 128x reference and exports the 64x/128x
difference and the final reference factor. This target gives a 20 dB margin to
the provisional -80 dBr comparison goal. These are approximate
offline references using the existing float FFT, not an exact continuous-time
solution. Very low error readings approach FFT precision. The reference uses
analytical input samples and ideal spectral band limiting; candidates use real
interpolation/decimation filters.

## Exported metrics

- `alias_dbr`: candidate power outside DC and the carrier's genuine in-band
  integer-multiple bins, normalized to the final reference's in-band RMS power. Odd
  carrier bins avoid low-order folded-harmonic collisions with genuine harmonics.
- `audio_band_alias_dbr`: the same nonharmonic-power metric restricted to
  20 Hz–20 kHz. `peak_alias_hz` locates the strongest full-band component. Both
  bands are retained: at higher base rates, half-band filter transition regions
  can permit ultrasonic aliases near Nyquist that do not fall in the 20 kHz band.
- `aligned_residual_dbr`: full-band complex difference after latency alignment,
  including DC and candidate Nyquist. This includes filter response, aliasing,
  and sampled-extremum/history differences; it is not a pure alias metric.
- `passband_residual_dbr`: the same difference restricted to DC through
  min(20 kHz, 0.4 * base rate), with the same reference power normalization.
  This reduces the influence of intentional rolloff near base Nyquist.
- `gain_error_db` and `phase_error_deg`: candidate fundamental relative to the
  final reference, after delay removal. They show changes to the wanted signal.
- `reference_32v64_dbr`: reference convergence error relative to 64x in-band RMS.
- `reference_last_dbr` and `reference_factor`: the final convergence check and
  reference factor actually used, including adaptive 128x cases.
- `filters.csv`: identity round-trip gain, delay-removed phase, and latency.
- `cost.csv`: local Debug median of five 32,768-base-sample mono renders,
  including filters, core, input copy and checksum; excludes preparation/FFT.
  These are comparative local timings, not production deadline guarantees.

The optional -80 dBr comparison line screens the 20 Hz–20 kHz nonharmonic
power in this proxy experiment; it is not a universal audibility threshold. A clean
nonharmonic-bin result also does not guarantee perfect harmonic amplitudes or
complete removal of aliases that coincide with genuine harmonics. Nonharmonic
artifacts also include sidebands from sampled turning-point history, so this
metric does not isolate spectral folding from every discretization effect.

## Reproduce

Configure/build the usual three plugin targets and Tests; do not build Benchmarks.

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
LUMIERE_MAGNETIC_OS_REPORT_DIR=/private/tmp/lumiere-os-v1 ./Builds/Debug/Tests --reporter compact
```

Without the environment variable, the full grid/cost export is skipped; normal
tests still exercise filters, partition/reset/channel behavior, numerical
stability, and a spectral/convergence case. The checked-in
[results](measurements/magnetic_oversampling_v1/README.md) include all CSVs,
configuration/provenance metadata and a plot. The separate offline ReportLab
tool `tools/render_magnetic_oversampling_report.py <report-directory>` regenerates
the SVG from the captured CSVs.

Future production choices require Release profiling, additional input material,
and voltage/flux/circuit coupling. No candidate is automatically enabled in the
plugin by this experiment.
