# Lumiere modular architecture: isolated contract spike

## Status and interpretation

**DECISION:** the product direction is one analog-inspired compressor whose tube
stages, input/output transformers, compression circuits and PSU are independently
swappable, including free-for-all combinations and independent Off choices. See
[ProjectLumiereVision.md](ProjectLumiereVision.md) for the product workflow.

**DECISION:** the aim is physically motivated behavior within each part, free
combinations between parts, tuned by ear, with no hardware-accuracy claims.

**DECISION:** the owner authorized the combined Tests-only spike on 2026-10-22.
The original plan below preserves its constraints and open questions; the
execution report records the resulting evidence. No production integration,
other module family or UI work is authorized. Stop after this report and leave
architecture and listening decisions to the owner.

Labels throughout these documents mean:

- **DECISION:** owner-selected direction, constraint or scope.
- **PROPOSAL:** an experiment to test, not an accepted production design.
- **OPEN:** a result or policy still needing evidence or an owner decision.

Numeric specifications in the original plan below are requirements, proposed
experiment settings or inspected source constants, not measurement results.
Numerical results in the execution report and linked exports are labeled
**MEASUREMENT** and identify their workload and method. Any suggestion about
sound is an **UNTESTED LISTENING HYPOTHESIS**. No hardware accuracy is claimed;
module names and comparisons must not introduce hardware brand names.

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

## Step 1 — PROPOSAL: minimal module contract and tests first

Define a small header-first contract with:

- `prepare(sampleRate, maxBlock, channels)`, `reset()` and audio-block
  `process(...)`, all `noexcept` and allocation-free; a separate control-signal
  process variant supports future detector modules.
- Reported latency in samples, fixed after preparation, plus tail length.
- Oversampling metadata: nonlinear flag and preferred factor, with explicit rate
  ownership so adjacent modules can eventually share an oversampled region.
- Fixed modulation inputs, such as a future supply-rail value, with neutral
  defaults and an explicit update/timebase policy.
- An Off stand-in: neutral, identical latency, no model state and negligible CPU.
- A descriptor table with permanent parameter ID, label, unit, minimum, maximum,
  step, default and skew, readable by the workbench, presets and a future randomizer.
- A permanent part ID and version string.

Add a short chain container with a fixed slot capacity, each slot holding a module
or its Off stand-in. Total chain latency must remain constant across selections.
**OPEN:** slot capacity, delay budgets, delay ownership and mixed-rate latency
accounting. A delayed bypass needs history: resolve the requested stateless Off
by separating model state from chain-owned delay storage, rather than promising
a nonzero delay without state.

Write contract tests before the implementation: finite output for NaN/Inf/huge
input, measured versus reported latency, bit-exact delayed Off, deterministic
reset, block-size independence and stereo independence. **OPEN:** reconcile
bit-exact Off with nonfinite-input sanitization by defining the input domain and
sanitizer boundary explicitly. Define the latency measurement stimulus and method
so nonlinear waveform reshaping is not mistaken for transport delay. Do not
silently weaken either contract to obtain a passing result.

## Step 2 — PROPOSAL: isolated transformer module

Build a simple source-impedance, shunt-magnetizing-branch, secondary and load
circuit. Keep its nonlinear magnetizing element swappable behind a small interface:

- **Variant A:** smooth flux-based saturating inductor, using a tanh/arctan-style
  constitutive relation, without hysteresis.
- **Variant B:** the same element plus a reduced hysteretic contribution from
  the existing play-operator approach, using three or four operators (proposed
  experiment size, not a validated reduction).

This reopens simple constitutive functions for the isolated comparison; it does
not restore the old memoryless saturation stage to the shipped plugin. Do not
include the old LF emphasis shelf. Keep the input limiter separate and explicit,
with its level, placement and engagement counters reported.

Expose shunt-magnetizing and series-element topologies as selectable experimental
modes. **UNTESTED LISTENING HYPOTHESIS:** topology may change the perceived
"layered versus committed" character. No preference or sound-quality conclusion
is established without listening.

