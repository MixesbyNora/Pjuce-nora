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
- **Not modeled yet:** grid current at the output follower's upper grid.

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

## Headroom

`headroom`: 6–30 dB, default **14**, version hint 11.
- H dB means 0 dBu = −H dBFS (0 dBFS = +H dBu).
- **Which stages:** the input iron, the gain tube's grid level, the 5751, and
  the output stage (tube and iron). Not the compressor law or Calibration.
- **How:** every stage was calibrated at 18 dB, so each gets 18 − H dB of
  extra drive and is level-compensated.
- **The default drives everything 4 dB softer than before.** The level stays
  within 0.02 dB from 8 to 26 dB of Headroom, while distortion falls steadily.
- **Automation:** Headroom changes the gain element's settings, which
  crossfade over 20 ms, one change at a time.

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
- **Still costly:** the push-pull stage remains the largest cost, at 192 kHz
  inside the shared 4× region.

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
- hot drums through the push-pull stage: bounded evaluations per sample,
  fallbacks and worst sample, and the exact-law output (`[cpu]`).

Release-law tests were updated to the 20 ms floor. Tests of calibrated behavior
set Headroom to 18 dB.

## Open

- Owner listening at the new defaults.
- Grid current at the output follower's upper grid.
- Measured grid current for the 6386, 5751 and 12BH7, if found.
- Headroom automation without stepwise crossfades.
