# Shared magnetic transformer

The live plugin, Standalone and active offline measurements share the voltage-driven
play/Preisach magnetic circuit. The private LF pre-emphasis/inverse post-emphasis
shelf pair is now **removed entirely**. The oversampled path is input -> magnetic
circuit -> output. This intentionally changes enabled transformer sound, including
recalled projects. Amount zero and Xfrm Out retain exact delayed bypass; no host
parameter ID, order, range, step or default changes.

The old memoryless tanh/residual-ADAA audio engine remains removed. The internal
magnetic constitutive tanh remains, evaluated by the same prepared double-precision
polynomial table. This is a normalized, uncalibrated circuit, not a VTB1148 fit.
No statement about hardware accuracy or subjective improvement is implied.

## Circuit and realtime behavior

The equations remain `B=H+k*M`, `D*x=H+tau*dB/dt`, `y=x-H/D`. The carried accepted
branch-slope predictor, monotone bracket and safeguarded Newton/bisection fallback
are unchanged. The scaled root tolerance stays 1e-13 and the bounded evaluation
cap stays 96. Only accepted trials commit magnetic history. The constitutive lookup
retains its local degree-five polynomials and double-precision error checks.
The frozen `tests/helpers/TransformerAudioReference*` files are untouched.

Placement remains after compressor/makeup and before OUT gain. Detector, SC modes,
FB/FF, program-dependent release, gain reduction, BA features and meters are
unchanged. Each channel has independent magnetic state. Amount still maps -24 to
+12 dB drive with 20 ms smoothing; private calibration offsets that drive mapping.
There are no shelf coefficients, shelf state or per-sample biquad calls inside
`TransformerSaturation` now. Sidechain filters and LFM are separate and unchanged.

Two fixed banks retain the 20 ms voicing crossfade. The incoming bank first runs
muted for 20 ms; a 100 ms rest coalesces rapid edits after each fade. Lookup tables
and play weights stay prepared; only scalar magnetic coefficients are updated.
See the current workbench measurements below for transition CPU and limitations. Out/Amount activity uses its existing 10 ms
fade. The optional linked RMS audition matching remains after the transformer;
it never affects the detector and is bypassed for exact zero/Out behavior.
Rendering uses fixed/prepared storage, with no allocations, locks, logging or UI.

## Rate, latency and bypass

The shelves were zero-latency IIRs; removing them leaves the FIR design and all
integer delays unchanged. Existing direct-stage and real-processor latency/bypass
checks cover mono/stereo and control changes.

| Host rate | Internal factor | Reported latency |
|---|---:|---:|
| 44.1 kHz | 16x | 144 samples |
| 48 kHz | 16x | 144 samples |
| 96 kHz | 8x | 137 samples |
| 192 kHz | 4x | 124 samples |

The host still receives a conservative two-second tail. Amount zero or Xfrm Out
skips/resets the magnetic solve and outputs original samples shifted by the exact
integer delay. FIR histories stay warm for re-engagement. Dry Mix, Delta and global
bypass retain that same delay; `Delta = mixed - aligned dry` remains unchanged.
No oversampling parameter, new latency or stereo mode is introduced.

## Permanent voicing workbench

Transformer Voicing remains a collapsible, model-agnostic tuning panel alongside
the generic editor and meters. A descriptor table defines each private ID, label,
unit, range, step, default and skew. Drive calibration remains first, followed by
magnetization weight k, memory and magnetic circuit corner. Their defaults retain
the previous shelf-free engine bit-for-bit. UI sliders, A/B values, quantization, JSON and measurement metadata use it.
Packing widths are computed from descriptors; both snapshots and flags currently
fit in one lock-free atomic. A compile-time limit prevents silent overflow if
future controls outgrow it. Audio reads one word without retry loops.

Version 4 exports identify `lumiere-voicing-workbench-v4`. Versions 1, 2 and 3
JSON and project state retain drive calibration, selection and audition flags,
with defaults for newly introduced magnetic controls. Old `emphasisDb` and
`cornerHz` never control shelves or the new `magneticCornerHz` descriptor. Panel status explains this migration;
it is not an error, and obsolete fields never re-enable shelves. The notice is
preserved on save/export. Unknown future schemas are rejected without replacing
current private settings; projects with no private child still load defaults.
The archived tanh favorite remains unchanged, without a promise to reproduce its
old sound. See [TransformerVoicing.md](TransformerVoicing.md) for the workflow.