Provide descriptors for saturation point, knee, memory amount for variant B,
loss and drive calibration. **OPEN:** exact equations, units, ranges, steps,
defaults, modulation rules and topology semantics. Do not invent a calibrated
material or hardware target. The execution report distinguishes the implemented
descriptors from the still-pending panel hookup.

## Step 3 — PROPOSAL: measurement matrix, no tuning to pass

For both variants and both topologies, use mono and stereo at 44.1/48/96/192 kHz
(required experiment rates, not measurements). Record default and extreme-drive
workloads with explicit levels, block sizes, warm-up and measurement windows.
Compare the shipped play/Preisach transformer on the same workloads.

| Required measurement | Record and compare |
|---|---|
| Release CPU | Median, worst block and 99.9th percentile block cost, plus maximum solver/iteration count per block. State machine, configuration, timing scope and repeat policy; distinguish one-core realtime budget from a host CPU meter. |
| Latency | Reported and measured values; confirm constant latency when selecting Off, A and B, including topology changes. |
| Off | Cost versus an empty chain and bit-exact delayed-bypass results under the defined input policy. |
| Aliasing/accuracy | Reuse existing validation harnesses against converged high-rate references at factors 1/2/4/8 (required candidates). Apply existing screens unchanged; identify the lowest passing factor, or report that none passes. |
| Possible later optimizations | Estimate how ADAA or shorter filters might help, if relevant. Label unmeasured estimates explicitly; do not add either technique unasked. |
| Switching | Switch modules and topology mid-stream; compare maximum sample step against a no-switch control. Propose a crossfade only if evidence shows it is needed. |
| HF survival | Use a quiet 2 kHz probe under heavy bass (required probe frequency). Compare both topologies with the limiter engaged and out of the way; report attenuation, survival-ratio definition and limiter/invalid-input diagnostics separately. |

The existing acceptance screens and reference-convergence method are documented
in [MagneticTransformerLive.md](MagneticTransformerLive.md). Reuse those criteria
without loosening them or modifying the frozen reference helpers. Any additional
adapter or reference code belongs to the isolated Tests-only directories.
The earlier [diagnosis](TransformerCommitDiagnosis.md) supplies context for
separating topology from input clipping; its results are not new spike results.

## Step 4 — PROPOSAL: shared oversampling feasibility only

Design the interface for shared rate ownership in the contract, then test it with
the transformer followed by a trivial additional nonlinear stage. Compare each
module oversampling independently against one shared oversampled region. Measure
total latency and CPU, report savings or regressions and interface complexity,
and explain any filtering/response differences that affect comparability.

This is a design experiment, not production integration or an authorization to
replace the shipped filters. Any test-only sharing implementation must stay in
the isolated spike. Factor negotiation, latency padding, reset/swap semantics and
whether selected modules truly permit sharing remain **OPEN**.

## Inter-module coupling addendum — original experiment plan

**DECISION:** this addendum has the same isolation, realtime, dependency,
latency/bypass, frozen-reference, solver-tolerance and reporting constraints as
the main spike. Its fixture, optional metadata, comparisons and renders are part
of the authorized isolated experiment. No other module family or production
integration is authorized; the execution report does not settle this plan's
OPEN architecture questions.

### Step A — OPEN: Audio coupling vs electrical coupling between modules

An audio buffer alone communicates signal samples, conventionally representing
voltage; it does not communicate terminal current or the load presented to the
preceding stage. Source/load interaction requires an additional interface or
coupled calculation. Supply-rail modulation alone does not establish loading.
**UNTESTED LISTENING HYPOTHESIS:** loading interactions may contribute to the
character the owner wants; the spike must expose the question without settling it.

| Coupling level | Meaning in this proposed experiment |
|---|---|
| Mode 1: ideal buffered audio | Pass the voltage signal unchanged between stages, with no inter-module loading. Internal behavior of each module still applies. |
| Mode 2: impedance-aware one-way coupling | Use the sender's reported output impedance and receiver's reported input impedance to apply a divider. The sender is not re-solved in response to the receiver. |
| Mode 3: true two-way reference | Solve the source fixture and transformer together offline. A future general interface might use wave-variable ports or a shared circuit solve; neither is selected by this plan. |

