# Shunt transformer improvement — Step 1 diagnosis

## Scope and stop gate

**DECISION:** execute Step 1 of [the owner brief](CodexBrief-ShuntImprovement.md),
then stop. Only shunt variants A and B are used in new exports. Series is dropped
from this research; historical spike results remain unchanged. No constitutive
curve, circuit, solver stopping rule, filter, host state, UI or shipped DSP was
changed. Steps 2–5 are not implemented. CPU and latency budgets remain **OPEN**.

**MEASUREMENT:** the earlier shunt off-grid failures are dominated by insufficient
conditioning, not evidence of a large steady-state aliasing problem. The
synthetic bass input also already contains high-frequency energy. Variant B has
a much smaller factor-dependent non-harmonic component, reported separately
below. This does not identify the cause of the owner's listening impression.

**STOP-GATE ANSWER:** no factor is certified against the literal requirement
“non-harmonic energy below the harmonic floor in every band.” Some bands contain
no upper harmonics, or harmonics below numerical resolution, so that comparison
has no resolved harmonic floor. The lowest tested factor is 4×; the measured
non-harmonic energy is already very small there, but empty/unresolved bands have
not been relabeled as passes. No factor or curve is selected for the next phase.

All result numbers below are **MEASUREMENTS** or analyses of the linked measured
exports. Signal levels, frequencies, FFT settings, drive settings and analysis
floors under Method are experiment settings, not acceptance results. Statements
about sound are **UNTESTED LISTENING HYPOTHESES**. No hardware accuracy is claimed.

## Method

The opt-in harness is [ShuntMeasurements.cpp](../tests/modules/ShuntMeasurements.cpp),
with an independent double-precision radix-two FFT in
[ShuntAnalysis.h](../tests/modules/ShuntAnalysis.h). The frozen validation helpers
and all transformer source files are unchanged. The original float-FFT off-grid
screen is called unchanged for the conditioning experiment; the independent FFT
is used for the new harmonic decomposition.

Experiment settings:

- Base rate: 48 kHz; stereo material renders; prototype factors 4×, 8×, 16×, 32×.
  Higher-rate comparisons use 64× and 128×. No new series renders or coupling
  comparisons were made.
- Drive: prototype calibration 0 or +6 dB, otherwise descriptor defaults.
  The shipped reference uses Amount 100%, calibration −12 or −6 dB, other voicing
  defaults and level match off. Its Amount mapping contributes +12 dB, so the
  net applied drive is the same 0 or +6 dB. These are transformer-stage-only
  comparisons, not complete-plugin gain/compressor settings, and do not equate
  the different models' physical operating points.
- Material: the exact previous synthetic bass, drums and vocal-like equations;
  unprocessed inputs are also exported. These are not recordings. Clips last
  one second, with one second of preceding silence. The slow log sweep runs
  eight seconds, from 20 Hz to 20 kHz, at peak amplitude 0.8 with edge fades.
- Fixed sine peak: 0.8. Requested frequencies are 55, 110, 440, 1000 Hz, with
  additional 7001, 13001 and 19001 Hz probes. Each is snapped to the nearest odd
  coherent bin of a 65536-frame FFT. Actual frequencies are in `sines.csv`.
  This avoids a literal 1 kHz/48 kHz test hiding folded components on harmonic
  bins. The first full period conditions the circuit; two subsequent periods
  check stationarity. Fundamental energy is excluded from upper-harmonic energy.
- Harmonic/non-harmonic split: exact integer harmonic bins versus remaining
  positive-frequency bins, in bands 20–250 Hz, 250 Hz–2 kHz, 2–8 kHz and 8–20 kHz.
  No FFT window is applied to these coherent, stationary sine periods. Coherent
  aliases can still coincide with harmonic bins, so high-rate waveform
  differences are retained as a separate diagnostic.
