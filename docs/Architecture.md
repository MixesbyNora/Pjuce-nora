# Lumiere modular architecture: isolated contract spike

## Status and interpretation

**DECISION:** the product direction is one analog-inspired compressor whose tube
stages, input/output transformers, compression circuits and PSU are independently
swappable, including free-for-all combinations and independent Off choices. See
[ProjectLumiereVision.md](ProjectLumiereVision.md) for the product workflow.

**DECISION:** the aim is physically motivated behavior within each part, free
combinations between parts, tuned by ear, with no hardware-accuracy claims.

**DECISION:** the owner authorized the combined Tests-only spike on 2026-10-22.
The [archived original plan](archive/ModuleContractSpikePlan.md) preserves its
constraints and open questions; the execution report below records the evidence. No production integration,
other module family or UI work is authorized. Stop after this report and leave
architecture and listening decisions to the owner.

Labels throughout these documents mean:

- **DECISION:** owner-selected direction, constraint or scope.
- **PROPOSAL:** an experiment to test, not an accepted production design.
- **OPEN:** a result or policy still needing evidence or an owner decision.

Numeric specifications in the archived original plan are requirements, proposed
experiment settings or inspected source constants, not measurement results.
Numerical results in the execution report and linked exports are labeled
**MEASUREMENT** and identify their workload and method. Any suggestion about
sound is an **UNTESTED LISTENING HYPOTHESIS**. No hardware accuracy is claimed;
module names and comparisons must not introduce hardware brand names.

**DECISION — subsequent owner direction:** series is dropped from new research.
Continue with shunt variants A and B only. Historical exports and the execution
report below are preserved. The Step 1 diagnosis, corrected conditioning method
and current stop gate are in [ShuntImprovement.md](ShuntImprovement.md).
CPU and latency budgets remain **OPEN**; no production architecture is selected.

**OPEN — audio coupling vs electrical coupling:** ideal buffered audio,
impedance-aware one-way coupling and true two-way coupling remain distinct.
Supply modulation alone does not provide loading. The archived coupling plan
and the measured report below preserve the assumptions and untested items.

## Isolation and preservation

**DECISION — requirements for the future spike:**

- Do not change shipped source, host parameter IDs/order/ranges/defaults, bypass,
  Mix/Delta alignment or reported latency. The protected latency schedule is
  144/144/137/124 samples at 44.1/48/96/192 kHz respectively (existing documented
  baseline, not remeasured here).
- Keep `tests/helpers/TransformerAudioReference*` frozen and preserve the existing
  circuit's solver tolerance of `1e-13` (protected source constant).
- Put new code in isolated directories such as `source/modules/` and
  `tests/modules/`, linked into Tests only, never the shipped plugin targets.
- All processing uses fixed storage: no allocation, locks, logging or UI calls.
  The proposed preparation/reset/process contract is also allocation-free and
  `noexcept`; capacity and unsupported-input handling need explicit definitions.
- Add no dependencies without asking. If an external wave-digital-filter library
  looks useful, report its license and stop for approval before adding it. A
  small handwritten wave-digital/state-space element is within the proposed scope.
- Do not build Benchmarks. Existing tests must continue to pass in Debug and
  Release when implementation is eventually authorized. Update these documents
  in place; do not create a document per step or put test counts/hashes in prose.

## Current source inspection, not new validation

The private LF emphasis shelves have already been removed. In
`source/dsp/TransformerSaturation.h/.cpp`, each bank's channel contains a magnetic
circuit, and `processBank` sends the oversampled sample directly to that circuit.
There are no pre/post shelf coefficients, state or per-sample shelf calls. The
earlier removal intentionally changed sound; this planning update changes none.
Historical measurements and latency checks remain in
[MagneticTransformerLive.md](MagneticTransformerLive.md); none were rerun here.

