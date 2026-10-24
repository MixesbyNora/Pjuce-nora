# Overdrive: from saturation into distortion, and Headroom

**Status:** live (2026-10-24).

## Owner feedback (2026-10-24)

- The full chain saturates well and the voicing controls work.
- Pushed from saturation into distortion, from any drive point and with any
  voicing, it turned "spiky" and didn't react the way analog should. The
  drive points were Digital IN, Makeup, and both Amounts at 100% with IN +2.
- The agreed plan: diagnose each stage, finish the missing physics, then add
  **Headroom** with **0 dBu = −14 dBFS** as its default.

## Diagnosis

Task `saturation-diagnosis` in [TubeStage.cpp](../tests/modules/TubeStage.cpp)
([saturation_spectra.csv](measurements/saturation/saturation_spectra.csv); the
first run is in [saturation_spectra_before.csv](measurements/saturation/saturation_spectra_before.csv)):

- **What it runs:** the plugin at 48 kHz, with stages isolated by their
  In/Out switches. Each is driven from saturation into overdrive with coherent
  tones (100 Hz, 1 kHz, 5 kHz at −12 dBFS peak), at the stages' 18 dB
  calibration reference.
- **What it measures:** THD; low (H2–H5), mid (H6–H10) and high (H11+)
  harmonics; and non-harmonic energy, all relative to the fundamental. It
  also exports waveforms and renders material
  ([saturation_audio/](measurements/saturation/saturation_audio/)).

**Found three causes. None was the tubes' own curves.**

1. **The compressor's program-dependent release reached one sample.**
   - From 12 dB of gain reduction the release sped up to a single sample. The
     gain then followed the waveform within each cycle: 0.9 dB of GR ripple
     per 5 kHz cycle, and only 9 dB of the 26 dB the curve asked for.
   - That made the compressor a clipper. Its rectified detector also aliased
     at 4×, giving −31 dBc of non-harmonic energy at 5 kHz.
   - This was in the digital compressor, before the tubes existed, and it is
     what pushing Digital IN exposed.
2. **The output stage collapsed into the saturated iron.**
   - With both Amounts at 100%, the single-ended 12BH7 follower drove the
     iron about 8× past nominal. The saturated core demanded current pulses
     the follower could not sink, so it cut off.
   - The output dropped to zero for about a third of every bass cycle. THD
     went from 1% to 13% between IN +2 and IN +6.
   - The 1000 µF coupling capacitor then resonated chaotically with the
     saturating inductance (ferroresonance), giving −29 dBc of non-repeating
     energy.
3. **No grid current or blocking.** Overdriven grids rounded nothing off and
   nothing recovered after hits.

## Changes (owner decisions)

**Release Mod** (`releaseMod`, 0–100%, default 50%, version hint 10;
[DbGainEnvelope.h](../source/dsp/DbGainEnvelope.h)).
- From 12 dB of GR, the release moves geometrically from the Release time
  toward **20 ms**, by the Release Mod fraction. At 0% the release is always
  the Release time.
- It is never faster than 20 ms, so the gain cannot follow the waveform.
- The compressor's golden reference audio was recaptured for the new law
  (`tests/data`).

**Push-pull output stage** ([CathodeFollower.h](../source/modules/CathodeFollower.h)).
- The default **Wiring** is a White cathode follower with one 12BH7 section in
  each position:
  - The upper section's 1.5 kΩ plate resistor drives the lower section's grid
    through an ideal coupling capacitor, so the lower section sinks current
    whenever the load pulls back.
  - The lower section is biased by its bypassed cathode resistor, with its
    grid at 0 V DC.
  - Both sections run at the voiced current, with the same plate-to-cathode
    voltage: 140 V each at the 300 V default.
- It is solved jointly with the iron: one more node per sample, solved inside
  the scalar root for the magnetizing current.
- The level is normalized by its linearized gain. It is within 0.01 dB of the
  iron alone at 440 Hz–10 kHz, and within 0.06 dB at 50 Hz.
- **Single-ended** remains a Wiring choice. **Sections** applies only to it.
- The tube workbench is now version 2; older tube voicings load defaults.
  Version 3 adds Output grid and still reads version 2 saves, with Output
  grid at Ideal: a schema whose newer versions only add controls can declare
  `readableSince` ([Voicing.h](../source/Voicing.h)).

**Coupling capacitor default 220 µF** (was 1000 µF): no ferroresonance in the
diagnosis range. The slider still covers 22–1000 µF.

**Grid current** ([TriodeModel.h](../source/modules/TriodeModel.h)).
- Ig = 0.70 mA × V^1.5 above about 0 V, with the onset smoothed over a few
  tenths of a volt.