## Before/after measurements

Baseline renders were captured from the existing Release build **before any code
change**: the shipped A/B tone grid, full independent validation grid, clamp counts
and isolated complete-plugin CPU workload. [Before data](measurements/magnetic_shelf_removal/before/tones.csv)
and [after data](measurements/magnetic_shelf_removal/after/tones.csv) are retained,
not used as bit-matching targets. A is default calibration; B migrates the archived
favorite's drive calibration. Before B also had its old shelf voicing; afterward
neither slot has shelves. Measurement CSV settings now use descriptors generically.

The numerical validation reference was updated by removing its independent
emphasis states, so it models the same input -> core -> output chain. It still
uses analytic high-rate voltage, scalar constitutive evaluation and independent
FIR decimation, extending reference rate as required for convergence. Candidate
band/off-grid/applicable tail screens stay -80 dBr, peak error 0.1 dB; reference
convergence remains -100 dBr with maximum band error 1e-4. Alignment removes FIR
delay only, retaining real magnetic phase and decay. No thresholds were loosened.

### Clamp and probe measurements

48 kHz, Amount 100%, default calibration, +24 dB IN; a 60 Hz bass tone plus a
-60 dBFS 2 kHz probe. Counts are post-warmup deltas per channel; percentages use
385,024 internal samples. Probe gain is relative to the probe after IN. The
probe response is isolated using opposite probe polarities, rather than counting
bass distortion as probe output. L/R bass phases differ by 90 degrees.

| Bass peak before IN | Before limitedInputs L / R | After L / R | Before probe gain L | After probe gain L |
|---:|---:|---:|---:|---:|
| -12 dBFS | 0 / 0 | 0 / 0 | -0.000384 dB | -0.000379 dB |
| -6 dBFS | 203230 / 204254 | 0 / 0 | -6.543842 dB | -0.000384 dB |
| 0 dBFS | 299895 / 300804 | 0 / 0 | -13.177309 dB | -0.000394 dB |

For full-scale bass, the measured core-input peak drops from about 47.39 to 15.86,
below the unchanged ±16 guard. Before, roughly 78% of samples were limited; after,
none are limited in this grid. The -6 dBFS bass case similarly drops from roughly
53% to none. Both channels report zero invalid inputs before and after. This does
not guarantee no limiting on intersample overshoots, higher-than-normalized input,
compressor makeup or other material. The guard itself is unchanged and tested.
The quiet HF carrier still largely survives: shelf removal does not change the
core's `x-H/D` structure into a full-band series saturator.

### Complete-plugin Release CPU measurements

Same default host parameters, Amount 100%, default calibration, 0.7-peak stereo
60/73 Hz tones, 256-frame blocks. Median of seven 16,384-frame runs. Includes
stimulus generation, compression, transformer, BA routing and meter publication;
excludes preparation and file output. Each timing run is isolated from builds and
tests. The changed magnetic operating point is part of this comparison, so the
CPU delta is not solely the cost of deleting two biquads.

| Host rate | Before one-core budget | After | Delta |
|---:|---:|---:|---:|
| 48 kHz | 25.69% | 33.50% | +7.81 percentage points |
| 96 kHz | 23.82% | 32.21% | +8.38 percentage points |
| 192 kHz | 24.94% | 33.49% | +8.55 percentage points |

### Tone-grid measurements

Representative default-slot A results at -24 dBFS, Amount 100%, 48 kHz. These
quantify an intentional sound change, not pass/fail targets. Harmonic values are
Fourier measurements relative to the fundamental, not listening judgments.

| Tone | Fundamental gain before / after | H3 before / after |
|---:|---:|---:|
| 20 Hz | -1.903 / -0.702 dB | -23.93 / -22.14 dBc |
| 60 Hz | -0.101 / -0.048 dB | -25.28 / -52.32 dBc |
| 200 Hz | -0.012 / -0.012 dB | -67.08 / -70.77 dBc |
| 1000 Hz | -0.001 / -0.001 dB | -87.81 / -88.08 dBc |

[Full tone changes](measurements/magnetic_shelf_removal/tone_changes.csv),
[before clamp diagnostics](measurements/magnetic_shelf_removal/before/diagnosis.csv)
and [after diagnostics](measurements/magnetic_shelf_removal/after/diagnosis.csv).

