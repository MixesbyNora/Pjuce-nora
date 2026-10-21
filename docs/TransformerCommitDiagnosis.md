# Transformer commitment: historical Phase 0 diagnosis

The numerical findings below describe the **former shelf-equipped engine**.
The LF shelf pair has since been removed, intentionally changing the enabled
transformer sound. Its measurements are retained for comparison, not asserted to
be the current transfer. Current before/after results and the shelf-free workflow
are documented in [MagneticTransformerLive.md](MagneticTransformerLive.md).
Magnetic-lever controls and the series commit stage remain unimplemented.

## Numerical findings

At Amount 100%, default private voicing (calibration 0 dB, LF shelf +12 dB /
120 Hz / Q 0.5), the actual live stage gives the following left-channel results.
Bass level is its peak before IN; the 2 kHz probe is -60 dBFS before IN. Gain is
relative to that probe **after** IN, so it does not hide the applied input gain.
HF survival is the 2 kHz component with bass divided by the component without bass.

| Bass peak dBFS | IN dB | Core-input peak before guard | Guarded samples % | 2 kHz gain dB | HF survival | Guard-lifted 2 kHz gain dB |
|---:|---:|---:|---:|---:|---:|---:|
| -12 | 0 | 0.7518 | 0.000 | -0.000368 | 0.999965 | -0.000368 |
| -12 | 6 | 1.5000 | 0.000 | -0.000381 | 0.999963 | -0.000381 |
| -12 | 12 | 2.9929 | 0.000 | -0.000377 | 0.999964 | -0.000377 |
| -12 | 18 | 5.9717 | 0.000 | -0.000379 | 0.999963 | -0.000379 |
| -12 | 24 | 11.9151 | 0.000 | -0.000384 | 0.999963 | -0.000384 |
| -6 | 0 | 1.4990 | 0.000 | -0.000386 | 0.999963 | -0.000386 |
| -6 | 6 | 2.9910 | 0.000 | -0.000375 | 0.999964 | -0.000375 |
| -6 | 12 | 5.9677 | 0.000 | -0.000380 | 0.999963 | -0.000380 |
| -6 | 18 | 11.9072 | 0.000 | -0.000382 | 0.999963 | -0.000382 |
| -6 | 24 | 23.7580 | 52.784 | -6.543842 | 0.470772 | -0.000384 |
| 0 | 0 | 2.9900 | 0.000 | -0.000386 | 0.999963 | -0.000386 |
| 0 | 6 | 5.9657 | 0.000 | -0.000380 | 0.999963 | -0.000380 |
| 0 | 12 | 11.9032 | 0.000 | -0.000383 | 0.999963 | -0.000383 |
| 0 | 18 | 23.7500 | 52.784 | -6.543825 | 0.470773 | -0.000389 |
| 0 | 24 | 47.3875 | 77.890 | -13.177309 | 0.219350 | -0.000393 |

At +24 dB IN, both channels' existing diagnostics report:

| Bass peak dBFS | L limitedInputs | R limitedInputs | L / R guarded % | L / R invalidInputs |
|---:|---:|---:|---:|---:|
| -12 | 0 | 0 | 0.000 / 0.000 | 0 / 0 |
| -6 | 203230 | 204254 | 52.784 / 53.050 | 0 / 0 |
| 0 | 299895 | 300804 | 77.890 / 78.126 | 0 / 0 |

Each counter is a post-warmup delta over **385,024 oversampled input samples**
(24,064 native frames, 0.501333 s). Right bass phase is 90 degrees ahead of left;
this counter window contains a fraction of a bass period, explaining the small
L/R count difference. Spectral measurements use a separate exact half-second
window. The complete CSV reports both channels for all cases.

The 60 Hz pre-emphasis gain is about 2.989x (+9.51 dB), rather than the shelf's
asymptotic +12 dB. At +24 dB IN, a full-scale bass peak therefore reaches about
47.39 at the circuit input, almost three times its ±16 guard. The guard is at
`TransformerAudioCircuit::processSample` before the solve, after emphasis. It
replaces the voltage used by both the forcing term and the `voltage - H/D` output;
it is ordinary hard clipping, not an emergent magnetic characteristic. Amount's
drive and private calibration act **after** this voltage guard and cannot prevent
it. At these steady levels the bass-only guard onset is approximately +14.6 dB
IN for a 0 dBFS bass peak and +20.6 dB IN for a -6 dBFS bass peak; the quiet probe
slightly perturbs the exact crossing.

For a sine of amplitude A through a hard limiter at C, the small-probe carrier
survival is approximately the unclipped fraction, `2/pi * asin(C/A)` when A>C.
Using the measured emphasized peak as an approximate A predicts 0.4704 and
0.2193 for the -6 and 0 dBFS bass cases at +24 dB IN. The measured ratios are
0.4708 and 0.21935. This is an additional numerical check of ordinary limiter
gating, not a magnetic or hardware characterization.