These mode numbers are identifiers, not measurements. **OPEN:** what level is
sufficient for the free-swap product, how impedance depends on frequency, level
and state, and which interactions require a joint solve rather than a one-way
approximation. No coupling level is accepted as the production contract yet.

These levels are not a guaranteed CPU ranking. Lower cost for buffered or scalar
divider coupling is an expectation to test; measure every available mode. WDF
(wave-digital-filter) and shared-solve approaches overlap: a WDF realization with
interacting nonlinearities can itself require a coupled solve. Neither approach
is automatically the cheapest, most accurate or least compatible with swapping.

A divider applied after a saturating source cannot reproduce loading-induced
changes inside that source. The source's nonlinear output has already been
computed; the divider changes the voltage delivered to the receiver. The offline
two-way reference is intended to measure the resulting gap, not assume its size.

### Step B — PROPOSAL: source-stage test fixture only

Build a minimal saturating source with configurable output impedance to drive
the experimental transformer. Label it explicitly in code and documentation as
a **test fixture, not a tube model or product module**. Give it the same contract
surface: allocation-free, `noexcept` preparation/reset/block processing, fixed
latency and an Off stand-in. Keep code in the isolated Tests-only spike
directories. Source equations and parameter settings remain **OPEN**.

### Step C — PROPOSAL: optional impedance metadata

Extend the proposed module descriptor with optional input/output impedance
reporting, using a scalar or a small fixed-capacity frequency-dependent table.
Unreported impedance means ideal coupling; Off stand-ins report neutral ideal
impedance. Preserve all existing contract tests, latency and Off behavior.

Add contract tests for neutral Off reporting, ideal treatment of omitted metadata
and finite output for impedance edge cases. **OPEN:** representation of ideal
ports without unsafe nonfinite arithmetic, impedance units, valid bounds,
invalid-value handling, table capacity/interpolation, and whether frequency data
includes phase/reactance. A frequency table alone is not an implementation of a
time-domain divider. Document the actual realization if that experiment proceeds.
Keep any metadata storage fixed and do not add runtime allocation or locks.

### Step D — PROPOSAL: compare coupling levels

Run each available coupling mode with transformer variants A and B and both
shunt and series topologies from the main spike:

- Ideal buffer: voltage passes unchanged between modules, without loading.
- One-way divider: begin with scalar impedances. Document the exact application
  of `vReceiver = vSource * Zin / (Zout + Zin)`, including the signal reference
  point, any transformer impedance referral and assumptions. Avoid double-counting
  source impedance already inside a transformer model.
- **Mode 2b — load-aware source, optional, only if cheap:** pass the downstream
  load impedance to the source fixture as a parameter and let it adjust the
  fixture's operating behavior, for example drive level or bias point. This
  changes the source's distortion as well as the voltage reaching the transformer.
  Document the exact rule, parameter mapping, update policy and interaction with
  the divider; compare it against the two-way reference alongside modes 1 and 2.
  This remains a one-way approximation, not a jointly solved electrical port or
  a tube model. If it is not inexpensive, leave it unimplemented and report why.
- Two-way reference: use a coupled source-fixture/transformer solve at converged
  high oversampling, **offline only**, as a measurement tool. It is not a
  production candidate. Its equations, convergence checks and cost must be clear.

For frequency-dependent impedance, implement the scalar experiment first. Before
measuring any table/curve version, define its time-domain realization: filter
structure, phase handling and state, including reset/swap behavior. Attempt it
only if inexpensive. If that realization is not defined, report the table/curve
case as untested; do not guess its response, cost or equivalence to a circuit.

**OPEN — prepared configurations:** could a small set of fixed, prepared circuit
configurations preserve modularity while using shared solves under fixed
connection rules, such as a particular source type feeding a particular
transformer type? Record viability and limitations as a design question only.
Do not build prepared shared-solve configurations as part of this amendment.

**DECISION:** if the two-way reference needs more than modest, clearly labeled
effort, report that limitation and stop the coupling comparison at the ideal and
one-way modes (including the optional load-aware mode only if inexpensive).
Do not expand the spike into a general circuit solver. Without
that reference, comparisons between cheaper modes are not evidence of accuracy
against true two-way coupling. See the execution report for reference scope,
measured convergence and remaining limitations.

