# Transformer voicing workflow

The collapsible panel is a permanent tuning workbench. Its controls, A/B slots,
JSON import/export and measurement metadata use one descriptor table. It is not
tied to a particular saturation model. Compression and tube controls may use it
later; they are not introduced here.

The current live/offline transformer is the voltage-driven magnetic circuit,
without the former LF emphasis shelf pair. **This intentionally changes sound**,
including saved projects with the transformer enabled. No legacy shelf or tanh
engine is selectable. Amount zero and Xfrm Out remain exact delayed bypass.

## Listening and feedback

1. Open **Transformer Voicing** beside the generic editor. Hiding the panel changes
   no settings. Enable the stage being tuned; drive calibration currently needs
   Xfrm Amount above zero and Xfrm Out unchecked.
2. Change one control at a time. A/B switches stored slots; **B > A / A > B** copies
   the selected slot. **Default voicing** temporarily auditions descriptor defaults,
   preserving both editable slots. **Reset selected** resets that slot and turns
   matching off. Default now means the current shelf-free engine.
3. Optionally use **Match level (audition)** on a repeating passage and allow it to
   settle. It matches linked output RMS to input using a 250 ms average, 100 ms gain
   smoothing and at most +/-12 dB compensation. This is not perceptual loudness
   matching; disable it for numerical measurements.
4. **Copy settings** exports both slots, selection, audition flags and descriptor
   values, plus the processor parameter/sample-rate context. Send this JSON with
   the source passage, input level and listening notes to guide later code tuning.
5. **Paste settings** recalls private settings without changing host parameters.
   Project state saves the same schema alongside APVTS.

| Current private descriptor | Range | Default | Meaning |
|---|---|---|---|
| `driveCalibrationDb` / Drive calibration | -12 to +12 dB, step 0.1 dB | 0 dB | Offset to Amount's internal drive mapping |

Only this descriptor is active. Units, range, step, default and skew all come from
`source/TransformerVoicing.h`; new sliders are built from that table. Private
settings are not host automation parameters. DSP ramp, bypass and audition timing
constants remain in `source/dsp/TransformerSettings.h`.

## State, migration and realtime behavior

Exports identify version 3 and `lumiere-voicing-workbench-v3`. Version 1 tanh and
version 2 magnetic files are accepted. Drive calibration, A/B selection, reference
and level-match flags migrate. Removed `emphasisDb` and `cornerHz` values are
ignored rather than validated or applied. Panel status clearly says that legacy
LF emphasis and shelf frequency were ignored because the shelves were removed.
That notice is included when migrated settings are saved/exported.

Unknown future schemas/models and incomplete current values are rejected without
replacing private settings. Projects with no workbench data reset to defaults;
projects with legacy data migrate without error. A future private child is ignored
while preserving current private settings; host parameter state still restores.
The archived [tanh favorite](voicings/tanh-favorite-v1.json) is unchanged: its B
calibration migrates, but its shelf values and former tone do not.

Descriptor quantization determines the packing widths. Both slots and flags fit
in a single lock-free atomic word; a compile-time guard prevents overflow if a
later schema grows. Audio reads are bounded, with no seqlock spin. Fixed magnetic
banks crossfade over 20 ms, coalescing rapid edits to the latest requested values.
Amount smoothing, Out fade and dry/Mix/Delta latency alignment are unchanged.
There are no rendering allocations, locks, logging, files, JSON or UI operations.

## Measurements

Open **Measurements** and press **Measure A/B**. The worker captures stored slots
and runs separate current transformer instances. Internal tones never enter the
DAW path. Cancel keeps the last completed report; closing the editor cancels and
joins the worker. Live audio, meters, settings and processor state are independent.

The 48 kHz grid uses 20/50/60/200/1000/5000/8000 Hz, -60/-24/-12/-6 dBFS and
Amount 25/50/100%, with 250 ms warmup and one-second Fourier analysis. Matching is
off and the transformer is In. Compressor, BA gains, LFM, Mix and monitor are
excluded. The current stage supplies its own fixed internal oversampling.

Tone, Level and Amount selectors browse captured results. Gain and RMS are dB;
H2/H3/H5/H7 are relative to the fundamental (dBc). The display floor is -140 dBc;
harmonics at/above Nyquist are NA. Captured-control labels and CSV settings come
from descriptors, with no shelf fields. Edits mark old captures stale. Copy CSV
retains the full grid and exported context when closing the editor.

For a selected-voicing command-line report (use Release for offline speed):

```bash
LUMIERE_VOICING_REPORT=/private/tmp/transformer.csv Builds/Release/Tests '[voicing-report]' --reporter compact
```

Set `LUMIERE_VOICING_SETTINGS` to an exported JSON file to select its current or
migrated voicing. Historical tanh reports remain historical; new measurements use
only the current shelf-free magnetic DSP. See [the live report](MagneticTransformerLive.md)
for retained before/after tone grids, CPU, clamp counts and numerical checks.

## Proposed controls, awaiting approval

The next possible descriptors are magnetization weight k, memory and **magnetic
circuit** corner Hz (not the deleted shelf frequency). Their current fixed values
are k=4, memory=0.35 and 5 Hz. Do not add them merely by
extending the UI table: their setters must prepare a differently configured
inactive circuit. A decision is needed on whether/how to warm that bank with
recent input/history before its 20 ms crossfade. No such controls, new preparation
policy or series commit stage are implemented in this phase.

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
