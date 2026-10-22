# Archived module-contract spike plan

Historical proposal and coupling addendum. The execution report remains in
[Architecture.md](../Architecture.md); the current shunt-only research is in
[ShuntImprovement.md](../ShuntImprovement.md). These proposals do not authorize
new production work.

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
in [MagneticTransformerLive.md](../MagneticTransformerLive.md). Reuse those criteria
without loosening them or modifying the frozen reference helpers. Any additional
adapter or reference code belongs to the isolated Tests-only directories.
The earlier [diagnosis](../TransformerCommitDiagnosis.md) supplies context for
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