- Sweep inspection: Hann STFT, 4096-frame windows and 512-frame hop. Track
  negative-slope peak segments; check frequency and slope against
  `m * internalRate - harmonic * inputFrequency`, require at least 10 dB excess
  over the 128× reference and five consecutive matching segments. The track
  analysis floor is −110 dBFS. All candidate segments are retained, including
  those rejected by the fold tests. This is a limited ridge detector, not a
  proof that arbitrarily weak or overlapping aliases are absent.
- Material files are RMS-matched across the input, A/B factors and shipped
  reference within each material/drive set, with a common peak constraint.
  Raw spectra and all analysis precede matching. `material_bands.csv` describes
  the full material clips; for the longer sweep its band summary uses the
  initial FFT-length excerpt, not the full sweep. Sweep tracking and reference
  convergence use the full sweep. FIR delay is removed offline
  with the existing alignment helper; this is not a live-latency change.

## Harmonic versus non-harmonic results

**MEASUREMENT — worst band-integrated non-harmonic energy over all fixed sine
frequencies and both drive settings, dBFS:**

| Factor | A | B |
|---|---:|---:|
| 4× | −234.47 | −150.63 |
| 8× | −234.47 | −162.86 |
| 16× | −234.47 | −175.08 |
| 32× | −234.47 | −187.17 |

A's extremely low, factor-invariant result is an analysis/input-generation
numerical floor, not an established physical distortion level. An independent
unprocessed-sine control measures −233.56 dBFS in the high band of the highest
probe, consistent with A's result after its insertion attenuation; see
[input_numerical_floor.csv](measurements/shunt_improvement/input_numerical_floor.csv). B's falling
non-harmonic component is consistent with a small aliasing contribution from
its piecewise play law. This inference does not say anything about audibility.
The shipped stage's corresponding measured maximum at its normal 16× factor is
−156.94 dBFS; its float output and different circuit prevent a direct ranking
of underlying numerical quality from these values.

**MEASUREMENT — representative harmonic/non-harmonic separation:** B,
440.185546875 Hz, +6 dB drive, integrated 8–20 kHz band, dBFS:

| Factor | Upper harmonics | Non-harmonic |
|---|---:|---:|
| 4× | −105.76646 | −150.63321 |
| 8× | −105.76646 | −162.85952 |
| 16× | −105.76646 | −175.07984 |
| 32× | −105.76647 | −187.16701 |

Here the upper-harmonic energy persists while the much smaller non-harmonic
energy decreases. More oversampling does not remove the intended harmonics.
All per-frequency, drive, factor and band splits are in
[sines.csv](measurements/shunt_improvement/sines.csv); individual harmonic
amplitudes are in [harmonics.csv](measurements/shunt_improvement/harmonics.csv).
[sine_summary.csv](measurements/shunt_improvement/sine_summary.csv) preserves
band ranges and the literal every-band comparison, including failures.

**MEASUREMENT — reference and stationarity checks:** the prototypes' 64×/128×
full-waveform sine differences range from −155.79 to −128.82 dBr. The worst
successive-period difference is −216.40 dBr. Thus startup leakage is not
responsible for the reported stationary sine split. This is convergence of the
same constitutive model, not validation against a physical transformer.

## Original off-grid failure: conditioning artifact

**MEASUREMENT:** repeat every previously failing shunt periodic setting at
192 kHz, at each originally tested factor, using the original helper and its
unchanged −80 dBr screen. Keep the original conditioning duration and compare
one and two seconds. The 64×/128× analytic references agree below −121.26 dBr
throughout this experiment.

| Conditioning | Candidate off-grid range, dBr | Reference off-grid range, dBr | Original screen |
|---|---:|---:|---|
| 16384 frames / 85.33 ms | −78.36 to −63.99 | −78.36 to −63.99 | Fails |
| 192000 frames / 1 s | −147.89 to −125.08 | −148.57 to −145.74 | All pass |
| 384000 frames / 2 s | −147.89 to −125.08 | −148.57 to −145.74 | All pass |

The high-rate reference has the same large short-conditioned off-grid energy;
longer conditioning removes it from both candidate and reference. The unchanged
screen passing at both later durations identifies the old failure as startup
leakage into the analysis window, rather than establishing a large steady-state
aliasing failure. Smaller real aliases can coexist with that artifact.

