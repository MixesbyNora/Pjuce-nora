# Lower-latency oversampling, linear phase only

**Status:** adopted. **Relaxed 4× at −100 dB** is live as of 2026-10-23, with
per-rate widths and re-measured at every rate (see "Live design" below).

## Owner brief (2026-10-23)

Get closer to competitors' latency without any high-frequency phase shift:
- UAD API 2500 native: 86 samples;
- Kazrog True Iron: 38 samples;
- Mixland Vac Attack: 38 samples at 4×.

Test option 1 (shorter linear-phase FIRs) and option 4 (fewer stages).

## Designs

All use JUCE half-band equiripple FIRs, so all are linear phase. Each keeps an
**integer round-trip latency**: the harness narrows the down filters slightly
until the delay is a whole sample. Dry, Mix and Delta therefore stay
sample-exact.

- **Current.** Passband edge about 22 kHz, −120 dB stopband.
- **Relaxed.** Passband edge 20 kHz at 48 kHz (first-stage transition width
  1/12). The stopband starts at 28 kHz, so anything that folds back on
  decimation lands between 20 and 24 kHz, above the audio band. The second
  stage only needs to pass 0–28 kHz, so its filter can be much shorter.

## Results

[oversampling_options.csv](measurements/oversampling_options/oversampling_options.csv),
48 kHz, Release. Task: `oversampling-options` in
[OversamplingOptions.cpp](../tests/modules/OversamplingOptions.cpp).

- **Signal:** both transformers (VTB9046 in, VTB1148 out) at +18 dB drive.
- **Aliasing:** worst in-band non-harmonic energy over 1 k / 7 k / 13 k / 19 kHz
  sines.
- **Two-tone:** 55 Hz saturating the core plus a 13 kHz probe, compared with a
  64× reference, DC–20 kHz.
- **CPU:** filters plus both transformers, stereo; the compressor is excluded.

| Design | Latency | 20 kHz | 21 kHz | 22 kHz | Pre-ring length (> −100 dB) | Aliasing | Two-tone vs 64× | CPU |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **Current 4×, −120 dB** | **124** (2.6 ms) | −0.00002 dB | 0.00 | 0.00 | 58 samples | −117.6 dBc | −115.8 dB | 14.1% |
| Relaxed 4×, −100 dB | **48** (1.0 ms) | −0.0002 dB | −0.07 | −0.8 | 25 | −117.6 dBc | −98.2 dB | 12.4% |
| Relaxed 4×, −90 dB | **43** | −0.0007 dB | −0.10 | −1.0 | 24 | −117.6 dBc | −87.7 dB | 12.4% |
| Relaxed 4×, −80 dB | **38** | −0.002 dB | −0.16 | −1.2 | 22 | −117.6 dBc | −77.9 dB | 12.3% |
| Relaxed 2×, −100 dB | **39** | −0.0002 dB | −0.07 | −0.8 | 25 | −105.2 dBc | −98.0 dB | 6.9% |
| Relaxed 2×, −90 dB | **35** | −0.0007 dB | −0.10 | −1.0 | 24 | −105.2 dBc | −88.1 dB | 6.8% |
| 1× | **0** | 0 | 0 | 0 | none | −94.6 dBc | −93.8 dB | 3.4% |

### What the numbers mean

- **Phase:** every design is linear phase. There is no phase shift at any
  frequency.
- **Flat band:** every relaxed design is flat to 20 kHz within 0.002 dB.
  They roll off only above 20 kHz: −0.1 dB at 21 kHz and about −1 dB at
  22 kHz. The current design is flat to 22 kHz.
- **Aliasing:** in-band aliasing is unchanged at 4× (−117.6 dBc) and stays
  very low at 2× (−105 dBc). The relaxed filters let images fold back, but
  only into 20–24 kHz.
- **Two-tone error:** tracks the stopband setting (about 2 dB under it). The
  probe's upsampling image leaks into the transformers at the stopband level
  and folds back onto itself. So −80 dB designs reach about −78 dB from the
  reference, and −100 dB designs about −98 dB.
