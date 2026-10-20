# Transformer voicing workflow

This development panel tunes the existing symmetric tanh/residual-ADAA stage.
The unchanged original sound is the reference. A Preisach magnetic core, tube
stage, additional harmonic-shape controls and oversampling are later work.

## First listening session

1. Open **Transformer Voicing** at the right of the generic editor. The panel
   can be hidden without changing the sound. The editor remains non-resizable.
2. Set **Xfrm Amount** above 0% and leave **Xfrm Out** unchecked. Keep compressor
   gain reduction at zero for the initial transformer comparison. The BA Bypass
   switch bypasses the full wet path, so leave it off while evaluating saturation.
3. A and B begin at the original values. Edit B first, then switch between A/B.
   **B > A / A > B** copies the selected voicing to the other slot.
4. **Original transformer** temporarily auditions the fixed original voicing,
   preserving both editable slots. **Reset selected** restores the selected slot
   to the original and turns level matching off.
5. Optionally enable **Match level (audition)** and allow about one second to
   settle on a repeating passage. It matches the transformer output RMS to its
   input, linked across channels. This changes audition gain, so turn it off for
   exact-reference or harmonic measurements. Bypass and zero amount remain exact.
6. Use the same bass, drums, vocal and full-mix excerpts at the same input level.
   Change one property at a time. **Copy settings** copies versioned JSON with
   both slots, their selection, audition flags, all current plugin parameter
   values and sample rate. Include that JSON and the source passage/feedback.
7. **Paste settings** restores the private voicing slots and audition flags. It
   does not change the other plugin parameters included as feedback context.

| Voicing control | Range | Original | Effect |
|---|---|---|---|
| Drive calibration | -12 to +12 dB, 0.1 dB steps | 0 dB | Offset to the existing Amount/drive mapping |
| LF emphasis | 0 to +24 dB, 0.1 dB steps | +12 dB | Low-shelf excitation ahead of the nonlinearity |
| Shelf frequency | 30 to 500 Hz, 1 Hz steps | 120 Hz | Corner of both inverse shelves; Q stays 0.5 |

These controls are private development state, not new host automation parameters.
Projects save both slots, selection and flags in a versioned child alongside APVTS
state. Older projects load the original voicing with matching off. Future builds
can hide the panel while retaining those values. The JSON identifies the model;
incompatible models/versions and incomplete data are rejected without replacing
the current settings. Numeric inputs are bounded to the supported ranges.

## Processing and comparisons

Changing voicing crossfades two fixed-size complete transformer filter/state banks
over 20 ms. The previous bank keeps running through the transition; fast edits
are coalesced to the latest requested voicing for the next transition. Amount's
20 ms smoothing and Out's 10 ms fade retain their original behavior.

The inverse de-emphasis is recomputed algebraically from the new pre-emphasis.
All coefficient creation uses fixed arrays, never reference-counted IIR allocation.
Private state reaches the audio thread in one lock-free packed atomic snapshot;
processing never accesses UI, JSON, files, locks or dynamic storage.

Optional level matching uses a 250 ms power average, 100 ms gain smoothing and
at most +/-12 dB compensation. It is off by default. It operates only on the
transformer output before OUT gain, LFM, Mix, bypass and monitor, with no effect
on detector input or compressor gain reduction. It is an audition aid, not a
replacement for the final desired gain staging. All tunable bounds and times are
in `source/dsp/TransformerSettings.h`.

## Repeatable measurement report

### In-plugin measurement view

Open **Measurements** in the collapsible Transformer Voicing panel, then press
**Measure A/B**. A low-priority background worker captures both stored slots and
measures a separate transformer instance. Its internal tones never enter the DAW
signal path. The audio callback, meters, host parameters and saved voicing remain
unchanged. Closing the editor cancels and joins the worker; **Cancel** stops a
capture without publishing a partial report.

Measurements use the same grid and transformer implementation as the offline
report below: 48 kHz, 250 ms startup, one-second analysis, seven frequencies,
four input levels and Amount 25/50/100%. This fixed sample rate is independent
of the DAW sample rate. Both slots are measured with the transformer In and
audition matching off, regardless of the live Out, Amount, Original or matching
controls. Compressor, sidechain, BA gains, LFM, Mix and monitor are excluded.