- Fitted to the GE 5670 sheet's dashed grid-current lines (p5): 2.0, 5.75 and
  10 mA at +2, +4 and +6 V.
- The 6386 and 5751 sheets publish none. They use the same law (inferred).
- **Gain-tube grids:** driven through 4.7 kΩ (inferred: the input iron's
  secondary). Driven positive, a grid draws current and its peaks round off.
  This is memoryless and tabulated once.
- **5751 grid** ([AmpTube.h](../source/dsp/AmpTube.h)):
  - Driven from the gain tubes' plates (22 kΩ, inferred) through 0.1 µF into a
    1 MΩ leak.
  - Grid current charges the capacitor, so after big hits the stage biases
    itself colder and recovers with about a 100 ms time constant. This is
    blocking.
  - It is one scalar solve per sample, only when the grid nears its cathode.
- **The output follower's upper grid:** a workbench choice, Ideal by default.
  See "Output grid current" below.

The band-limited detector planned in step 2 was not needed. With the release
floor, the compressor's non-harmonic energy fell to −85 dBc at 4×.

## Result

Calibration reference, 100 Hz:

| Case | THD | High harmonics (H11+) | Non-harmonic |
|---|---|---|---|
| Output stage (Amounts 100%), IN +6 | 12.9% → **3.0%** | −39 → **−67 dBc** | −45 → **−291 dBc** |
| Output stage, IN +12 | 56% → **17%** | −28 → **−47 dBc** | −29 → **−143 dBc** |
| Makeup +24 (all stages) | 10.4% → **7.0%** | −41 → **−67 dBc** | −41 → **−110 dBc** |
| Gain tube under heavy GR | (rises smoothly with drive) | −38 to −51 → **−79 to −81 dBc** | −96 to −128 → **−137 to −163 dBc** |
| Compressor alone, +6 dBFS at 5 kHz (4×) | 0.34% → **0.018%** | — | −31 → **−85 dBc** |

- **The cliff is now a gradual climb.** The output stage goes 0.6% → 3% →
  17% → 31% across IN +2, +6, +12 and +18 dB.
- **At absurd drive the iron still collapses,** for example both Amounts at
  +18 dB with bass at 0 dBFS, about 19 dB past its 1% point. A saturated core
  cannot induce output; that is transformer physics.
- **Grid current adds a little high-order content where it clamps,** for
  example −57 dBc at Digital IN +24. That is what a real conducting grid does.

## Output grid current

Owner request (2026-10-24): grid current at the 12BH7's upper grid. Owner
decision, after the measurements below: a **tube workbench choice, Ideal by
default**, to judge by ear.

**The network (inferred; no schematic is published).**
- **Source:** the 5751's plate, so Ra ∥ rp = 100 kΩ ∥ 50 kΩ = **33 kΩ** from
  our fit. The 5751 is the stage that drives the follower.
- **Coupling:** 0.1 µF into a 1 MΩ leak returned to the bias, as for the 5751's
  own grid.
- **Grid current:** the 5670 sheet's law, as elsewhere. The 12BH7 sheet
  publishes none.
- **Choices:**
  - *Ideal:* a voltage source. Today's sound, unchanged.
  - *DC-coupled:* the source resistance and grid current only.
  - *Capacitor:* the full network, so the grid also blocks after hits.

**How it is solved** ([TriodeModel.h](../source/modules/TriodeModel.h),
`ResistiveGrid`; [CathodeFollower.h](../source/modules/CathodeFollower.h)).
- Grid current depends only on grid-to-cathode voltage. So for a source's open
  circuit drive d, the grid's voltage w solves (d − w)/R = Ig(w).
- That is a one-dimensional monotone function, tabulated once with its slope.
- The solve gains no unknown: one more table look-up per evaluation. The grid
  current also leaves through the upper cathode.
- **Tube Amount:** the capacitor's charge is kept per unit of the Amount's
  drive. Otherwise a charge built at 100% reappeared 8× larger, relative to the
  signal, when the Amount came down: up to 1.4 full scale of offset in the
  sweep test.

**MEASUREMENT** (both Amounts at 100%, 100 Hz; the diagnosis now settles for
3 s, because the capacitor version settles in about 1.5 s at the hardest drive):

| Drive | Ideal | Capacitor | DC-coupled |
|---|---|---|---|
| IN +6 | 3.0% THD | 3.0% (identical) | 3.0% |
| IN +12 | 19% THD, H11+ −39 dBc | 32%, −29 dBc | 47%, −32 dBc |
| IN +18 | 51% | 76% | 69% |