The current safety limit is still **inside**
`source/experimental/TransformerAudioCircuit.cpp::processSample`: nonfinite input
is replaced with zero and finite voltage is clamped to `+/-16.0` linear amplitude
(`AudioSettings::maximumInput`, inspected source constant). In the live chain
this is after upsampling, at the circuit entrance, before applying the internal
drive in the forcing calculation. Limited and invalid inputs have separate
diagnostics. The deferred module must instead expose its safety limit as a
separate explicit stage, without modifying this shipped implementation or
mistaking limiting for magnetic behavior. The new stage's level and exact
placement remain **OPEN** and must be reported with the eventual measurements.

The Voicing panel already builds its sliders from descriptors in
`source/TransformerVoicing.h`, and its existing A/B, JSON, copy/paste, measurement
metadata and project state use that schema. However, that schema and its packed
lock-free publication still describe the current transformer. Arbitrary module
registration and a generic module-state publisher are not implemented. Preserve
the permanent workbench; produce the spike descriptors without editing the
shipped panel. Actual panel hookup remains pending a separately authorized UI
and state-publication change.

## Execution report — isolated experiment

**DECISION:** this is Tests-only research. The product architecture remains
**OPEN**. The report distinguishes unit/regression checks, numerical acceptance
screens, unavailable reference results and owner listening. Passing the unit
suite does not certify an experimental model against the aliasing screens.

### Contract and repository findings

The header-first contract is in
[`ModuleContract.h`](../source/modules/ModuleContract.h), with a separate
audio-block and control-block interface, `noexcept` lifecycle methods, fixed
storage, descriptors, permanent part/version identifiers, modulation and optional
impedance metadata. The source fixture is explicitly labeled as a test fixture.
No external library or dependency was added.

The module kernels report zero transport latency. The fixed-slot chain owns
delay storage and pads each selected kernel to its slot budget; a delayed test
fixture also exercises nonzero kernel latency. Off has no model state and retains
the same slot budget. Finite samples, including signed zero, pass exactly through
delayed Off. Nonfinite input is sanitized at the boundary; finite huge samples
are preserved by Off. The transformer has its separate explicit safety stage.
Invalid preparation specifications are rejected. Slot capacity and maximum
kernel delay remain compile-time research limits, not a production commitment.

Checks cover measured transport latency, exact Off, partition/reset determinism,
stereo independence, invalid samples and safe scalar impedance handling. The
control-signal interface is defined but has no detector implementation or
behavioral validation. Allocation freedom is supported by fixed-storage code
inspection; no allocator-interposition audit was performed. Tail metadata uses
an asymptotic-tail sentinel for the transformer and zero for Off; a finite
audibility-based tail cutoff has not been certified.

**Repository finding:** LF shelf removal has landed in the shipped code.
The Voicing panel is descriptor-driven, but arbitrary module registration,
publication and state migration have not landed. New descriptor tables are
provided; panel hookup remains pending under the no-UI-work constraint. The
shipped generic editor, permanent workbench, host state and DSP remain unchanged.

### Normalized transformer and numerical method

[`TransformerModule.h`](../source/modules/TransformerModule.h) defines the
variants and the scalar state-space circuit. The following are **model design
definitions**, not measured hardware values: impedance base is 1000 ohms, flux
scale is 0.005 seconds and the reversible linear magnetic slope is 0.1. The
secondary is ideal and referred at unity ratio. Source resistance, winding loss
and load are explicitly separate terms. No shelf, capacitance, leakage model or
identified material parameters are implied.

The flux laws are:

```
A: lambda(i) = fluxScale * [0.1*i + saturation*tanh(i/knee)]
B: lambda(i) = fluxScale * [0.1*i + saturation *
     ((1-memory)*tanh(i/knee) + memory*mean(tanh(play_j(i)/knee)))]
```

The reduced play construction uses fixed radii from the new header and retains
history only on accepted roots. A and B are not calibrated to equal small-signal
response or saturation onset. Saturation, knee, memory, winding loss and drive
defaults/ranges are centralized in the descriptor table; memory affects B only.
Winding loss adds normalized resistance, rather than acting as a permeability
proxy. Drive scales excitation and compensates the output scale.