Across all 45 stereo settings (90 channel rows), invalidInputs and solverFailures
are zero. The guard-lifted experiment has zero limitedInputs. Where the shipped
guard never engages, the measured probe response agrees with the lifted guard
in this experiment. The replay of the original guard is bit-identical to
the shipped float stage over the complete stream (maximum sample difference 0).
At every bass/IN pair, guard counts are identical across the three drive settings.

## Why the model can still feel parallel

The circuit uses `y = x - H/D`, `B = H + k*M`,
`D*x = H + tau*dB/dt`. With M locally saturated, its changing contribution largely
disappears and the remaining branch is approximately a linear high-pass. With
default k=4, memory=0.35 and saturation field=1, its corner tends toward
`5 * (1 + 4 * (1 - 0.35)) = 18 Hz`. A quiet 2 kHz carrier consequently survives.
At flux transitions, M changes and adds harmonics to the magnetizing correction;
that is not a full-band series waveshaper.

Without guard clipping, measured HF survival across this grid is within about
0.00034 dB of unity, including the guard-lifted extreme-IN cases. At default
voicing, 0 dBFS bass and +24 dB IN, the shipped HF survival is 0.21935 (-13.17725 dB
relative to probe-only), versus 0.999962 (-0.000332 dB) with the guard lifted.
This numerical A/B identifies clipping as the dominant **measured HF attenuation**
at that setting. It does not prove which spectral event a listener describes as
“parallel,” nor does it turn the unguarded circuit into a series saturator.

## Method and limits

- 48 kHz stereo, current 16x oversampling and unchanged 144-sample FIR latency.
  Amount 100%, transformer In, audition level match off. Default core parameters
  and default LF voicing; calibration alone is swept -12/0/+12 dB, giving total
  internal drive 0/+12/+24 dB. IN is swept 0/6/12/18/24 dB; bass peak -12/-6/0 dBFS.
- The real `TransformerSaturation` class is exercised directly. IN is an explicit
  settled gain before it; compression, OUT, LFM, Mix and monitor are deliberately
  excluded to isolate the transformer. This is not a compressed DAW-program test;
  in a project, compression/makeup can change the voltage reaching this stage.
- Input is float, as in the plugin. L/R magnetic states are independent. R has a
  90-degree bass phase offset; the probe phase is shared. No output limiting,
  RMS matching, normalization or clipping is applied by the tool.
- Warmup: 48,128 native frames (1.002667 s). Capture: 24,000 frames (0.5 s), shifted
  by the reported FIR delay, giving coherent 60 Hz and 2 kHz Fourier components.
  The measured probe is half the difference between renders with opposite probe
  polarities. This rejects bass-only content and retains the odd response to the
  finite -60 dBFS probe. HF survival concerns the **2 kHz carrier**, not the total
  energy in all HF intermodulation sidebands. It is not a general audio-quality score.
- The offline-only counterfactual uses a power-of-two scale S=16: feed v/S, set
  D to D*S, multiply core output by S. Then D*v and the H/M equations stay the same,
  while the effective voltage guard becomes ±256. Production guards, ±32 magnetic
  field extension, coefficients, sample rate, constitutive function, history and
  1e-13 solver tolerance are untouched. The same pre/post shelves and FIR are used.
  This is a diagnostic identity, **not a proposed production fix** or a safety
  validation for removing/raising the guard on arbitrary audio.

These are numerical checks on normalized equations. “More committed” bass,
perceived clean leakage, and the sound of a future series stage are subjective
listening hypotheses. No result is claimed as hardware-accurate or a VTB1148 fit.
No Phase 1 packed-state/private-control changes, or Phase 2 commit stage/aliasing
or CPU comparison, are included in this Phase 0 stop.

## Historical data and current verification

[The retained Phase 0 measurements](measurements/transformer_commit_phase0/diagnosis.csv)
were captured before shelf removal. Their provenance remains in the associated
machine-readable manifest. The current diagnostic test now exercises the
shelf-free path, so running it does not recreate this historical shelf-equipped
result. Fresh baseline captures were retained before editing, alongside the
[before/after shelf-removal measurements](measurements/magnetic_shelf_removal/before/diagnosis.csv).

`tests/TransformerCommitDiagnosisTests.cpp` keeps the input levels, drive sweep,
window and counterfactual method, but its replay no longer contains shelves.
Its endpoint assertions now require no limiting for the tested normalized bass
at +24 dB IN. The guard itself and its independent safety tests are unchanged.
The frozen `tests/helpers/TransformerAudioReference*` files and solver tolerance
remain untouched. Full suite, latency, exact delayed bypass and numerical
validation are rerun; detailed outcomes belong in machine-readable provenance.
Benchmarks is neither built nor run.