### Numerical outcomes

The complete independent shipped-stage grid passes before and after with unchanged
convergence, band, peak, off-grid and tail limits. The worst residuals are:

| Metric | Before | After |
|---|---:|---:|
| Band residual | -103.73 dBr | -103.73 dBr |
| Reference convergence | -105.19 dBr | -105.19 dBr |
| Applicable off-grid power | -139.21 dBr | -138.89 dBr |
| Applicable tail error | -159.55 dBr | -157.01 dBr |

The solver probe stays finite with zero failures before and after; its maximum
is 61 evaluations and its worst scaled root residual is 9.999981e-14. Its outputs
are identical because the core and solver did not change. Reassembling the
committed equation in another floating-point order remains within the probe's
existing roundoff bound. The live grid reaches at most four evaluations, with
zero solver failures. All four host latencies and exact delayed bypass checks pass.
Debug and Release test suites pass; requested plugin and Tests builds are warning-
and error-free. Benchmarks was not built. Detailed machine-readable outcomes and
provenance accompany the retained captures.

All tone, clamp and quality results are synthetic numerical measurements. CPU
values are local medians for the stated workload, not worst-case deadline budgets.
Whether removing the shelf feels more natural, less parallel, or otherwise better
is an **untested listening hypothesis**. The remaining `x-H/D` structure can still
preserve a quiet HF carrier; removing a shelf does not add a series commit stage.
Winding capacitance, leakage, eddy-current dynamics and hardware calibration remain
outside this simplified model.

## Behavior and test changes

- Enabled transformer processing loses the LF excitation boost and inverse output
  shelf. The circuit equations and safety guards do not change. The before/after
  tone grid quantifies the resulting response and harmonic changes.
- The obsolete identity/shelf-inverse test is deleted because those filters no
  longer exist. Small-signal, odd-harmonic, low-frequency, automation, queued bank
  edits, DC-transition and finite-output assertions retain their existing limits.
- The independent shipped-stage reference no longer has emphasis filters. Its
  quality and convergence limits remain unchanged. The frozen v1 reference is
  not rewritten to match the new live sound.
- The clamp diagnostic replays direct input, rather than shelving it. Its default
  +24 dB IN endpoint now expects no limiting and high probe survival for the tested
  normalized bass. Independent over-limit/invalid-input guard tests remain.
- Tests of private values use the descriptor array rather than the deleted fixed
  triple. Incomplete-current-data tests remove drive calibration, not a deleted
  shelf key. Archived favorite tests now verify drive/flags migration and the
  visible ignored-shelves notice, rather than obsolete shelf recall.
- New checks cover descriptor defaults/quantization/ranges/nonfinite values,
  dynamic slider construction, both legacy schemas in project state, future
  rejection, sticky migration status and coherent publication of both slots/flags.
  The original torn-read test remains. Measurement tests compare the current
  descriptor capture and metadata. Its numeric Fourier tolerances are unchanged.
- Missing project-private state still resets to defaults. An unrecognized future
  private child preserves existing private settings while host state restores.
  Copy/paste, measurement cancellation/progress, stale capture detection, meter
  independence and bit-exact delayed bypass remain tested.

## Reproduce

Use the repository's Xcode workflow; never build Benchmarks:

The three-format Release build deploys AU/VST3 into the user's plugin folders.
With Xcode, AU deployment waits until Standalone's post-build phase, after the AU
dependency has completed native local signing. Building AU alone leaves its
bundle in the build directory. Debug cannot overwrite installed Release copies.
Deployment stages and verifies each bundle, preserving the previous installation
under `Builds/plugin-backups`. Reload the DAW plugin after deployment; an existing
instance continues running its previously loaded binary. Use the Release Standalone
bundle when auditioning outside the DAW.

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Release --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
LUMIERE_MEASUREMENT_REPORT=/private/tmp/lumiere-after/tones.csv \
LUMIERE_MAGNETIC_LIVE_REPORT_DIR=/private/tmp/lumiere-after \
LUMIERE_TRANSFORMER_DIAGNOSIS_DIR=/private/tmp/lumiere-after \
ctest --test-dir Builds -C Release --verbose --output-on-failure -E '^Benchmarks'
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
ctest --test-dir Builds -C Debug --verbose --output-on-failure -E '^Benchmarks'
```

Create the output directory first. Run CPU export separately, with no overlapping
builds or tests:

```bash
LUMIERE_MAGNETIC_CPU_REPORT_DIR=/private/tmp/lumiere-after \
Builds/Release/Tests '[magnetic-live-cpu]' --reporter compact
```

The standalone solver probe uses the unchanged solve equations and full combined
calibration/drive range, including instant drive reversals:

```bash
clang++ -std=c++23 -O3 -flto -Wall -Wextra -Wconversion -Isource \
  tools/check_magnetic_solver.cpp source/experimental/TransformerAudioCircuit.cpp \
  source/experimental/ConstitutiveLookup.cpp source/experimental/PlayPreisachCore.cpp \
  -o /private/tmp/lumiere-check-magnetic-solver