Shunt mode solves `v=(u-Rs*i)/(1+Rs/Rload)` and `d(lambda)/dt=v`, with output
`v`. Series mode solves `vL=u-(Rs+Rload)*i` and `d(lambda)/dt=vL`, with output
`Rload*i`. The latter is a series-inductor circuit experiment, not a claim that
it represents a particular output transformer. Both use trapezoidal integration
and a safeguarded scalar root. Trials do not mutate magnetic history. Newton
effort is bounded before bracket bisection takes over.

**Safety configuration:** `InputSafety` is outside the circuit, before drive,
with a configured limit of +/-16 linear amplitude and independent invalid/limited
counters. Nonfinite samples become zero. Limiter-off measurements leave the
guard out of the way; guard action is not classified as magnetic character.
The corresponding shipped guard remains in its original location inside the
existing circuit. Neither that implementation nor its tolerance was edited.

The first experiment confused fixed flux error with voltage-equivalent accuracy.
At high rates this left quiet tails. The corrected stopping criterion scales
the requested voltage-equivalent accuracy by the timestep and accounts for
floating-point evaluation error using individual constitutive term magnitudes.
Net flux alone is insufficient when hysteretic terms cancel. Independent
small-signal recurrence and cancellation regressions exercise both corrections.
The existing circuit's protected tolerance remains unchanged. Earlier results
are retained in [initial flux-stop exports](measurements/module_spike/initial_flux_stop/)
and [net-flux roundoff export](measurements/module_spike/net_flux_roundoff_stop/).
They are superseded observations, not acceptance evidence for the final kernel.

### Numerical exports and interpretation

All per-setting measurements are retained, including failed screens:

- [Complete accuracy/aliasing grid](measurements/module_spike/validation.csv).
- [CPU, extreme-drive, Off and empty-region timing](measurements/module_spike/cpu.csv).
- [HF carrier survival with and without limiting](measurements/module_spike/hf_survival.csv).
- [Coupling sweep, spectra, corner estimates and reference validity](measurements/module_spike/coupling.csv).
- [Derived coarse onset brackets](measurements/module_spike/onset.csv).
- [Measured latency and exact Off](measurements/module_spike/latency.csv).
- [Immediate-switch sample steps](measurements/module_spike/swaps.csv).
- [Shared versus separate oversampling](measurements/module_spike/shared.csv).
- [Machine-readable summary](measurements/module_spike/summary.json) and
  [provenance/method settings](measurements/module_spike/provenance.json).

**Measurement method:** CPU uses Release, preallocated buffers, input generation
outside the timed call, warm-up followed by a finite block window and empirical
median/P99.9/maximum. These are observed block costs, not guaranteed worst-case
deadlines or a host CPU meter. Prototype diagnostics expose the largest root
iteration count within each block; the shipped accessor exposes a running
maximum including warm-up. Total iterations per block are not instrumented.
The shipped row includes float/double conversion at the tool boundary, and
different models are not calibrated to equivalent distortion. Fixed prototype
factor hints and the shipped rate-adaptive factors must be considered alongside
timing comparisons. Full numeric settings and quantile ranks are in provenance.

The accuracy tool reuses the existing stimuli, metrics, alignment and unchanged
screens, with actual mono and stereo rendering. Analytic-input high-rate renders
avoid candidate interpolation in the reference input. Reference convergence and
candidate acceptance are separate flags. Shared constitutive code makes this
a temporal/resampling comparison, not independent validation of material physics.
An unconverged or failed reference never certifies a candidate.
Off-grid tests apply to periodic stimuli and tail tests to silent-tail stimuli;
nonapplicable metric columns must not be interpreted as acceptance checks.

