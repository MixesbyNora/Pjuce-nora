# Detector: the analog 12AL5 sidechain

**Status:** live (2026-10-24), the default.

## Owner decisions (2026-10-24)

- **Build the analog detector,** the vari-mu's last base part.
- **12AL5 is the default.** The previous law stays as a choice, **Digital law**.
- **No ratio knob:** ratio and knee emerge. A workbench **Sidechain gain**
  sets how steeply it compresses. Its default is matched to the Digital law's
  4:1 near the default threshold.
- **Release Mod becomes a real dual time constant,** not the digital speed-up
  toward 20 ms.
- **FB/FF now defaults to FB,** and the blend is kept (decided after the
  measurements below).
- Downloads approved: the Tung-Sol 12AL5 sheet (Mar 1953) and the Manley Variable
  Mu manual (Oct 2023). Both are in `docs/references`.

## Sources

- **Manley manual** ([Manley-VariableMu-Manual-2023-10.pdf](references/Manley-VariableMu-Manual-2023-10.pdf)):
  - The gain-control chain is a feedback circuit (p10). The 5670 pair is
    biased by the sidechain (p15).
  - Gain-reduction curves (p13) show a "soft round knee": about 1.5:1 at 2 dB of
    GR, 2:1 at 5 dB and 5:1 at 10 dB in Compress, toward 8:1 when driven hard.
    Limit goes on to 12:1. A lower Threshold softens the knee.
  - The manual publishes no schematic: no timing network, no sidechain
    amplifier and no control-voltage range.
- **12AL5 sheet** ([TungSol-12AL5-1953.pdf](references/TungSol-12AL5-1953.pdf)):
  - A forward-current curve (p2, 0–9 V, to 52 mA).
  - Detector curves: DC developed against rectified µA for 2–30 V RMS.

## The 12AL5 law

[TriodeModel.h](../source/modules/TriodeModel.h) `diodeCurrent`, `tungSol12al5`:

```
I = P (softplus(s V) / s)^n      P = 2.75 mA, n = 1.346, s = 8.21 / V
```

- **Above about a volt:** space-charge conduction, fitted to 13 digitized
  forward points (0.75–8.25 V) within **0.47% RMS**.
- **Below it:** the cathode's initial-velocity current, exponential with slope
  n·s = q/kT at an oxide cathode's 1050 K. A detector therefore develops a small
  negative bias with no signal: **0.47–0.57 V** into 0.5–2 MΩ. The sheet's
  detector curves show about 0.6 V.
- **The detector curves are not fitted.** They imply about 20× less current
  than the forward curve, so they include a test circuit the sheet does not
  give.

## The circuit (inferred)

[AnalogDetector.h](../source/dsp/AnalogDetector.h):

```
|sidechain| - delay --- Ra --- 12AL5 --+-- Cf || Rf --+-- Cs || Rs --- ground
                                       control voltage = Vf + Vs
```

| Part | Control | Value |
|---|---|---|
| Sidechain amplifier | Calibration (extra gain); workbench **Sidechain gain** | 40 V peak per full-scale unit at 0 dB |
| Delay bias on the diodes | **Threshold** | the sidechain amplifier's output at the threshold level |
| Rectifier | — | 12AL5, full-wave: both channels' diodes charge one network (the stereo link) |
| Ra | **Attack** | Ra·C = 5 × Attack, so Attack is the 63% time of a 10 dB step over threshold |
| Cf, Cs in series, C = 1 µF | **Release Mod** m | Cf = C/m, Cs = C/(1 − m); m ≤ 0.95 |
| Rf, Rs | **Release** | Rs·Cs = Release; Rf·Cf = Release/10, at least 20 ms |
| Control voltage | — | biases the gain tubes below their quiescent grid; GR = −20 log(gm(bias)/gm(quiescent)) |

- **The control voltage drives the tube directly.** GR follows the selected
  tube's own gm curve (5670 or 6386), so ratio and knee come from the tube, the
  diodes and the loop. With the gain tube Out, the digital gain uses the same
  GR.
- **FB/FF** blends the sidechain signal: FF is the input, FB the input at the
  previous sample's gain, which is the gain stage's output before Makeup.
- **The solve, per sample at the internal rate:**
  - Each conducting diode solves u + Ra·I(u) = w, Newton inside a bracket, from
    a tabulated law.
  - The network is backward Euler, with the diodes linearized about the control
    voltage.
- **Switching** Digital ↔ 12AL5 carries the present gain reduction across:
  tested, with no step above 0.05 dB.

## MEASUREMENT

