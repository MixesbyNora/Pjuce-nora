# Gain tube: the vari-mu gain element, and Calibration

**Status:** in the live build (2026-10-23).

## Owner decisions (2026-10-23)

- **Build the gain-control tube and the Calibration control.**
- **Both tubes, selectable:**
  - GE **5670**: the standard Manley tube, and the default.
  - GE **6386**: the original Manley tube, a true remote-cutoff type also used
    in the Fairchild 670.
  - Sources are the Codex packet's
    [ManleyVariableMu-TubeData.md](references/ManleyVariableMu-TubeData.md) and
    the original GE sheets, downloaded to `docs/references`
    (`GE-5670-1956.pdf`, `GE-6386-1954.pdf`).
- **The tube is the gain element.**
  - Today's compressor law and controls stay: threshold, the 4:1 soft knee,
    attack, release, FB/FF and the sidechain filter.
  - Each gain-reduction value sets the tube pair's grid bias, and the pair
    makes the gain change.
  - So the distortion follows the real tube curve as GR deepens. The fully
    physical alternative, where ratio and knee emerge from the tube, was not
    chosen.
- **Calibration** (±24 dB): the level into the detector only. The audio level
  at the tube is unchanged.

## Tube data and fits

**Digitized** ([TubeCurveData.h](../tests/modules/TubeCurveData.h)):
- **5670:** the plate family on p5 (148 points, grid 0 to −10 V) and the
  transfer family (Eb 100–300 V).
- **6386:** the transfer family on p4, each section (Eb 100–300 V), with its
  remote-cutoff tail to −55 V. The p3 and p4 upper charts are cascode
  connections and were not used, as the Codex packet warns.
- **Method:** as for the output stage, with the transfer curves read along
  grid-voltage columns.

**Models** ([TriodeModel.h](../source/modules/TriodeModel.h); task
`gain-tube-fit`, [gain_tube_fit.csv](measurements/gain_tube/gain_tube_fit.csv),
[gain_tube_anchors.csv](measurements/gain_tube/gain_tube_anchors.csv)):
- **5670:** the Koren triode, fitted to its plate family. It matches the
  transfer curves to within 9.4% RMS (relative, floored at 0.25 mA).
- **6386:** a single Koren triode cannot follow its long tail. A remote-cutoff
  tube's variable-pitch grid behaves like sections in parallel with different
  cutoffs, so it is modeled as **two Koren sections**:
  - The sections share ex, kp and kvb; each has its own mu and kg1.
  - Fitted to the transfer family plus the sheet's anchors: 6.7% RMS.
  - The sharp section sits at the fit's mu bound of 60. A third section
    improved the fit only to 5.2% RMS.

| Anchor (published) | 5670 model | 6386 model |
|---|---|---|
| Class A1 point: 5670 150 V, −1.97 V, 8.2 mA, gm 5.5 mS, mu 35 | 7.9 mA, 5.1 mS, mu 33 | — |
| Class A1 point: 6386 100 V, −1.92 V, 9.6 mA, gm 4.0 mS, mu 17 | — | 9.67 mA, 3.98 mS, mu 17.5 |
| 6386 gm = 100 µS at about −16 V (100 V) | — | 100 µS |

## The gain cell

[GainTube.h](../source/dsp/GainTube.h):

- **A balanced pair at a fixed plate voltage.** Each tube runs at its sheet's
  class A1 point: 5670 at 150 V, 6386 at 100 V.
- **Gain reduction sets the bias.** The bias is chosen so that
  gm(bias) / gm(idle) = 10^(−GR/20).
- **Output:** the difference of the two plate currents for grid swings of
  ±(grid level × signal), normalized so the small-signal gain is exactly the
  requested GR (tested within 0.02 dB up to 40 dB). Makeup follows.
- **Distortion** therefore comes from the tube's own curvature at that bias.
  The matched pair cancels even harmonics.
- **Grid level** (default 1 V peak per full-scale unit, so 0 dBFS = 1 V on
  each grid) sets how hard the signal works the tube. Level is unchanged.
- **Pair imbalance** scales one section's current. The pair is trimmed at
  idle, so imbalance gives H2, plus thump when the bias moves, as in hardware.
- **Implementation.**
  - Tables of Ia and gm against grid voltage (5 mV steps, cubic Hermite) and
    of bias against GR (0.05 dB steps).
  - Built once when the compressor is prepared, never on the audio thread.
  - Per sample, two table look-ups per channel.
- **Switching.** Off, the compressor's original digital multiply runs
  unchanged (bit-exact, tested). Switching in or out, or changing tube or
  voicing, crossfades over 20 ms.
- **The detector** sees the signal before the tube, as before, so gain
  reduction and the meter are unchanged.

**MEASUREMENT** ([gain_tube_thd.csv](measurements/gain_tube/gain_tube_thd.csv),
task `gain-tube-report`; the cell is memoryless, so this holds at any
frequency). THD at the default grid level:

| Level at the cell | GR | 5670 | 6386 |
|---|---:|---:|---:|
| +4 dBu | 0 dB | 0.002% | 0.020% |
| +4 dBu | 10 dB | 0.070% | 0.043% |
| +4 dBu | 20 dB | 0.15% | 0.042% |
| +4 dBu | 30 dB | 0.21% | 0.002% |
| +18 dBu (0 dBFS) | 0 dB | 0.06% | 0.48% |
| +18 dBu | 10 dB | 1.6% | 1.0% |
| +18 dBu | 20 dB | 3.4% | 1.0% |

