# Input transformer: VTB9046 / Neve 31267 line input

**Status:** calibrated, as a research calibration only. It is **not in the live
build yet**. That integration needs owner decisions on where the stage sits in
the chain and how the plugin presents it.

## Owner decisions (2026-10-23)

- **Line, not mic.** The plugin takes line-level audio, so it models the 1073
  line input: Carnhill VTB9046, the usual substitute for the Neve 31267. It does
  not model the VTB9045/10468 mic input.
- **Gather more data before fitting.** The owner supplied screenshots of the JLM
  Audio page, which sits behind a bot check, and photos of their own Golden Age
  Project Pre-73 Premier measurements.
- **Model the 1073 line wiring** (10k:600, primaries in series).
- **Calibrate as a family tuning** of variant C.

## Data

All of it is in
[vtb9046_reference.csv](measurements/input_transformer/vtb9046_reference.csv),
with source, conditions and a verified flag for each point.

- **Carnhill datasheet** (design guide p.10):
  - turns ratio 2+2:1+1;
  - DC resistance 175 / 175 / 56 / 56 Ω;
  - 10k:600 wiring gives −13 dB.
- **JLM Audio** (owner screenshots):
  - **Neve 31267: 0.5% THD at +7 dBm and 1% at +9 dBm, at 20 Hz**, "at the
    input to the transformer".
  - Measured in a real 1073 from a 50 or 200 Ω generator, single unit.
  - Wired as a 10468 equivalent (one winding each side). JLM says each 31267
    winding equals the 10468's.
  - High-frequency behavior, qualitative only:
    - windings in parallel (600:2k4): one large overshoot;
    - in series (2k4:10k): no overshoot, rounded;
    - one winding: matches the 10468.
- **The owner's Pre-73 Premier in line mode** (1 kHz at −18 dBFS, gain varied,
  not level-matched).
  - The line transformer saw the same level in every shot, so these photos
    characterize the gain and output stages. They don't characterize the input
    transformer.
  - The unit is clean at low gain, shows H2/H3 next, then a dense class-A
    clipping series near full scale.
  - That makes them useful for the future amplifier/tube stage.
- **No published data** exists for low-level distortion, the harmonic balance
  or the HF response of the VTB9046 or 31267.

## Calibration

- **Targets.** 0.5% and 1% THD at **+13 and +15 dBu**, 20 Hz, at the line input
  (0 dBFS = +18 dBu).
  - **This is an inference.** JLM's +7 / +9 dBm was measured with one winding.
    The 1073 line wiring puts both primaries in series, which halves the flux
    per input volt, so onset moves about +6 dB.
  - The owner chose this over using +7 / +9 directly. The workbench's
    Saturation onset control at about −6 dB reaches the measured single-winding
    point.
- **Method.** Variant C at the VTB1148 calibration, with saturation and knee
  scaled together (flux capacity only), golden-section search on the scale.
  Task: `input-vtb9046` in
  [ShuntCalibration.cpp](../tests/modules/ShuntCalibration.cpp).
- **Result** ([vtb9046_fit.csv](measurements/input_transformer/vtb9046_fit.csv)):
  - scale **0.7810** of VTB1148, i.e. saturation 0.03881 and knee 0.07762;
  - **0.5% at +13.15 dBu and 1% at +14.85 dBu**, both within 0.15 dB;
  - the inherited curve shape already gives JLM's roughly 2 dB spacing between
    the two points.
- **Recorded as** `vtb9046LineInputC()` in
  [TransformerModule.h](../source/modules/TransformerModule.h), pinned by a
  `[shunt-calibration]` test.
- **Inherited from VTB1148, not measured for this part:**
  - low-level hysteresis distortion (about 0.6% at −38 dBu, 20 Hz);
  - the even-harmonic balance (H2 about 20 dB under H3);
  - the distortion-vs-frequency shape;
  - drive mapping, circuit values and LF response.

  All four VTB1148 workbench controls apply, if it's tuned by ear.
- **20 Hz level sweep** ([vtb9046_level_sweep.csv](measurements/input_transformer/vtb9046_level_sweep.csv)):

  | Level | THD | H2 | H3 |
  |---:|---:|---:|---:|
  | 0 dBu | 0.15% | −68 dBc | −57 dBc |
  | +12 dBu | 0.34% | −74 dBc | −50 dBc |
  | +16 dBu | 1.56% | −70 dBc | −36 dBc |
  | +20 dBu | 2.8% | −74 dBc | −31 dBc |

- **Aliasing** ([vtb9046_aliasing.csv](measurements/input_transformer/vtb9046_aliasing.csv)):
  the same as VTB1148 to within about 0.6 dB at every factor. The 4/2/1
  schedule applies.
- **HF stage: not added.** JLM describes the series wiring as rounded and
  over-damped, with no overshoot, but gives no corner frequency. The AMS 1073
  module spec (−3 dB at 40 kHz) covers the whole module. There is nothing to
  fit a resonance to.

## Renders

[vtb9046_audio/](measurements/input_transformer/vtb9046_audio/): input and
VTB9046 at 0, +12 and +18 dB drive, on the same synthetic material and with the
same matching as the VTB1148 renders.

## Open

- **Live integration:**
  - where the input stage sits (before the compressor, as in a 1073);
  - whether it has its own Amount/drive and In/Out;
  - whether it gets its own workbench slot;
  - its latency, which adds to the output stage unless the two share an
    oversampling region.
- **Optional verification:** stepped 20 Hz sines into the Pre-73 line input at
  minimum gain, reading where THD rises. Note the GA unit's line transformer is
  not necessarily a 31267.