**Method correction:** future periodic exports from the spike validation tool
now condition for at least one second, rounded up to its block boundary, rather
than a fixed frame count. The new regression reproduces the old failure and
checks the unchanged screen after conditioning. This policy is supported for
the tested default-parameter workloads; it is not a settling guarantee for
arbitrary future components or settings. Transient/impulse timing is unchanged.
No threshold or solver tolerance was loosened. The full original spike grid was
not rerun or retrospectively relabeled.

[off_grid.csv](measurements/shunt_improvement/off_grid.csv) retains short-window
failures and longer-window results. Original spike exports remain untouched.

## Bass fixture and sweep findings

**MEASUREMENT — raw bass 8–20 kHz integrated level, default drive:**

| Signal | Level, dBFS |
|---|---:|
| Unprocessed input | −64.07010 |
| A, 4× | −64.97649 |
| A, 32× | −64.97651 |
| B, 4× | −64.97639 |
| B, 32× | −64.97642 |
| Shipped stage, matched drive | −64.07013 |

The synthetic input's measured left-channel note-boundary sample steps are
+0.22856, −0.14160 and +0.09114 at the successive note changes. The input
therefore already has broadband transient energy. Its high-frequency tail
cannot be described as entirely generated by the transformer. The near-constant
high-band energy across prototype factors is also inconsistent with a large
factor-sensitive aliasing contribution to that particular tail. This does not
prove that the model adds no harmonics elsewhere, or resolve a listening preference.

**MEASUREMENT:** the sweep tracker found no sustained fold-ridge candidates
meeting its stated level, excess-reference, frequency, slope and persistence
conditions. See [sweep_tracks.csv](measurements/shunt_improvement/sweep_tracks.csv)
and [sweep_ridges.csv](measurements/shunt_improvement/sweep_ridges.csv).
No detected ridge is not evidence of zero aliasing below the analysis floor.

**MEASUREMENT — reference limitations:** material/sweep 64×/128× differences in
the DC–20 kHz band range from −154.48 to −128.80 dBr. This is band-RMS convergence
only; no general peak-error or aliasing certificate follows. Full-band material
differences are less favorable: approximately −96.2 dBr for bass and −61.1 dBr
for drums. The discrepancy is concentrated beyond the measured audio band;
near-Nyquist resampling/alignment is a suspected contributor, not separately
isolated in this step. Full-band failures are retained in
[render_convergence.csv](measurements/shunt_improvement/render_convergence.csv),
alongside the band checks in
[reference_band_check.csv](measurements/shunt_improvement/reference_band_check.csv).

**UNTESTED LISTENING HYPOTHESIS:** changing the synthetic bass note transitions,
the constitutive curve, or high-frequency roll-off could change the owner's
impression. No such change was made, and no listening judgment was made by Codex.

## Listening files, preservation and verification

RMS-matched files are in [audio/](measurements/shunt_improvement/audio/), with
material, model, factor and drive in the filename. Each set includes the
unprocessed input and the shipped magnetic stage at matched applied drive.
Start with the default-drive bass set:
[Input](measurements/shunt_improvement/audio/bass_input_shunt_0x_drive-0dB_tanh_rolloff-off.wav),
[A 4×](measurements/shunt_improvement/audio/bass_A_shunt_4x_drive-0dB_tanh_rolloff-off.wav),
[A 32×](measurements/shunt_improvement/audio/bass_A_shunt_32x_drive-0dB_tanh_rolloff-off.wav),
[B 4×](measurements/shunt_improvement/audio/bass_B_shunt_4x_drive-0dB_tanh_rolloff-off.wav),
[B 32×](measurements/shunt_improvement/audio/bass_B_shunt_32x_drive-0dB_tanh_rolloff-off.wav),
[Shipped](measurements/shunt_improvement/audio/bass_shipped_shunt_16x_drive-0dB_tanh_rolloff-off.wav).

The prototype `tanh` suffix names the constitutive curve; it does not mean the
removed memoryless saturation stage has returned. The shipped and input files
share that set suffix for naming consistency; the model field identifies them.