- **Grid current only flows at extreme drive,** about 12 dB past full scale on
  the tube side. A follower's cathode tracks its grid until the stage cannot
  supply the load.
- **It makes extreme overdrive grittier, not smoother.** At the grid, the clamp
  limits the current the upper section can push into the saturated iron, so the
  stage clips harder, with more high harmonics. The output's peaks rise: that
  is the waveform reshaping, not a softer clip.
- **Capacitor:** after hot hits the tube biases colder, then recovers. That
  gives a recovery thump (tested) and some slow "breathing": −65 dBc of
  non-repeating energy at IN +12. It settles to a periodic state, with no
  squegging (tested).
- **DC-coupled:** no breathing, but it clips hardest.

## Headroom

`headroom`: 6–30 dB, default **14**, version hint 11.
- H dB means 0 dBu = −H dBFS (0 dBFS = +H dBu).
- **Which stages:** the input iron, the gain tube's grid level, the 5751, and
  the output stage (tube and iron). Not the compressor law or Calibration.
- **How:** every stage was calibrated at 18 dB, so each gets 18 − H dB of
  extra drive and is level-compensated.
- **The default drives everything 4 dB softer than before.** The level stays
  within 0.02 dB from 8 to 26 dB of Headroom, while distortion falls steadily.
- **Automation** (2026-10-24): Headroom ramps over 20 ms everywhere, like
  Makeup.
  - **Before:** it reached the gain tube and the 5751 through the gain
    element's settings. Every change crossfaded to a fresh element, one change
    at a time. Automation moved in 20 ms stages, rendered two elements while it
    moved, and reset the 5751's blocking at every step.
  - **Now:** it is a per-sample drive into the gain tube's grids and the 5751.
    The 5751's capacitor charge is kept per unit of that drive.
  - **Tested:** a 6 → 30 → 6 dB sweep has no sample step beyond what either end
    has when held. A 0.1 dB move at Makeup +24 changes the output by 0.8% of
    peak, against 14% when an element crossfade resets the blocking.

## CPU

[gain_tube_cpu.csv](measurements/gain_tube/gain_tube_cpu.csv): the whole plugin
at 48 kHz, stereo, defaults, every tube in, 0.3-peak 110 Hz tone.

- **34.9% of a core.** It was 20.6% with the single-ended follower.
- **The push-pull follower solves two tubes against the iron.** Its upper
  plate is an inner root inside the joint solve. Three refinements took it
  from 1317 to about 930 ns per internal sample: a linear prediction for the
  inner start, reusing its last evaluation, and a 10⁻¹¹ relative outer
  tolerance.
- The earlier CPU figures in [GainTube.md](GainTube.md) and
  [TubeStage.md](TubeStage.md) were measured before the push-pull stage.

### Spikes when pushed hard (owner report 2026-10-24)

"When it's pushed hard … the CPU usage shoots up and the loudest elements
cause an audible glitch." Not at normal levels.

**MEASUREMENT** (whole plugin, 48 kHz, stereo, 256-sample blocks, the drum
fixture at 0.9 peak, Makeup +24; thread CPU time per block against the block's
real-time length; Apple M2 Pro, native arm64 Release):

| | Median block | Worst block |
|---|---:|---:|
| Before | about 45% | **114%** (237% wall-clock) |
| After | 22% | **53–58%** |

- **Cause:** every loud hit drove the push-pull output stage into its hardest
  region: the saturated iron and an upper section near cut-off. Single-ended,
  or with the output tube out, the worst block was 22%.
  - The per-sample root then needed up to 43 evaluations, bisecting.
  - Each evaluation solved the upper plate as an inner root: about 2.3 more
    triode evaluations, at 35 ns each.
  - Over 100% means a block takes longer than real time: the dropout heard on
    the loudest hits.
- **Three changes** ([CathodeFollower.h](../source/modules/CathodeFollower.h),
  [TriodeModel.h](../source/modules/TriodeModel.h)):
  1. **Brent's method** replaces bisection in the bracketed fallback: at most
     18 evaluations, where bisection needed 43.
  2. **A tabulated 12BH7.** Koren's law separates into a plate factor and a
     grid factor. Each is a one-dimensional cubic Hermite table with exact
     slopes, about 650 kB in all, built once at prepare. Outside the tables it
     falls back to the exact law. It costs 11–15 ns where the law costs 35 ns,
     and matches it within 1.2 × 10⁻⁹ in current and 1.6 × 10⁻⁷ in slope. On
     the hot drums the output differs by 4 × 10⁻¹² of peak.
  3. **Joint Newton for push-pull.** The unknown and the upper plate are
     stepped together: one evaluation of each section per step, with no inner
     root. Hot transients take 5–10 steps. After 10, the solve falls back to
     the nested, bracketed one: 0.5% of samples on the hot drums.