The HF survival ratio compares the probe increment under bass with the probe
alone. Subtracting a bass-only render removes its harmonics/leakage from the
carrier estimate. This is carrier survival, not total high-frequency energy or
an audibility measure. Limiter counters cover the full warm-up and analysis run.

### Coupling reference scope and limitations

[`SourceFixture.h`](../source/modules/SourceFixture.h) is a behavioral test
fixture. Its unloaded curve is `S(e)=tanh(4*e)/4`. Mode 2 applies the scalar
divider after that curve, using nominal reported input impedance. It does not
modify the source's internally generated distortion. Mode 2b uses the designed
rule `drive=4*(1+0.5*(1-dividerGain))`, then the divider. This is an explicit
one-way loading behavior, not a fitted approximation to the two-way reference.

[`OfflineCoupledReference.h`](../tests/modules/OfflineCoupledReference.h)
solves `terminal=S(excitation-Rsource*terminalCurrent)` jointly with the
transformer equations. Winding resistance remains separate. It is an offline
measurement tool, not a production module or prepared-configuration registry.
The source/load sweep uses several drive levels; CPU exports separately include
reference cost at its declared high factor.

The reference has explicit convergence/solver limits at some extreme settings.
The modest-effort reference scope ends here: invalid rows remain visible with
`reference_valid=0` and must be excluded from fidelity/null conclusions. No
general solver or new dependency was introduced to chase those rows. The
summary uses only valid references for cheap-mode error ranges. These comparisons
include gain, phase and nonlinear error; they are not level-matched nulls.

The corner column is a small-signal step-pole estimate, not a full frequency sweep;
its unavailable sentinel is documented in the tool. Harmonic onset is a coarse
bracket from the tested drive grid and an explicit third-harmonic diagnostic
threshold, not a measured material saturation constant. The full per-setting
harmonic/corner/reference fields remain in the CSV, including invalid-reference
flags. No source/load rule has been selected for production.

Frequency-table realization remains undefined and untested. Ideal tags avoid
representing infinite impedance as a floating-point infinity. Reserved table
metadata receives an explicit ideal fallback in the scalar experiment. Impedance
ordering, meaningful ports for control-only modules, state publication and
automatic rate negotiation remain awkward contract boundaries. A small set of
prepared shared solves remains **OPEN and unbuilt**.

### Latency, switching and sharing scope

Kernel/chain delays and each fixed-factor region retain reported transport
latency across selection changes. Exact Off and FIR identity centroids are
checked separately from a nonlinear circuit's frequency-dependent phase.
High-rate offline references have different raw FIR delays; comparison traces
and audition files are aligned explicitly. They are not a latency-compatible
live mode, and no such claim is made. Shipped latency/bypass checks remain in
the existing regression suite.

Switch results measure immediate cold-state changes against a no-switch control.
The experimental bypass line is cold when first selected, so that discontinuity
is an implementation limitation rather than evidence about physical loading.
The measured excess steps justify a **PROPOSAL** to keep transport delay warm,
prepare the inactive state and evaluate a bounded crossfade before any live
integration. No crossfade was implemented or judged by listening.

The sharing experiment manually groups the transformer and a trivial nonlinear
stage under one rate owner. Separate filtering changes the signal between
stages; shared filtering therefore is not certified as equivalent audio. Rate
negotiation, modulation timebases, state resets, bypass padding and shared-chain
aliasing need further design. No production grouping was built. ADAA and shorter
filters were not implemented, so no numerical savings or quality improvement
from those alternatives is claimed.

### Owner listening and stop boundary

[Listening files](measurements/module_spike/audio/) contain clearly named
variant, topology, coupling and source-impedance comparisons using deterministic
synthetic bass, drums and a vocal-like excitation. They are RMS-matched per
material across the comparison family; this is not perceptual loudness matching.
[Render metadata](measurements/module_spike/renders.csv) and
[WAV readback verification](measurements/module_spike/renders_verified.csv)
record gain, measured RMS and peak. The offline-reference audition uses its
declared fixed high factor; per-file convergence on these synthetic passages
has not been certified. Numerical sweep validity must not be transferred to
arbitrary material. No listening has been performed and no sound verdict is given.