### Step E — PROPOSAL: measurements without tuning to pass

Sweep source output impedance and transformer load at several drive levels. The
suggested source range is 100 times below through 100 times above nominal
transformer input impedance (proposed sweep endpoints, not measured values).
**OPEN:** define nominal impedance, frequency/reference conditions, load/drive
settings, block sizes and measurement windows before running the comparisons.

| Required measurement | Record and compare per setting |
|---|---|
| Reference differences | Ideal, one-way and optional load-aware outputs against the two-way reference where available: null/difference level in dB, with its normalization and alignment defined; low-frequency corner, saturation onset and harmonic spectra. Identify both matching and divergent regions per setting. Do not substitute a missing reference with an assumed ground truth. |
| Release CPU | Median, worst block and 99.9th percentile block cost for each available mode at 48/96/192 kHz (required experiment rates and statistic, not results). Report solver iterations where applicable. Identify reference oversampling, timing scope and any difference between offline-reference cost and realtime workloads. |
| Latency | Reported versus measured latency, constant across coupling modes and swaps. Make offline-reference alignment and any test-only delay padding explicit; do not change shipped latency. |
| Switching | Switch coupling modes and source-fixture instances/settings mid-stream. Report maximum sample step against a no-switch control. Propose a crossfade only if measurements show it is needed. |
| Contract fit | Record ordering and metadata-update problems, Off/absent-impedance handling and modules such as detectors or PSU that may have no meaningful audio-port impedance. Do not implement those module families. |

No tuning of models, references or acceptance screens to obtain a passing result.
Separate numerical checks from **UNTESTED LISTENING HYPOTHESES**. No numerical
measurement results are available for this addendum yet.

### Step F — PROPOSAL: owner listening renders

Render level-matched WAV comparisons using bass, drums and a vocal-like signal.
At selected source impedances, pair ideal-buffer and one-way outputs and include
the two-way reference where available. Clearly name the files and place them
next to the measurements. Record stimulus provenance, gain matching, alignment,
variant, topology, coupling mode, source/load settings and safety-limit activity
so the comparison is reproducible. Do not claim how the renders sound: the
owner must listen. Render files and their numerical verification are linked in
the execution report; there is no listening verdict.

### Step G — DECISION: report in place and stop

Update this document with the three coupling levels, what was tested, every
measurement, where each cheap mode tracks or fails the reference per setting,
cost per mode, proposed optional contract changes and every untested item.
Include the optional load-aware mode if tested, or explicitly mark it untested.
Use the differences to identify which module boundaries might justify real
coupling in a hybrid design. Do not choose the architecture; present the evidence
and leave that decision to the owner. List open owner decisions.

Answer whether ideal or one-way coupling gives enough for the planned free-swap
design at an affordable CPU cost using numerical errors and measured costs only.
**OPEN:** error and CPU budgets must be explicit before that numerical acceptance
decision can be made. Sound suitability remains the owner's listening judgment.
Any conclusion that ideal buffers are "enough" must state its scope: the actual
material, levels, part combinations and listening conditions tested. Numerical
agreement alone does not establish listening equivalence; an unperformed
listening comparison remains untested.

Then stop. Do not begin tube, PSU or compression modules, a production coupling
system or UI work. The source fixture remains a test fixture. This addendum
records experiments and questions; it does not settle the coupling architecture.

## Step 5 — DECISION: eventual report and stop boundary

Update this document with the resulting contract, what tests actually proved,
every measurement, surprises, awkward interface choices, recommended contract
revisions and all untested items. Compare A, B and the shipped core candidly for
CPU, latency and the lowest factor passing existing aliasing screens. Separate
numerical checks from listening hypotheses. Do not recommend sound quality;
that requires owner listening. List open owner decisions, then stop.

The eventual deliverables are contract headers/tests, the isolated transformer
variants, measurement tools and this updated document, with existing Debug and
Release tests passing. Do not begin tube, PSU, compression-module or UI work.

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
