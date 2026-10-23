# Tube stage: Manley-style output stage (12BH7, 5751)

**Status:** research, Tests only. Nothing here is in the live build yet.

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

## Open

- **Owner listening** on [coupling_audio/](measurements/tube_stage/coupling_audio/).
- **The size of the source-impedance effect.** It depends on C's low-level
  hysteresis, which was calibrated only from a ~0 Ω source (Woods). Real
  tube-driven VTB1148 data does not exist; the owner tunes by ear.
- **Stage values are a design choice:** B+, bias, cathode resistor,
  paralleled sections, coupling capacitor. Lower output impedance (more
  current, or a different follower) means less transformer distortion.
- **Not modeled:** grid current (no published curves), power-supply sag,
  heater-cathode leakage, and the 5751 voltage-amplifier stage before the
  follower. The 5751 is fitted, but not yet used in a stage.
- **Live integration.**
  - A joint tube + C solve inside `VtbTransformerCore` at 4×, behind a bypass.
  - A cheaper triode evaluation.
  - Aliasing measurement, with ADAA only if needed.

## Reproduce

```bash
cmake --build Builds --config Release --target Tests
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/tube_stage" LUMIERE_MODULE_TASK=tube-fit Builds/Release/Tests '[tube-report]'
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/tube_stage" LUMIERE_MODULE_TASK=tube-coupling Builds/Release/Tests '[tube-report]'
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/tube_stage" LUMIERE_MODULE_TASK=tube-coupling-cpu Builds/Release/Tests '[tube-report]'
```

The coupling task takes a few minutes. Verified 2026-10-23: the three Debug
plugin targets and Tests (Debug and Release) build with no warnings, and both
suites pass 124/124.