**OPEN — owner decisions:** define acceptable error and CPU budgets; audition
the comparisons; decide whether this source/transformer boundary warrants more
coupling research; decide how much further effort the extreme offline reference
deserves; choose transition/state/latency policies and whether future panel
registration should proceed. The data concerns only this fixture/transformer
boundary and cannot choose coupling for unbuilt tube, PSU or compression stages.
Any claim that ideal buffers are enough must state the actual material, levels,
part combinations and listening conditions tested. The architecture is not
chosen by this report. Stop here; other module families and UI remain deferred.

## Measured summary and numerical conclusions

**MEASUREMENT — complete-stage stereo CPU:** median percent of one core realtime budget, default-drive workload. These rows measure the transformer stage, not the complete plugin. Prototype factors are fixed trial settings; the shipped stage adapts its factor to rate.

| Stage / topology | Trial factor | 48 kHz | 96 kHz | 192 kHz |
|---|---:|---:|---:|---:|
| A / shunt | 4x | 6.10% | 11.95% | 24.04% |
| A / series | 4x | 5.72% | 11.85% | 22.52% |
| B / shunt | 8x | 14.78% | 30.17% | 58.97% |
| B / series | 8x | 13.48% | 26.73% | 52.89% |
| shipped / shunt | 16x / 8x / 4x | 26.58% | 24.24% | 21.81% |

A has the lowest measured cost in these default configurations at the lower rates. The shipped stage costs less at the highest rate; B loses its lower-rate cost advantage as its fixed factor raises internal work. Curves, dynamics and drive calibration differ, so this is a cost comparison of the defined configurations, not equal-quality or equal-distortion performance. Mono, extreme-drive, block maximum, P99.9 and offline-reference costs are all retained in the CPU export.

**MEASUREMENT — accuracy certificates:** no prototype earns a certificate across the full tested grid at any of the requested factors. The band-residual column below uses only settings with converged references; the certificate also requires peak, applicable off-grid and tail screens.

| Variant / topology | Worst valid-reference band residual at 8x (dBr) | Every reference converged? | Lowest certified tested factor |
|---|---:|---|---|
| A / shunt | -99.56 | Yes | None |
| A / series | -51.57 | No | None |
| B / shunt | -99.69 | Yes | None |
| B / series | -51.57 | No | None |

The shunt variants agree closely in band at the higher tested factors, but absolute off-grid screens fail on some high-rate finite windows. **Inference:** the very small candidate/reference band difference alongside shared off-grid content suggests a conditioning/window issue; it does not prove an added aliasing component. A settled-window diagnostic was not substituted to obtain a pass. The series variants have substantial accuracy differences at cheap factors and an unresolved high-frequency reference-convergence plateau. All screens remain unchanged. The shipped configuration retains its previously validated schedule; this spike does not establish a new minimum factor for it.

**MEASUREMENT — coupling difference to valid two-way references:** unnormalized RMS error relative to reference RMS, including insertion gain, phase and nonlinear differences. Invalid reference settings are excluded.

| Mode | Minimum difference (dBr) | Median difference (dBr) | Maximum difference (dBr) |
|---|---:|---:|---:|
| 1 | -102.36 | -31.42 | 38.44 |
| 2 | -53.91 | -6.16 | -0.03 |
| 2b | -53.52 | -6.01 | -0.02 |

Buffered coupling can closely track the reference under light loading and diverges strongly at other settings. The fixed nominal-impedance divider is not uniformly more accurate, and the chosen load-aware rule does not resolve the disagreement. These observations apply only to the fixture/transformer boundary and tested source/load/drive combinations. Full minimum/maximum settings, spectral differences and onset brackets are retained in the summary and CSV. There is no owner-selected error or CPU budget, so the data cannot establish that either cheap mode is enough for the free-swap design. Listening has not been performed.