- On the hot drums: 3.7 evaluations per sample on average, 32 at most, no
  failures. The same root and tolerances as before, so the sound is
  unchanged.

### Further savings (owner request, 2026-10-24)

Grid current made the joint Newton fall back four times as often, and the
worst block rose to 89–99%. Traces showed why:
- Near the upper section's cut-off its current is exponential and its slopes
  tiny, so a full Newton step overshot by up to 150 V into heavy conduction and
  cycled.
- Steps that carried the plate past B+ were clamped, which broke the descent.

**Changes** ([CathodeFollower.h](../source/modules/CathodeFollower.h)):
1. **Damped joint Newton.** A step that does not reduce the current mismatch
   is halved. Both residuals are amperes, so their squares add.
2. **Step limits.** The tube-side cathode moves at most 5 V per step, as SPICE
   limits junction steps. The plate stays within 90% of the way to B+ or to its
   cathode. Up to 16 steps.
3. **Plate tolerance** matched to the output's relative 10⁻¹¹, above the inner
   solve's absolute floor.
4. **The last Newton step is taken to first order** once the residual is
   within 10⁻⁶ of the term sizes. Newton's next residual would be about its
   square, so the confirming evaluation is skipped: about a third of the solve
   at normal levels. `strictSolve` keeps the confirmation for tests.
5. **A quadratic starting guess** from the last three solutions, instead of a
   linear one from two.

**MEASUREMENT** (same method as above):

| | Defaults, mean | Makeup +24 drums, median / worst block |
|---|---:|---:|
| After the first fix | 21.6% | 23% / 53% (Ideal grid), 89–99% (grid current) |
| Now, Ideal grid | **17.6%** | **18.5% / 38%** |
| Now, DC-coupled or Capacitor | — | **19–20% / 34–38%** |

- **Evaluations per sample:** 3.0 → 2.0 at normal levels; 3.7 → 2.45 on the
  hot drums. Fallbacks are under 0.02% of samples, with no failures.
- **The sound is the same:** against the exact law with every root
  confirmed, the output differs by at most −187 dB of peak on bass and drums,
  nominal to hot (tested to −160 dB).
- **Not changed:** the iron's magnetic element (play operators and one tanh)
  is now the next largest cost. It is shared by every transformer, so it is
  left alone.

### Voicing changes

A tube voicing change rebuilds the stage's design on the audio thread: the
operating point, inside the frame that applies it.
- **Before:** 60 rounds of a 100-step bisection for the push-pull's grid bias,
  so **475 µs in one frame**. That is 9% of a 256-sample block at 48 kHz, in a
  single sample, repeated with every slider step while dragging. Single-ended
  cost 57 µs.
- **Now:** guarded Newton solves (`risingRoot`, `gridForCurrent`), and the
  push-pull's outer fixed point stops once converged. The worst frame is
  **18 µs** push-pull and 16 µs single-ended.
- **Tested:** the designs meet their defining equations, within 10⁻⁹, across
  the workbench's current and supply range.

## Tests

[OverdriveTests.cpp](../tests/OverdriveTests.cpp):
- grid current against the sheet, and a smooth, monotone clamp;
- unity gain, blocking and recovery in the 5751 network;
- Release Mod;
- Headroom: default, ordering, level and distortion.

[OutputTubeTests.cpp](../tests/OutputTubeTests.cpp):
- the push-pull design point;
- push-pull at 3.0% THD against single-ended at 13.9%, both Amounts at 100%,
  100 Hz, −6 dBFS;
- the tabulated 12BH7 against Koren's law, and its exact fallback;
- hot drums through the push-pull stage with each Output grid choice: bounded
  evaluations per sample, fallbacks and worst sample (`[cpu]`);
- the fast solve against the exact law with every root confirmed;
- Tube Amount sweeps without a step, also with the capacitor-coupled grid.

[OverdriveTests.cpp](../tests/OverdriveTests.cpp), output grid and Headroom
automation:
- the grid clamp against the source law: exact, monotone and smooth;
- each Output grid choice transparent until overdriven, then conducting,
  grittier, and periodic once settled;
- the capacitor version's blocking and recovery;
- tube workbench version 2 saves loading with Output grid at Ideal;
- a Headroom sweep without a step, and a small move keeping the 5751's
  blocking.

Release-law tests were updated to the 20 ms floor. Tests of calibrated behavior
set Headroom to 18 dB.

## Open

- Owner listening: the Output grid choices against Ideal.
- Measured grid current for the 6386, 5751 and 12BH7, if found, and the real
  output grid network.