**MEASUREMENT:** every written WAV was read back as finite stereo floating-point
audio at its intended sample rate/duration. The maximum RMS-matching error on
read-back is below 0.000001 dB. No solver failures, invalid inputs or input-limit
hits were observed in these new exports. This does not cover untested extreme
settings. Per-file gains, peaks, delay alignment and diagnostics are in
[renders.csv](measurements/shunt_improvement/renders.csv) and
[renders_verified.csv](measurements/shunt_improvement/renders_verified.csv).

All shipped source, prototype circuit headers and frozen helpers match the
before-work snapshot. Debug formats remain in Builds; installed plugins were
not replaced. The existing latency test confirms the protected live delay schedule remains
144/144/137/124 samples at 44.1/48/96/192 kHz respectively. Full Debug and
Release suites passed, including the shipped bit-exact baseline, latency tests
and new analysis regressions. The three requested Debug plugin targets and
Tests in both configurations built with no compiler warnings or errors. Final
build/test outcomes are recorded in the provenance. Benchmarks was not built.

The historical proposal sections and coupling addendum moved to
[the archive](archive/ModuleContractSpikePlan.md). The original execution report,
measured summary and unresolved architecture questions remain in
[Architecture.md](Architecture.md). No architecture choice was made here.

## Reproduction and remaining decisions

After configuring and building the requested targets:

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
cmake --build Builds --config Release --target Tests
ctest --test-dir Builds -C Debug --verbose --output-on-failure -E '^Benchmarks'
ctest --test-dir Builds -C Release --verbose --output-on-failure -E '^Benchmarks'

LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/shunt_improvement" LUMIERE_MODULE_TASK=shunt-step1 Builds/Release/Tests '[shunt-report]'
python3 tests/modules/review_shunt.py docs/measurements/shunt_improvement --baseline docs/measurements/shunt_improvement/protected_source_before.json
```

Individual opt-in tasks are `shunt-step1-sines`, `shunt-step1-grid`,
`shunt-step1-renders` and `shunt-step1-convergence`. The grid task reads the
preserved prior spike CSV to identify the settings to repeat. No exports run
in ordinary test-suite execution. The audit script is
[review_shunt.py](../tests/modules/review_shunt.py); it requires an explicit
before-work source snapshot, verifies coverage/finite WAVs/matching/preservation,
and writes [summary.json](measurements/shunt_improvement/summary.json) and
[provenance.json](measurements/shunt_improvement/provenance.json).

**OPEN:** owner listening to the input/model/shipped sets; whether to authorize
Step 2 curve experiments; the factor/reference policy for those experiments;
CPU and latency budgets. No knee, roll-off, solver optimization or latency-option
experiment proceeds automatically from this report. Stop at the Step 1 gate.

# Step 2 — VTB1148 calibration (output transformer)

Executed by Claude in place of Codex, per the [revised brief](CodexBrief-ShuntImprovement.md).
Only parameter values changed. The constitutive curves, circuit, solver,
stopping rule and `fluxScale` are untouched. **One range was widened:** the
`driveCalibrationDb` upper bound in
[TransformerModule.h](../source/modules/TransformerModule.h) went from 24 to
48 dB, because B's fit plus the +18 dB render setting exceeds 24 dB. Shipped DSP,
UI and host state are unchanged, and the shipped stage was not used as a reference.

## 2a. Reference data

[vtb1148_reference.csv](measurements/shunt_improvement/vtb1148_reference.csv)
lists every point used, with source, page/figure, conditions and whether it was
read off a plot. Sources: the Carnhill datasheet
([design guide p.2](references/Carnhill-Design-Guide-Issue1e.pdf)) and Woods (2019).

**MEASUREMENT (published):** across the whole published range (−38 to +16 dBu
at the primary), the VTB1148 **never shows saturation**. Its THD+N **falls** as
level rises: at 20 Hz it goes from about 0.37% at −20 dBu to 0.08% at +6 dBu. At
+4 dBu it falls with frequency, from 0.088% at 20 Hz to about 0.01% at 500 Hz.
The harmonics are odd and even (Fig. 32: H3 −43 dBc, H2 −67 dBc at 20 Hz,
−38 dBu). This is low-level, hysteresis-type distortion. Above about +7.5 dBu at
20 Hz there is no transformer data, because the thesis test driver failed there.
The datasheet gives no maximum level, inductance or distortion figures.

**Exclusions:**
- 50 Hz points, which are contaminated by mains hum (−90 dBu).
- 20 Hz THD+N at −38 dBu, which is dominated by hum and noise. The FFT harmonic
  levels at that level are used instead.
- Points at or above the driver-failure level.

**Noise floor:** the published THD+N includes a noise floor (−92 dBu at 0 dB
gain, from the 1 kHz curve). That floor is added to the noiseless model so both
sides are compared as THD+N.

## 2b. Circuit values

Fit conditions follow the thesis test setup, referred to the unity primary:

| Term | Value | Basis |
|---|---:|---|
| Source | 0.01 Ω (module minimum) | instrumentation-amp driver; output impedance not stated |
| Winding loss | 16 Ω (`loss` 0.016) | measured 8 Ω per primary, series (Woods Table 3) |
| Load | 34 617 Ω | 100 kΩ analyzer ÷ 1.7², plus 42 Ω secondary ÷ 1.7² |
| Level map | peak 1.0 = +18 dBu RMS | owner decision, AES17 reading |

**Correction:** the datasheet's "200 Ω / 600 Ω" is the impedance pair seen at
each side (600 Ω ÷ 1.7² ≈ 208 Ω), not a source resistor. Its +4 dB gain is the
1.7 ratio less winding loss, from a low-impedance driver. The "datasheet" rows
of the level sweep therefore use about 0 Ω source, 12 Ω primary winding, and
600 Ω (+40 Ω winding) load referred.

## 2c. Fit

Method:
- Integer-period DFT of a cosine (zero-mean flux start) at 96 kHz through the
  bare module.
- Grid search over the descriptor ranges, then Nelder–Mead from the best three
  grid points.
- Cost: equal-weight RMS error in dB over 30 targets (20 Hz level series at
  0 and +16 dB gain; 30–500 Hz at +4 and −10 dBu; FFT harmonics at 20 Hz
  −38 dBu and 1 kHz +4 dBu).

**MEASUREMENT — fitted values**
([step2_parameters.csv](measurements/shunt_improvement/step2_parameters.csv)):

| Variant | Drive cal. | Saturation | Knee | Memory | RMS error |
|---|---:|---:|---:|---:|---:|
| A | +31.5 dB | 0.193 | 0.238 | — | 18.3 dB |
| B | +9.84 dB | 1.447 | 0.0614 | 0.989 | 10.9 dB |

**A cannot match the VTB1148.** It has no low-level (hysteresis) mechanism.
Its best compromise is a degenerate deep-drive setting, with THD+N errors of
+10 to +24 dB and harmonic errors down to −43 dB. Its render residual (about
−22 dB on every material and at every drive) is dominated by a linear
low-frequency phase change (+8.7° at 20 Hz), not by harmonics. Treat A as
unsuitable for this reference.

**B matches the main THD+N trends** ([step2_fit.csv](measurements/shunt_improvement/step2_fit.csv)):

| Range | Published | Model B | Error |
|---|---|---|---|
| 20 Hz, −20 to +1 dBu | 0.37 → 0.105% | 0.34 → 0.105% | −4.7 to +0.9 dB |
| +4 dBu, 30–500 Hz | 0.068 → 0.011% | 0.075 → 0.019% | −3.0 to +4.7 dB |
| −10 dBu, 30–500 Hz | 0.156 → 0.019% | 0.122 → 0.014% | −2.7 to +2.6 dB |
| 1 kHz +4 dBu, H3 | −83 dBc | −77.5 dBc | +5.5 dB |

**What B cannot match (owner findings):**

1. **Even harmonics.** Published H2 is −67 dBc (20 Hz, −38 dBu) and −100 dBc
   (1 kHz, +4 dBu). The model gives −98 and −145 dBc. Its symmetric curve
   cannot make even orders. This needs the on-hold asymmetric-curve work.
2. **Saturation onset too early.** At 20 Hz, B starts saturating just above
   +4 dBu: 0.43% at +6 dBu versus a published 0.08%, and 3.5% at +14 dBu
   ([step2_level_sweep.csv](measurements/shunt_improvement/step2_level_sweep.csv)).
   The real part is still clean at +7.5 dBu (20 Hz) and +16 dBu (50 Hz), and
   has no published onset at all. With these parameters, B cannot be both this
   clean at low level and this late to saturate; the fit splits the difference.
3. **Low-level harmonic balance.** At −38 dBu, H3 is 10.7 dB low and H5 is
   8 dB high.
4. **LF response.** B gives −0.08 dB and +0.34° at 20 Hz. Published is
   < 0.5 dB loss (consistent) and roughly 5–10° of phase (not reproduced).
   The phase plot's sign convention isn't stated, and the test circuit alone is
   flat (Fig. 43), so the phase comes from the transformer. Matching it would
   need a lower magnetizing inductance than this fit's `fluxScale` allows.
5. **Memory at 0.989** is close to its upper bound of 1.0. Almost all of B's
   magnetization is in the hysteretic play part.

**Application circuit** (datasheet, 600 Ω load): about −0.47 dB insertion
loss. Distortion is close to the thesis circuit: 20 Hz +2 dBu 0.070%, +14 dBu
2.5%; 1 kHz below 0.011% throughout. Note that at 1 kHz the model's THD falls
with level (0.0105% at +2 dBu, 0.0028% at +20 dBu), as the published curves do.

**Drive meaning after calibration:** "0 dB drive" = drive calibration 9.84 dB
(B), with 0 dBFS = +18 dBu. Extra drive is added on top and compensated at the
output.

## 2d. Renders

Existing synthetic material, scaled to −18 dBFS RMS (0 dBu), 8× oversampling,
RMS-matched per material:
[step2_audio/](measurements/shunt_improvement/step2_audio/),
[step2_renders.csv](measurements/shunt_improvement/step2_renders.csv).

**MEASUREMENT — B, residual after gain match (lower = cleaner):**

| Material | 0 dB drive | +12 dB | +18 dB |
|---|---:|---:|---:|
| bass | −55.1 dB | −34.4 dB | −29.6 dB |
| drums | −32.2 dB | −24.8 dB | −23.9 dB |
| vocal-like | −65.9 dB | −73.5 dB | −73.6 dB |
| log sweep | −56.1 dB | −33.0 dB | −27.9 dB |

- **Octave bands:** within ±0.4 dB of the input in every populated band.
- **Vocal-like:** gains content in its empty 8–24 kHz bands, but 97–109 dB
  below its loudest band.
- **A renders:** included for completeness; A's +18 dB render is limited to
  the new 48 dB drive bound (+16.5 dB extra).
- **Solver:** no failures and no input-limit hits. But B's per-sample
  iterations reach 44–51 (of a 96 cap) on bass and drums at higher drive,
  which matters for the Step 4 CPU work.

## Verification

- The three Debug plugin targets and Tests (Debug and Release) built with no
  compiler warnings.
- The calibration export passed all 745 checks.
- Debug and Release suites: 158/158 passed each, including the shipped
  bit-exact baseline and latency tests.
- Benchmarks not built.
- Reproduce:

```bash
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/shunt_improvement" LUMIERE_MODULE_TASK=shunt-step2 Builds/Release/Tests "Export VTB1148 shunt calibration"
```

**OPEN — owner decisions:**
- Listen to B at 0/+12/+18 dB against the input.
- Whether to authorize Step 3:
  - an asymmetric curve for even harmonics;
  - a way to delay saturation onset while keeping the low-level hysteresis
    (most likely a curve/knee change, since the present parameters cannot do both).
- Whether A should be retired for this module.
- CPU and latency budgets.

Stop at the Step 2 gate.