/private/tmp/lumiere-check-magnetic-solver /private/tmp/lumiere-after/solver_extremes.csv
```

Structured results/provenance are in the [measurement manifest](measurements/magnetic_shelf_removal/manifest.json).
Historical shelf-equipped live results remain archived in
[magnetic_live_v2](measurements/magnetic_live_v2/README.md), and the earlier clamp
findings are preserved in [TransformerCommitDiagnosis.md](TransformerCommitDiagnosis.md).
Magnetization weight, memory and magnetic-corner controls are now private
workbench descriptors, using the bounded warm-up/fade/rest policy described
below. The optional series commit stage remains deferred.


## Magnetic workbench controls

The v4 workbench exposes the existing k, memory and small-signal magnetic corner
alongside drive calibration. Defaults remain 4, 0.35 and 5 Hz; they do not change
the shelf-free default sound. These are private research controls, not host
parameters or a hardware calibration. [Ranges and migration](TransformerVoicing.md)
come from the same descriptor table used by UI, A/B, JSON and measurement CSV.

The inactive bank reuses prepared lookup tables, radii and weights. It updates
bounded scalar coefficients, resets fixed state, runs muted for 20 ms on the
incoming oversampled signal, then uses the existing 20 ms crossfade. A 100 ms rest
coalesces rapid requests to the latest value. Only warm-up/fade run both cores;
no full preparation, table construction, allocation, locking or UI work occurs
in processing. The warm-up is bounded and does not reconstruct arbitrary prior
hysteresis history. It has not been judged by listening as a physical transition.
There is no new audio delay; reported latency remains 144/144/137/124 samples.

### Release CPU measurements

The retained [before](measurements/magnetic_workbench_v4/before/plugin_cpu.csv)
and [after](measurements/magnetic_workbench_v4/after/plugin_cpu.csv) use the same
complete-plugin 0.7-peak 60/73 Hz stereo workload, Amount 100%, default controls,
256-frame blocks, median of seven 16,384-frame runs. Timings were isolated from
our builds/tests; the workload includes stimulus generation and all plugin DSP.

| Rate | Before, one-core budget | After | Change |
|---:|---:|---:|---:|
| 48 kHz | 34.67% | 34.02% | -0.64 percentage points |
| 96 kHz | 32.33% | 32.69% | +0.36 percentage points |
| 192 kHz | 34.20% | 34.09% | -0.11 percentage points |

These small mixed differences do not establish an optimization or a clear
steady-state regression. Checksums agree; exact audio checks use the retained
pre-implementation sample fixture, not the checksum alone.

The separate [edit workload](measurements/magnetic_workbench_v4/after/voicing_cpu.csv)
times processBlock only, stereo, 512-frame blocks, 0.6 seconds per pass and the
median of three passes after warm-up. Default and extreme rows hold settings
fixed. The rapid-edit row requests different calibration/memory every block,
with k=16 and corner=30 Hz. P95 and maximum are measured across all timed blocks.
The extreme fixed configuration (calibration +12, memory 1, k 16, corner 30) costs
less on this input; that does not establish a general best/worst setting.

| Rate | Default average | Extreme average | Rapid-edit average | Edit block P95 | Slowest edit block |
|---:|---:|---:|---:|---:|---:|
| 48 kHz | 33.76% | 21.84% | 39.11% | 56.02% | 61.55% |
| 96 kHz | 32.94% | 20.87% | 36.06% | 56.93% | 58.97% |
| 192 kHz | 34.16% | 22.11% | 32.25% | 58.44% | 68.26% |

These are local numerical timing observations, not FL Studio meter readings or
worst-case deadline guarantees. Host scheduling, other plugins and settings can
change headroom. Release installation retains the safeguards described above.

### Validation and behavior changes

Results and provenance are retained in
[the v4 manifest](measurements/magnetic_workbench_v4/manifest.json).
All requested plugin tests pass in Debug and Release, and all requested format
builds finish without compiler warnings or errors. The initial Debug CTest driver
also enumerated the intentionally unbuilt Benchmarks placeholder as Not Run;
every case in the separately enumerated non-Benchmarks suite passed. The final
Release driver excludes that placeholder, as do the documented future commands.
Benchmarks was not built.
The pre-implementation Debug and Release sample captures were identical and are
stored once as `tests/data/WorkbenchDefaultMagneticV3.bin`; the regression test
only reads it. Mono/stereo, supported rates, zero/half/full Amount and Xfrm Out
are covered. Frozen `tests/helpers/TransformerAudioReference*`, the root
tolerance, oversampling design and host parameter declarations remain unchanged.

Measured results: the shipped-stage grid's worst band residual is -103.73 dBr,
worst off-grid artifact -138.89 dBr and worst applicable tail error -157.01 dBr.
The added voiced-extreme grid reaches -103.13 dBr, -120.21 dBr and -174.22 dBr,
respectively, with no solver failures. Its selected configurations are calibration
+12/k16/memory1/corner30; calibration -12/k16/memory0/corner0.5;
calibration0/k0/memory0/corner30; and calibration+6/k8/memory0.75/corner15.
It covers low/high and close high-frequency multitones and Gaussian transients
at peak4 across supported rates. It does not certify every intermediate voicing,
arbitrary material or the spectrum of a moving control. The panel retains its
250 ms warm-up / one-second analysis window; very low corners can retain slow
transients in that window. Its readings are not guaranteed settled harmonics.

The reconfiguration matrix and separate extended solver probe both report zero
failures and at most 61 evaluations; their maximum scaled residuals remain below
1e-13. Reassembled committed equations retain the existing 2e-13 roundoff bound.
These are numerical checks, not hardware measurements or listening judgments.

New checks compare bounded reconfiguration with fully prepared circuits over
magnetic parameter extremes and the combined -36 to +24 dB drive endpoints,
including DC reversals, deterministic noise and very quiet samples. Existing
solver/reference and latency/bypass checks are retained. The shipped-stage grid
and new voiced extremes use independent analytic-input high-rate scalar
references with the existing -100 dBr convergence / -80 dBr candidate screens;
no thresholds are loosened.

Transition tests cover loud bass, DC, rapid requests during warm-up/fade/rest,
mono/stereo, all supported rates and block partitions 1/127/4096. They retain the
0.05 excess single-sample-step limit, check finite output, final-request recall,
solver status and bit-identical output across partitions. Extreme-control tests
also retain exact delayed zero/Out bypass. State tests now exercise all descriptor
fields in coherent atomic snapshots, old projects and JSON migration, future
rejection, panel bounds and real slider updates. Offline measurement coverage
uses an analytical low-level 20 Hz response with a 30 Hz corner and includes
all descriptors in captured CSV metadata. Existing meter, detector, Mix/Delta,
measurement cancellation and A/B workflows remain covered.

The intentional behavior change is that private edits now use the bounded
warm-up/fade/rest policy, including drive-only edits. Defaults and recalled old
steady voicings preserve the prior magnetic engine. Low-frequency response,
harmonics and hysteresis can change when the new controls are edited. Whether
those changes improve the sound or the sense of committed saturation remains an
untested listening hypothesis. The x-H/D architecture is unchanged, the optional
series commit stage stays deferred, and no hardware-accuracy claim is made.

For full offline checks (CPU must be exported separately):

```bash
LUMIERE_MAGNETIC_LIVE_REPORT_DIR=/private/tmp/lumiere-v4 \
LUMIERE_MAGNETIC_VOICING_REPORT_DIR=/private/tmp/lumiere-v4 \
Builds/Release/Tests '[magnetic-live][validation],[magnetic-voicing][validation],[magnetic-voicing][solver]' --reporter compact
LUMIERE_MAGNETIC_VOICING_REPORT_DIR=/private/tmp/lumiere-v4 \
Builds/Release/Tests '[magnetic-voicing-cpu]' --reporter compact
```