The **Tone**, **Level** and **Amount** selectors browse the completed capture;
they do not change live parameters. Blue is A and amber is B. The upper plot
shows fundamental gain versus input level at the selected frequency/Amount.
The lower plot and numeric table show H2/H3/H5/H7 at the selected input level,
plus fundamental and RMS gain. Harmonics are relative to the fundamental (dBc).
The display floor is -140 dBc; lower readings appear as `< -140`. Frequencies
at or above Nyquist are `NA`, including H3 at an 8 kHz fundamental. Aliasing
can still affect in-band readings; this view adds no oversampling.

Voicing changes during a capture do not change that capture. The status marks
completed results stale when either slot differs from its captured settings.
**Copy CSV** copies all 168 A/B points, exact measured voicings and the versioned
plugin-context snapshot. Live plugin parameters in that context are reference
information; only the two transformer voicings drive the measurements. Cancel
keeps any earlier completed report available. Results are temporary editor state;
copy the CSV to retain them when closing the editor.

### Saved favorite

[tanh-favorite-v1.json](voicings/tanh-favorite-v1.json) preserves the listening
favorite supplied on 2026-10-20: B has -6.1 dB drive calibration, +6.1 dB LF
emphasis, and a 103 Hz corner; A is the original. The supplied host-parameter
context is retained in the file. Paste its JSON with **Paste settings** to recall
the voicings; other host parameters are not imported. Factory defaults remain
unchanged. [The favorite A/B report](measurements/transformer_tanh_v1_favorite_ab.csv)
compares those settings using the new measurement engine.

### Command-line report

Build the requested plugin targets and Tests with the usual Xcode workflow.
The report is an offline Catch2 utility, not an audio callback or Benchmarks job:

```bash
LUMIERE_VOICING_REPORT=/private/tmp/transformer.csv ./Builds/Debug/Tests '[voicing-report]' --reporter compact
```

To measure a copied voicing, save its JSON to a file and additionally set
`LUMIERE_VOICING_SETTINGS` to that file's absolute path. The exporter measures
the selected voicing (or Original), always with audition matching disabled.

The checked-in `measurements/transformer_tanh_v1_baseline.csv` records the original
voicing at 48 kHz: 20/50/60/200/1000/5000/8000 Hz; -60/-24/-12/-6 dBFS; Amount
25/50/100%. It reports fundamental gain, RMS gain, and H2/H3/H5/H7 relative to
the fundamental. Each measurement discards 250 ms startup and analyzes one
second. Harmonics at/above Nyquist are `NA`, not measured as aliased harmonics.
The first two comment lines identify the model/settings and data provenance.

This file measures Lumiere's original software model; it is not Carnhill hardware
data or evidence that the current model matches a VTB1148.

## Published VTB1148 starting references

Source: Louis Woods, *The Measurement of Audio Transformer Colouration* (2019),
[University of Huddersfield thesis](https://eprints.hud.ac.uk/id/eprint/35000/1/FINAL%20THESIS%20-%20Louis%20Woods.pdf).
Page numbers below are printed thesis pages. The documented setup used series
windings (1:1.7), an active test driver and a 100 kOhm analyzer input; these are
conditions of that experiment, not assumed universal transformer behavior.

| Published observation | Location | How to use it |
|---|---|---|
| Less than 0.5 dB LF loss at 20 Hz, 0 dB test-circuit gain; no obvious change at +6 dB | pp. 82, 84 / Fig. 28 | Approximate linear-response constraint for a later measured model |
| Approximately -10 degrees phase at 20 Hz at 0/+6 dB test-circuit gain | pp. 82, 85 / Fig. 29 | Approximate phase constraint under the same conditions |
| Reported THD+N around 0.08-0.09% at 20 Hz, +4 dBu input in the summary comparison | p. 89 | Trend/reference only; verify each gain condition before numerical fitting |
| Harmonic content includes odd and even components, decreasing with frequency | pp. 82-89 / Figs. 30-32 | Qualitative target; the current symmetric curve intentionally has no independent even-harmonic control |
| High-gain LF tests encountered test-driver failure | pp. 81-82, 138-140 | Exclude affected points from transformer calibration |

The thesis provides further spectra and distortion plots in Appendix D,
pp. 135-140. Its approximate hysteresis figure on p. 77 belongs to LL1582,
not VTB1148. No VTB1148 Preisach distribution is claimed from these data.
The observations above are reference constraints, not digitized raw datasets.
Do not directly equate the thesis's analog dBu levels to the report's dBFS
levels; a defined electrical calibration is still needed for hardware fitting.

Next iteration: select and retain listening excerpts, voice within this panel,
then introduce magnetic-memory behavior behind the same comparison interface.