**MEASUREMENT — HF probe at the heavy-bass setting:** carrier increment relative to the probe-only run, with the configured guard disabled/enabled. This table shows the measurement at the selected rate; all other rates and excitation levels remain in the export.

| Variant / topology | Rate | Bass peak | Guard disabled: survival ratio | Guard enabled: survival ratio |
|---|---:|---:|---:|---:|
| A / shunt | 48 kHz | 32 | 0.9881 | 0.3287 |
| A / series | 48 kHz | 32 | 20.5321 | 3.8115 |
| B / shunt | 48 kHz | 32 | 0.9882 | 0.3285 |
| B / series | 48 kHz | 32 | 13.4138 | 2.4352 |

**MEASUREMENT — delay and switching:** shipped delay remains 144/144/137/124 samples at 44.1/48/96/192 kHz. The prototype fixed-slot chain measures 64 samples, including the nonzero-delay fixture with slot padding; the tested FIR regions report 0/99/124/137 samples at factors 1/2/4/8. Maximum identity-centroid error is 1.99e-13 samples, and every measured Off comparison is bit-exact. High-rate reference raw delay differs and is removed only in offline alignment.

| Immediate event | Maximum excess sample step versus control |
|---|---:|
| A to B | 0.053077 |
| Shunt to series | 0.438878 |
| Buffered to divider | 0.075442 |
| Source fixture impedance change | 0.003979 |
| Active region to cold Off transport | 0.785818 |

**MEASUREMENT — shared region:** median CPU savings range from 21.75% to 32.23% in the tested stereo transformer-plus-shaper chains. Separate FIR transport measures 274 samples, shared transport 137 samples. This is feasibility evidence with changed interstage filtering, not a production audio-equivalence result.

**MEASUREMENT — listening-file audit:** all exported WAV samples are finite, with checked format and peak bounds. Maximum RMS-match error on reading the written files back is 4.99e-07 dB. Listening suitability remains untested.

### Verification and reproducibility

The existing full suites passed in Debug and Release. After the final cancellation-bound correction, the complete new module regressions and shipped bit-exact baseline were rechecked in both configurations; the wide numerical export was also regenerated. Builds of all three Debug formats and both Tests configurations finished without compiler warnings or errors. Debug stayed in Builds; installed formats were not deployed. Benchmarks was not built. Existing source and helpers were verified against the pre-spike snapshot.

The private test sources are discovered recursively by the existing Tests setup. Research headers are excluded from SharedCode. Reproduce the offline exports with the commands below after configuring and building Tests. Each task is explicit opt-in; ordinary test runs do not generate audio or run the profiling exports. Run CPU tasks alone, outside concurrent tests/builds.

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
cmake --build Builds --config Release --target Tests
ctest --test-dir Builds -C Debug --verbose --output-on-failure -E '^Benchmarks'
ctest --test-dir Builds -C Release --verbose --output-on-failure -E '^Benchmarks'

LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/module_spike" LUMIERE_MODULE_TASK=cpu Builds/Release/Tests "[module-report],[module-shared]"
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/module_spike" LUMIERE_MODULE_TASK=validation Builds/Release/Tests "[module-report]"
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/module_spike" LUMIERE_MODULE_TASK=probe Builds/Release/Tests "[module-report]"
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/module_spike" LUMIERE_MODULE_TASK=coupling Builds/Release/Tests "[module-report]"
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/module_spike" Builds/Release/Tests "[module-latency],[module-transition],[module-renders]"
python3 tests/modules/review_reports.py docs/measurements/module_spike
```

**DECISION:** the spike stops here. No shipped transformer replacement, coupling architecture, new module family or UI integration is selected. **OPEN:** reference convergence, conditioning policy, acceptable error/CPU, transition policy and owner listening determine the next authorization.