- **Pre-ringing:** shorter filters mean shorter pre-ringing, 22–25 samples
  instead of 58. The largest pre-echo is slightly higher (about −31 dB vs
  −38 dB relative to the main peak), but it lasts less than half as long.
- **1× (no oversampling):** zero latency, and still −94.6 dBc aliasing at the
  current calibration. It leaves the least margin for Headroom and future tube
  stages.

## Renders

[oversampling_audio/](measurements/oversampling_options/oversampling_audio/):
bass, drums and vocal-like material at −18 dBFS RMS through both transformers,
at +0 and +12 dB output drive, for each design.

## Recommendation

**Relaxed 4×, −100 dB: 48 samples (1.0 ms).**
- About 2.6× lower latency than now, at the same 4× rate.
- Same in-band aliasing.
- Error against the reference stays near −98 dB.
- Pre-ringing less than half as long.
- Margin for Headroom and tube stages.

**To match 38 samples:**
- Relaxed 4× at −80 dB gets there, with a −78 dB worst-case two-tone error.
- Relaxed 2× at −100 dB gets 39 samples with −98 dB, but only −105 dBc
  aliasing and less margin for hotter stages.

**Before adopting a design:**
- The widths here are for 48 kHz; 44.1 kHz needs its own (narrower) widths to
  keep 20 kHz flat.
- The chosen design must be re-measured at 44.1 / 88.2 / 96 / 176.4 / 192 kHz.

## Live design (owner decision, 2026-10-23)

**Adopted:** relaxed linear-phase half-bands, −100 dB stopband, flat to 20 kHz
at every rate.
- Recorded per rate in `MagneticAudio::relaxedDesigns`
  ([MagneticOversampling.h](../source/dsp/MagneticOversampling.h)) and applied
  by `prepareLiveFilters` in both the live chain and the standalone stage.
- Down widths were narrowed slightly for whole-sample latency (task
  `oversampling-design`).
- Non-standard rates fall back to the original steep design.

**MEASUREMENT** ([live_design_by_rate.csv](measurements/oversampling_options/live_design_by_rate.csv),
task `oversampling-live`, exact live filters plus both transformers at +18 dB drive):

| Rate | Factor | Latency (was) | 20 Hz–20 kHz max deviation | Pre-ring | Aliasing | Two-tone vs reference | Plugin CPU (nominal) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 44.1 kHz | 4× | **78** (124) | 0.0002 dB | 45 samples | −113.2 dBc | −110.4 dB | 11.7% |
| 48 kHz | 4× | **48** (124) | 0.0001 dB | 25 | −117.6 dBc | −98.2 dB | 12.1% |
| 88.2 kHz | 2× | **11** (99) | 0.005 dB | 6 | −113.2 dBc | −112.6 dB | 10.9% |
| 96 kHz | 2× | **11** (99) | 0.004 dB | 6 | −117.6 dBc | −113.0 dB | 11.6% |
| 176.4 kHz | 1× | 0 (0) | 0 | none | −113.2 dBc | −112.4 dB | 10.8% |
| 192 kHz | 1× | 0 (0) | 0 | none | −117.6 dBc | −113.0 dB | 11.9% |

- **Latency and time:** 78 samples at 44.1 kHz is 1.77 ms; 48 samples at
  48 kHz is 1.0 ms.
- **44.1 kHz is longer** than 48 kHz because 20 kHz sits close to its 22.05 kHz
  Nyquist. A 19.5 kHz passband would give 66 samples and 19 kHz would give 56.
  Not adopted; the owner's rule is flat to 20 kHz.
- **Reference:** the two-tone reference is a steep design at about 3 MHz
  internal (64× at 44.1/48 kHz, 32× at 88.2/96 kHz, 16× at 176.4/192 kHz).
- **Mix and Delta:** latency is a whole sample at every rate, so they stay
  sample-exact.
