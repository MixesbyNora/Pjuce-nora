# Tube stage: Manley-style output stage (12BH7, 5751)

**Status:** the joint 12BH7 + VTB1148 output stage is **in the live build**
(2026-10-23), with its own workbench and Output Tube switch. See "Live output
stage" below. The fits and the coupling test are the research behind it.

## Owner decisions (2026-10-23)

- **First tube stage:** the Manley-style output stage. The Pre-73 is not a
  reference. Sources are the Codex research packets
  ([ManleyVariableMu-ClaudeHandoff.md](references/ManleyVariableMu-ClaudeHandoff.md),
  [ManleyVariableMu-TubeData.md](references/ManleyVariableMu-TubeData.md)).
- **Fit the 12BH7 and 5751** to their datasheets, then run the hybrid plan's
  tube-to-transformer coupling test
  ([HybridArchitecture.md](HybridArchitecture.md#test-tube-to-transformer-coupling-proposal)).
- **Cathode follower, not common cathode.** The plan said common cathode. A
  12BH7 common-cathode stage has about 3.5 kΩ output impedance. Into the
  VTB1148's roughly 3 H primary, that loses bass below about 200 Hz. A cathode
  follower has about 120 Ω, and its LF corner is about 7 Hz.
- **Datasheets downloaded** to `docs/references`: the RCA 12BH7-A
  (Mar 1955, tentative data) and the GE GL-5751 (ETX-245A, Jun 1952), the
  sources the Codex packet cites.

## Step 1: triode fits

### Model

The plate current uses Koren's published triode equation (N. Koren, "Improved
vacuum tube models for SPICE simulations", *Glass Audio* 8(5), 1996, revised
2003):

```
E1 = Va/kp · ln(1 + exp(kp · (1/mu + Vgk / sqrt(kvb + Va²))))
Ia = E1^ex / kg1 · (1 + sgn E1)
```

- Code: [TriodeModel.h](../source/modules/TriodeModel.h). It returns Ia
  together with its partial derivatives, for Newton solves.
- No grid current. Neither sheet publishes positive-grid curves.

### Digitizing

The scans have no machine-readable data, so the curves were read off the
images ([TubeCurveData.h](../tests/modules/TubeCurveData.h)):

1. Each page was rendered at high resolution. The 12BH7-A plot is printed
   rotated, so it was turned upright first.
2. Grid lines were located by pixel density. That gives the axis scale:
   10 V and 1 mA per square for the 12BH7-A, 4 V and 0.1 mA for the 5751.
3. Each curve was read where it crosses horizontal rows halfway between grid
   lines, so the grid itself does not interfere.
4. Curve labels, text and the curve ends were dropped by hand.

The result is 178 points over 9 grid voltages for the 12BH7-A and 150 points
over 9 for the 5751. These are plot readings, not measurements.

### Fit

- **Error measure.** Points were read along current rows, so the reading error
  is horizontal. The fit therefore minimizes the RMS plate-voltage error at
  each digitized current.
- **Search.** A coarse grid over kp, ex and kvb, with kg1 set to meet the
  published typical current. Then Nelder–Mead from the best three starts.
- **Task:** `tube-fit` in [TubeStage.cpp](../tests/modules/TubeStage.cpp).
- **Recorded as** `rca12bh7a()` and `ge5751()` in
  [TriodeModel.h](../source/modules/TriodeModel.h), pinned by a
  `[tube-stage]` test.

**MEASUREMENT** ([tube_fit.csv](measurements/tube_stage/tube_fit.csv),
[tube_fit_points.csv](measurements/tube_stage/tube_fit_points.csv)):

| Tube | mu | ex | kg1 | kp | kvb | RMS error | Worst point | RMS current error |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 12BH7-A | 23.44 | 1.103 | 389.1 | 107.4 | 5310 | 4.3 V | 19 V (−40 V curve) | 6.9% |
| 5751 | 87.41 | 1.069 | 749.1 | 409.6 | 4508 | 2.0 V | 6.8 V | 5.8% |

The Koren `mu` parameter is not the published amplification factor. The
effective mu, gm and rp come from the model's derivatives at each operating
point.

**Against the published typical points** ([tube_fit_typical.csv](measurements/tube_stage/tube_fit_typical.csv)),
used for validation only:

| Point | Published Ia / mu / gm / rp | Model |
|---|---|---|
| 12BH7-A, 250 V, −10.5 V | 11.5 mA / 16.5 / 3.1 mS / 5.3 kΩ | 10.6 mA / 18.9 / 3.27 mS / 5.77 kΩ |
| 12BH7-A, 250 V, −14 V | 4 mA | 3.0 mA |
| 12BH7-A, 250 V, −23 V | about 0.05 mA | 0.058 mA |
| 5751, 250 V, −3 V | 1.0 mA / 70 / 1.2 mS / 58 kΩ | 1.02 mA / 68 / 1.26 mS / 54 kΩ |
| 5751, 100 V, −1 V | 0.8 mA / 70 / 1.2 mS / 58 kΩ | 0.89 mA / 79 / 1.73 mS / 45 kΩ |

Overlays of the model (blue) and the digitized points (red) on the scans:
[12bh7a_fit_overlay.png](measurements/tube_stage/12bh7a_fit_overlay.png),
[5751_fit_overlay.png](measurements/tube_stage/5751_fit_overlay.png).

**What the fit gets right and wrong**
- **5751:** follows every curve to within about 2 V. The exception is below
  about 30 V on the 0 V curve, where the printed curve bends
  (probably contact potential) and the model does not.
- **12BH7-A:** good on the 0, −5 and −10 V curves (2–3 V RMS). Those are where
  the cathode follower below runs. The printed −15 V curve is spaced unevenly
  against its neighbors, and the model misses it by about 9 V, which is why it
  is low at the −14 V published point.
- **Anchoring tried and dropped.** Weighting the published typical points
  into the fit moved the −10.5 V current only from 10.6 to 10.8 mA. The limit
  is the Koren equation's shape, not the fit.
- **The 5751's published small-signal values** are the same at both operating
  points (70 / 1.2 mS / 58 kΩ). They are nominal, and the curves disagree with
  them at 100 V.

## Step 2: tube-to-transformer coupling test

### Question

The hybrid plan asks whether a **linear impedance interface** is close enough
to a **fully coupled solve** when a tube stage drives the VTB1148 output
transformer.

### Fixture

[TubeCouplingCircuit.h](../tests/modules/TubeCouplingCircuit.h), Tests only.

```
B+ 300 V ── plate
grid = 145 V bias + signal          12BH7-A, both sections in parallel
cathode ── 7.5 kΩ ── ground
cathode ── 1000 µF ── VTB1148 primary: 16 Ω winding, then magnetizing branch ∥ load
```

- **Transformer:** the live VTB1148 C calibration (`vtb1148OutputC()`). That
  means the 34.6 kΩ referred load, 16 Ω primary winding, and the same level
  map (0 dBFS = +18 dBu at the primary).
- **Inferred, not published:** the stage values. Manley publishes no output
  stage schematic. One 12BH7 per channel with paralleled sections matches the
  two-bottle count for a stereo unit, but is an inference.
- **Large coupling capacitor**, so that the test measures the tube's
  resistance rather than the capacitor's reactance (8 Ω at 20 Hz).

**MEASUREMENT — operating point** ([coupling_operating_point.csv](measurements/tube_stage/coupling_operating_point.csv)):

| Cathode | Plate–cathode | Grid–cathode | Current | Dissipation | Output impedance | Primary L (C, large-signal) | LF corner |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 150.5 V | 149.5 V | −5.5 V | 20.1 mA (10 per section) | 1.5 W per section | 118 Ω | 3.0 H | 7.1 Hz |

All are within the 12BH7-A ratings: 20 mA cathode current, 3.5 W, and 200 V
heater–cathode. A `[tube-stage]` test pins this.

### Paths

Every path runs at 384 kHz (8× 48 kHz), so differences between them come from
coupling only. Each is one trapezoidal step per sample.

- **Reference.** The tube, capacitor and C's magnetizing law solved together.
  - Every node follows from the magnetizing current, so each sample is one
    monotone scalar root.
  - The solver uses Newton for at most 8 steps, then bisection, as
    `TransformerCircuit` does. This cost a re-run: a first attempt let Newton
    cycle on the hysteresis kinks and failed on 36 k samples.
- **(a) Linear interface.**
  - The tube drives a *linear* model of the primary (3 H, winding, load).
  - C is then fed the stage's EMF (primary voltage + 118 Ω × primary current)
    with `sourceOhms` = 118 Ω.
- **(b) Per block.** As (a), but the output resistance is re-read every
  2048 samples (256 at 48 kHz) from the tube's operating point. It uses a new
  `TransformerCircuit::setSourceOhms`, which keeps the flux history.
- **(c) Uncoupled.** The tube drives a resistor, and C sees the live
  calibration's ~0 Ω source. This is what putting a tube module in front of
  today's transformer would do.

### Sweep

**MEASUREMENT** ([coupling_sweep.csv](measurements/tube_stage/coupling_sweep.csv),
task `tube-coupling`):
- Sines at 20 / 50 / 100 / 1000 Hz.
- Grid level −10 to +30 dBu RMS.
- A 0.1 s fade-in and at least 0.3 s to settle (1 s at ≤ 100 Hz), then a 0.2 s
  whole-period analysis window.
- Zero solver failures; at most 52 iterations.

The table shows the **matched difference to the reference in dB** (lower is
closer), with the reference THD alongside.

| Frequency | Level | Reference THD | (a) linear | (b) per block | (c) uncoupled |
|---:|---:|---:|---:|---:|---:|
| 20 Hz | 0 dBu | 1.5% | −51 | −51 | −10 |
| 20 Hz | +12 dBu | 3.9% | −41 | −45 | −10 |
| 20 Hz | +16 dBu | 13% | −22 | −24 | −7 |
| 20 Hz | +20 dBu | 43% | −10 | −9 | −4 |
| 50 Hz | +16 dBu | 0.96% | −57 | −57 | −18 |
| 50 Hz | +20 dBu | 1.8% | −46 | −46 | −17 |
| 50 Hz | +24 dBu | 14% | −18 | −18 | −12 |
| 100 Hz | +24 dBu | 0.67% | −58 | −57 | −23 |
| 100 Hz | +28 dBu | 1.9% | −40 | −40 | −21 |
| 100 Hz | +30 dBu | 12% | −19 | −19 | −15 |
| 1 kHz | 0 dBu | 0.075% | −93 | −93 | −38 |
| 1 kHz | +30 dBu | 0.18% | −79 | −78 | −43 |

### Renders

[coupling_audio/](measurements/tube_stage/coupling_audio/),
[coupling_renders.csv](measurements/tube_stage/coupling_renders.csv).
- The same synthetic bass, drums and vocal material as earlier renders, at 0,
  +12 and +18 dBu RMS on the grid.
- Each set has the input and all four paths, written in full-scale units
  (peak 1.0 = +18 dBu) with no level matching.

| Material | Level | (a) | (b) | (c) |
|---|---:|---:|---:|---:|
| Bass | 0 / +12 / +18 dBu | −62 / −61 / −43 | −60 / −60 / −43 | −19 / −20 / −19 |
| Drums | 0 / +12 / +18 dBu | −66 / −61 / −41 | −63 / −59 / −41 | −20 / −21 / −21 |
| Vocal | 0 / +12 / +18 dBu | −77 / −74 / −73 | −77 / −74 / −73 | −36 / −38 / −39 |

### What it shows

1. **Coupling is not optional.**
   - The tube's 118 Ω source multiplies the transformer's distortion by about
     8–11× (+18 to +21 dB) at low and moderate levels, compared with today's
     ~0 Ω source plus a 16 Ω winding. For example, at 1 kHz, 0 dBu: 0.075% vs
     0.010%.
   - It also loses about 1 dB at 20 Hz at low level, where C's inductance is
     low.
   - Path (c) misses all of this: 10–43 dB off the reference everywhere.
2. **The linear interface (a) is right until the iron saturates.**
   - It holds to better than −40 dB up to about +12 dBu at 20 Hz, +20 dBu at
     50 Hz, +28 dBu at 100 Hz, and everywhere at 1 kHz.
   - Past that, the reference saturates much harder. At 20 Hz, +16 dBu: 13%
     THD vs 7.3%.
   - The cause is the cathode follower itself. The magnetizing current peaks
     swing its plate current from 1.3 to 45 mA (near cut-off), and it cuts off
     completely from +20 dBu. So its
     output resistance swings within each cycle, which a fixed or per-block
     value cannot follow.
3. **Per block (b) does not help.** It is slightly better at moderate bass
   levels (−45 vs −41 dB at 20 Hz, +12 dBu). In saturation it reads values up
   to 7.5 kΩ (the cathode resistor, at cut-off) and overshoots.
4. **Where it matters:** saturated bass, the region the plan expected. Mids
   and highs, and normal program levels, are fine on (a).
5. **Cost** ([coupling_cpu.csv](measurements/tube_stage/coupling_cpu.csv),
   task `tube-coupling-cpu`; unoptimized test code, 192 kHz internal, bass at
   +12 dBu, stereo at 48 kHz):

   | Path | ns per internal sample | % of one core |
   |---|---:|---:|
   | Reference (joint solve) | 925 | 35.5% |
   | (a) linear interface | 686 | 26.4% |
   | (b) per block | 710 | 27.3% |
   | (c) uncoupled | 377 | 14.5% |

   The joint solve is one scalar root per sample, like C alone. It costs
   about 35% more than (a), because (a) also solves the tube stage, just
   against a linear load. Most of the time is the triode's `exp`/`log`/`pow`
   inside Newton; that has not been optimized.

   *Superseded:* the live build's solver (below) brings the joint solve to
   367 ns, cheaper than (a). `coupling_cpu.csv` now holds those numbers. The
   sweep and renders were made with the earlier solver; both converge to the
   same roundoff tolerance.

### Decision gate (plan step 6)

**Recommendation:** use the joint solve for the tube → output-transformer
pair, as one stage, rather than the linear interface.
- It is exact by construction.
- It is already a single scalar root.
- It costs about 9 percentage points more than (a) before any optimization.

The linear interface stays the default for pairs with little coupling, such
as the input transformer driving a grid.

**Owner listening is still needed** (plan step 5): the reference against (a)
and (c) on the renders, especially bass and drums at +18 dBu.

## Live output stage (owner decisions, 2026-10-23)

The owner listened to the coupling renders and chose the reference: the joint
solve goes live, with voicing controls.

```
... compressor -> Makeup
  -> output stage:
       12BH7 cathode follower (Output Tube, Tube Amount)  ─┐ solved together, one
       VTB1148 C (Output XFRM, Xfrm Amount)               ─┘ scalar root per sample
  -> LF Mono -> Digital OUT -> Mix
```

**Switching**
- **Output Tube** (`outputTubeOut`): a new host switch, In by default,
  appended with version hint 6.
- What each combination does:
  - **Both in:** the joint solve.
  - **Tube out:** today's transformer, bit-exact (C with its ~0 Ω source).
  - **XFRM out:** the follower drives a resistor equal to the primary winding
    plus the referred load.
  - **Both out:** the stage is bypassed with the existing 10 ms ramp and
    stops working sample-exactly.
- **Amounts are drive only** (owner decision, 2026-10-23). Neither Amount
  bypasses; 0% is the minimum drive (−24 dB) and the In/Out switches are the
  only bypass. Both default to the calibrated 0 dB (57.1%, −0.02 dB on the
  0.1% grid). Previously Xfrm Amount defaulted to 0% and 0% bypassed, so a
  new instance now has the output stage in.
- **Tube Amount** (`tubeAmount`, version hint 7). The tube's own drive, mapped
  like Xfrm Amount (−24 to +18 dB) and level-compensated the same way.
  - **Xfrm Amount** sets the iron's level; **Tube Amount** sets the tube's.
  - In the joint solve this is an ideal level shift between them. The tube
    sees Tube/Xfrm times the iron's signal, cathode swing and magnetizing
    current, while the iron still sees the follower's own output resistance.
  - So Tube Amount changes only how hard the tube works: its own clipping,
    and how soon it cuts off on the iron's bass current. The tube's grip on
    the iron stays with Plate current.
  - At equal Amounts the circuit is the plain, physical one.
- **Crossfades.** Switching between combinations, and every tube voicing
  change, uses the same warmed-bank crossfade as transformer voicing. New
  circuit values start from fresh state, and the new bank runs alongside the
  old one before the crossfade.
- **Level.** Joint and tube-only output is divided by the follower's
  quiescent midband gain (about 0.94), so 1 kHz level is unchanged when the
  tube is switched in or out (within 0.1 dB, tested). The bass and saturation
  differences are the point and are left alone.

**Tube workbench** (Output tube, the third stage next to Input xfmr and
Output xfmr). It has its own A/B, copy/paste, Default and Match level, and is
saved as `TUBE_VOICING`. Sessions without it restore the defaults.

| Control | Range | Default | What it does physically |
|---|---|---|---|
| Plate current | 3–20 mA per section | 10 mA | Sets the follower's output resistance (≈ 1/gm). Less current: the tube leans harder on the iron, more bass saturation, earlier cut-off on bass peaks. |
| Coupling cap | 22–1000 µF | 220 µF (was 1000 µF until 2026-10-24) | Small values resonate with the primary inductance (an LF bump or tilt) and limit bass current into the core. |
| Supply (B+) | 200–400 V | 300 V | The cathode sits at half of it, so it sets the swing headroom before the tube runs out of plate voltage. |
| Sections | 1 or 2 | 2 | One section doubles the output resistance (single-ended wiring only). |
| Wiring | Push-pull / Single-ended | Push-pull | Added 2026-10-24 ([Overdrive.md](Overdrive.md)): a White cathode follower that pushes and pulls current, or the original single-ended follower. |

- **Design rule** (`followerDesign`). The cathode is set to half the supply;
  the cathode resistor gives the chosen current; the grid bias comes from the
  fitted triode law. The defaults give 150 V at the cathode, 7.5 kΩ and
  −5.5 V grid–cathode, which is the coupling-test stage.
- **Ratings are not enforced.** Some combinations exceed them, for example
  400 V at 20 mA is 4 W per section against a 3.5 W rating. These are voicing
  choices, not a hardware spec.

**Generalized workbench code**
- [Voicing.h](../source/Voicing.h): any schema.
- [VoicingState.h](../source/VoicingState.h): A/B state in one atomic 64-bit
  word.
- [VoicingPanel.h](../source/VoicingPanel.h): the panel.
- The transformer workbench is unchanged in encoding and behavior.
- The tube schema is [TubeVoicing.h](../source/TubeVoicing.h).

**Solver** ([CathodeFollower.h](../source/modules/CathodeFollower.h), moved
out of Tests and shared with the coupling fixture):
- **Starting guess.** Each sample starts from a linear extrapolation of the
  last two solutions: 3.7 iterations on average instead of 7.7.
- **Shared exponential.** One `exp` serves both the triode's softplus and its
  sigmoid.
- **Safe under `-Ofast`.** Bracket ends are flags, never infinities, and the
  finite check is bit-level, because Release builds assume finite math.
- **On a failed solve** the stage holds its last output instead of dropping
  to 0. None failed in any measurement.

**MEASUREMENT — aliasing** ([live_aliasing.csv](measurements/tube_stage/live_aliasing.csv),
task `tube-aliasing`):
- Method as the transformer's Step 3d: tones from 55 Hz to 19 kHz and the
  two-tone case at −6 dBFS, steep oversampling, against a 64× render.
- Worst band difference to 64×, dB:

| Factor at 48 kHz | Extra drive | Tube + transformer | Transformer alone |
|---|---:|---:|---:|
| 4× (live) | 0 / +12 / +18 dB | −90 / −94 / −94 | −103 / −106 / −106 |
| 2× | 0 / +12 / +18 dB | −77 / −82 / −82 | −95 / −99 / −99 |
| 1× | 0 / +12 / +18 dB | −63 / −61 / −55 | −82 / −87 / −87 |

- **Verdict.** The tube adds aliasing, but at the live internal rate (about
  192 kHz at every host rate) it stays 90 dB or more below the signal.
  4/2/1 stays, and ADAA is not needed.
- **Not aliasing.** At 55 Hz with +18 dB drive there is non-harmonic content
  at −38 dBc at every factor. When the follower cuts off on bass peaks, the
  coupling capacitor charges and the bias drifts slowly, so the output is not
  periodic in the window. That is real tube behavior (a blocking-style bias
  shift), not an artifact.

**MEASUREMENT — whole-plugin CPU** ([live_cpu.csv](measurements/tube_stage/live_cpu.csv),
task `chain-cpu-tube`). Median % of one core, stereo, Release, threshold
−24 dB, 60/73 Hz test:

| Rate | Amount nominal, tube out | Tube in | Amount 100%, tube out | Tube in |
|---:|---:|---:|---:|---:|
| 44.1 kHz | 12.6% | 22.1% | 14.4% | 25.8% |
| 48 kHz | 12.6% | 22.7% | 14.7% | 26.9% |
| 96 kHz | 12.8% | 22.3% | 14.1% | 26.5% |
| 192 kHz | 12.8% | 21.9% | 13.9% | 26.1% |

The first unoptimized version cost 39–50% with the tube in.

**Tests** ([OutputTubeTests.cpp](../tests/OutputTubeTests.cpp), `[tube]`):
- default design point;
- level kept at 1 kHz with the tube in, out, and alone;
- bass distortion raised by the tube;
- every control reaching the audio;
- extremes finite and converging at +18 dB drive;
- switching and voicing changes without a step (core and plugin);
- parameter order and version hint;
- state round-trip and old-session defaults;
- the editor's third stage.

## 5751 amplifier (owner decisions, 2026-10-23)

The 5751 is the Manley-style voltage amplifier between the gain tubes and the
12BH7 follower. It completes the core chain: input transformer → gain tubes →
**5751** → 12BH7 → output transformer.

- **Makeup drives it.** Makeup is applied before the amplifier, so more Makeup
  is more level into the 5751. The output is normalized to unity small-signal
  gain, so the level still follows Makeup.
- **Amp Tube** (`ampTubeOut`): In/Out, In by default, version hint 9, appended
  last. Out is the clean gain.
- **Headroom will include it** and the gain tube (owner decision), once
  Headroom is built.

**Circuit** ([AmpTube.h](../source/dsp/AmpTube.h)): a textbook
resistance-coupled common-cathode stage with the fitted GE 5751.
- 300 V supply, 100 kΩ plate resistor, 1 mA. That puts the plate at 200 V and
  needs a 2.29 kΩ cathode resistor (bypassed).
- It drives the next grid's 1 MΩ leak resistor through a coupling capacitor.
- Both capacitors are treated as ideal at audio rates, so the stage is
  memoryless: plate voltage against grid signal is solved once (KCL, bisection)
  and tabulated in 1 mV steps for cubic Hermite interpolation.
- Gain 33.2 dB, inverting; the polarity is kept in the output.
- **Levels follow the real chain:** the grid sees the signal divided by the
  stage gain, so at 0 dBFS the plate swings the follower's line level
  (+18 dBu peak).
- **Placement:** inside the compressor's gain element, after Makeup. Switching
  uses the same 20 ms crossfade.
- **Not modeled:** grid current, and blocking at the next grid. This
  circuit's actual Manley values and any feedback around it are unpublished.

**MEASUREMENT** ([amp_tube_thd.csv](measurements/gain_tube/amp_tube_thd.csv),
task `gain-tube-report`):

| Level at the amplifier | THD | H2 | H3 |
|---:|---:|---:|---:|
| −10 dBu | 0.045% | −67 dBc | −140 dBc |
| +4 dBu | 0.23% | −53 dBc | −112 dBc |
| +18 dBu (0 dBFS) | 1.1% | −39 dBc | −84 dBc |
| +30 dBu (Makeup pushed) | 4.5% | −27 dBc | −59 dBc |

- **Almost pure H2,** the single-ended triode signature. It rises about 1:1
  with level.
- **More than Manley's published "<0.1%" at nominal level.** The real unit
  probably uses feedback (the manual describes the gain-control chain as a
  feedback circuit). Kept as the plain textbook stage for the owner to judge
  by ear; feedback or an unbypassed cathode are possible voicing options later.
- **Aliasing** ([amp_tube_aliasing.csv](measurements/gain_tube/amp_tube_aliasing.csv)):
  −107.5 dB against 64× at 4×, even at +6 dBFS.
- **CPU** ([gain_tube_cpu.csv](measurements/gain_tube/gain_tube_cpu.csv)):
  +0.27%. The whole plugin uses 20.6% at 48 kHz with every tube in.

**Tests** (`[amp-tube]` in [GainTubeTests.cpp](../tests/GainTubeTests.cpp)):
- gain and cathode resistor;
- unity small-signal gain with polarity kept;
- H2 dominant and rising with level;
- Makeup drives it harder;
- Amp Tube is appended and switches smoothly.

Older tests that isolate the input iron now set the amplifier Out too. A meter
test allows 0.25 dB because the H2 skews the output peak.

## Open

- **The size of the source-impedance effect.** It depends on C's low-level
  hysteresis, which was calibrated only from a ~0 Ω source (Woods). Real
  tube-driven VTB1148 data does not exist; the owner tunes by ear.
- **Stage values are a design choice:** B+, bias, cathode resistor,
  paralleled sections, coupling capacitor. Lower output impedance (more
  current, or a different follower) means less transformer distortion.
- **Not modeled:** grid current (no published curves), power-supply sag
  (planned last), and heater-cathode leakage.
- **Owner listening** on the live build: the four tube controls, and Output
  Tube in and out.
- **CPU.** The tube adds about 10 points. A table-based triode would cut it
  further if needed.
- **A tube measurement tab** for the workbench (THD vs level, like the
  transformers'), not built.

## Reproduce

```bash
cmake --build Builds --config Release --target Tests
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/tube_stage" LUMIERE_MODULE_TASK=tube-fit Builds/Release/Tests '[tube-report]'
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/tube_stage" LUMIERE_MODULE_TASK=tube-coupling Builds/Release/Tests '[tube-report]'
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/tube_stage" LUMIERE_MODULE_TASK=tube-coupling-cpu Builds/Release/Tests '[tube-report]'
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/tube_stage" LUMIERE_MODULE_TASK=tube-aliasing Builds/Release/Tests '[tube-report]'
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/tube_stage" LUMIERE_MODULE_TASK=chain-cpu-tube Builds/Release/Tests '[shunt-report]'
```

The coupling task takes a few minutes. Verified 2026-10-23, after the live
output stage: the three Debug plugin targets and Tests (Debug and Release)
build with no warnings, and both suites pass 134/134 (after Tube Amount
and the removal of the Amount bypass).