Tests in [DetectorTests.cpp](../tests/DetectorTests.cpp); compressor alone at
192 kHz, 1 kHz tones, threshold −18 dBFS, 5670.

**Feedforward runs away; feedback is the vari-mu.**
- With the detector on the input, the control voltage grows with signal
  amplitude while the 5670's gain falls ever faster toward cut-off. At 0 dB
  sidechain gain it uses up its roughly 6 V control range about 8 dB over
  threshold: 42 dB of GR there, and the 60 dB floor beyond. That is real
  feedforward vari-mu behaviour.
- In feedback the loop limits itself, which is why the Manley and the Fairchild
  670 are feedback designs.
- **Hence the FB default.** FF can still be blended in, and over-compresses.

**Static curve, FB, 0 dB sidechain gain:**

| Input | 12AL5 GR | Digital law GR (old FF default) |
|---|---:|---:|
| −15 dBFS | 2.01 dB | 1.97 dB |
| −12 dBFS | 4.02 dB | 4.04 dB |
| −9 dBFS | 6.22 dB | 6.13 dB |

- **The ratio then rises with GR,** as on the Manley's curves: about 3:1 near
  2 dB of GR, and 6.7:1 at 18 dB.
- **At −12 dB sidechain gain:** 1.5:1 at 1.5 dB of GR and 2.0:1 at 3.8 dB, as
  in Manley's Compress mode. It is gentler than their 5:1 at 10 dB.
- **The 6386 behaves almost the same in feedback.** A high-gain loop sets the
  output level, not the tube's curve.

**Attack** (63% of the settled GR):

| Attack | 3 dB step | 10 dB step | 20 dB step |
|---|---:|---:|---:|
| 1 ms | 3.2 ms | 1.2 ms | 0.36 ms |
| 5 ms | 12.7 ms | 4.8 ms | 2.2 ms |
| 30 ms | 52 ms | 26 ms | 11 ms |

A peak detector catches big hits faster and small ones slower. The Digital law
uses the setting for every step.

**Release** (37% of the starting GR, Release 200 ms):

| Release Mod | after a 30 ms hit | after 2 s of compression |
|---|---:|---:|
| 0% | 162 ms | 162 ms |
| 50% (default) | 99 ms | 143 ms |
| 100% | 19 ms | 30 ms |

- At 0% it is one RC. GR falls faster than the control voltage because GR is
  not linear in it: 162 ms at 200 ms.
- With Release at 2 s and Mod at 50%: 0.5 s after a 30 ms hit, 1.4 s after 2 s
  of compression.

**Low-frequency distortion** (−6 dBFS, threshold −24 dBFS, FB):

| | 50 Hz | 100 Hz | 1 kHz |
|---|---:|---:|---:|
| 12AL5, defaults (11 dB GR) | 2.8% | 1.4% | 0.14% |
| Digital, defaults (7 dB GR) | 0.95% | 0.48% | 0.05% |
| 12AL5, fastest (0.2 ms, 50 ms, Mod 100%) | 11.9% | 7.7% | 0.94% |
| Digital, fastest | 3.1% | 1.9% | 0.22% |

The control voltage ripples at twice the signal frequency. GR in dB is steeper
than linear in the control voltage, so the ripple distorts more than the
Digital law's dB envelope does. Fast vari-mu settings do thicken bass.

**CPU:** the whole plugin with compression on goes from 17.7% to 19.2% of a
core (48 kHz stereo, M2 Pro, Release).

## Parameters and workbench

- **Detector workbench,** the fifth stage, saved as `DETECTOR_VOICING`:
  - **Detector:** 12AL5 | Digital law. Default 12AL5.
  - **Sidechain gain:** ±12 dB in 0.1 dB steps, default 0. 12AL5 only.
- **Sessions saved before the detector** restore Digital law. Their FB/FF was
  set for it and defaulted to FF, where the 12AL5 runs away.
- **FB/FF default is 0 (FB),** previously 100.
- The compressor-law tests pin feedforward, the law they describe. So does the
  golden reference render.

## Open

- **Owner listening:** 12AL5 against Digital, Sidechain gain, Release Mod's dual
  release, and FF blends.
- **Inferred values** to revisit if a schematic or a measured unit turns up:
  the 40 V sidechain scale, 1 µF, the attack scale, Rf·Cf = Release/10, and the
  control voltage applied 1:1 to the grids.
- **Not modeled:** the sidechain amplifier's own limits (supply clipping), the
  12AL5's capacitances, the control path's filtering into the grids, and
  Manley's Limit/Compress switch. Sidechain gain covers that range instead.