- **Both are under Manley's published "<0.1% at 1 kHz"** at nominal level
  without GR. That spec does not state the conditions.
- **5670: distortion climbs steadily with GR,** the sharper character.
- **6386: flatter.** At 30 dB of GR it becomes very clean, because the
  remote section alone carries the gain there and is nearly linear at these
  swings. That comes from the two-section fit; judge it by ear.
- **Large signals expand slightly.** The curve is convex, so at hot levels
  the pair's gain rises a little above the requested gain: +0.95 dB for the
  5670 at +18 dBu and 20 dB GR. It is under 0.1 dB at nominal level.

**MEASUREMENT — aliasing** ([gain_tube_aliasing.csv](measurements/gain_tube/gain_tube_aliasing.csv);
−6 dBFS tones from 1 kHz to 19 kHz; worst band difference to a 64× render):

| Factor | 5670, GR 0 / 10 / 20 dB | 6386, GR 0 / 10 / 20 dB |
|---|---|---|
| 4× (live) | −108 / −107 / −107 dB | −108 / −107 / −107 dB |
| 2× | −106 / −100 / −106 dB | −106 / −106 / −106 dB |
| 1× | −78 / −47 / −41 dB | −58 / −52 / −52 dB |

The compressor already runs inside the shared 4/2/1 region, so nothing
changes there.

**MEASUREMENT — CPU** ([gain_tube_cpu.csv](measurements/gain_tube/gain_tube_cpu.csv)):
the whole plugin at 48 kHz, stereo, defaults with the threshold at −30 dB, goes
from 19.9% with the gain tube out to 20.4% with either tube in.

## Calibration

- **What it is.** A dB offset added to the detector's level, after the
  sidechain filter and before the gain computer and FB/FF blend. It is not
  the gain at the tube.
- **Effect.** +6 dB gives the same steady gain reduction as a 6 dB hotter
  input (tested within 0.1 dB). The audio is untouched when below threshold
  (tested bit-exact).
- **Typical uses.** Hot sources: Calibration down with Analog Input up, so the
  tube works harder at the same GR. Quiet sources: Calibration up.

## Parameters and workbench

- **Calibration** (`calibration`): −24 to +24 dB, 0.1 dB steps, default 0,
  version hint 8.
- **Gain Tube** (`gainTubeOut`): In/Out, In by default, version hint 8. Both
  are appended last.
- **Gain tube workbench:** the fourth stage, next to Input xfmr, Output xfmr
  and Output tube.
  - Controls: **Tube** (5670 / 6386, a slider labeled by name), **Grid level**
    (±18 dB) and **Pair imbalance** (0–20%).
  - Saved as `GAIN_TUBE_VOICING`.
  - No Match level, because the gain is exact by design.
- **Generalized workbench:** voicing descriptors can name their steps
  (`Descriptor::choices`), and panels can hide Match level.

**Tests:** [GainTubeTests.cpp](../tests/GainTubeTests.cpp) (`[gain-tube]`):
- exact small-signal gain for both tubes up to 40 dB;
- distortion rising with GR, even harmonics cancelled when matched, and H2
  from imbalance;
- grid level;
- Off bit-exact, and On with the same GR and level;
- Calibration equivalence, silence below threshold, and the plugin's detector;
- crossfades without a step;
- parameters, state and the editor's fourth stage.

## Overdrive additions (2026-10-24)

See [Overdrive.md](Overdrive.md).
- **Grid current:** the gain-tube grids are driven through 4.7 kΩ (inferred)
  and draw current (the 5670 sheet's law) when driven positive.
- **Release:** the compressor's release is floored at 20 ms, with Release Mod.
- **Headroom** offsets the gain tube's grid level and the 5751's drive.

## Changes to know about

- **The gain tube is In by default,** so every instance, including older
  sessions, now has tube color in the compressor. Gain Tube Out returns the
  exact previous sound.
- **One older test** (Input XFRM isolation) now sets the gain tube Out,
  because the tube's −90 dBc H3 would otherwise mask what it measures.

## Open

- **Owner listening:** 5670 against 6386, grid level, imbalance and thump, and
  Calibration in use.
- **Not modeled:**
  - the pair's plate load and plate-voltage swing (the plate is held fixed);
  - grid current;
  - the control-voltage path's own filtering;
  - the T-Bar 6BA6 (no triode-connected curves published).
- **The fully physical alternative,** with ratio and knee emerging from the
  tube and a feedback detector, is now built: the 12AL5 detector, the default
  since 2026-10-24 ([Detector.md](Detector.md)). This law remains as Digital law.
- **6386 above 30 dB of GR** follows the fit's remote section; there is no
  data that deep beyond the −16 V / 100 µS anchor.

## Reproduce

```bash
cmake --build Builds --config Release --target Tests
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/gain_tube" LUMIERE_MODULE_TASK=gain-tube-fit Builds/Release/Tests '[tube-report]'
LUMIERE_MODULE_REPORT_DIR="$PWD/docs/measurements/gain_tube" LUMIERE_MODULE_TASK=gain-tube-report Builds/Release/Tests '[tube-report]'
```
