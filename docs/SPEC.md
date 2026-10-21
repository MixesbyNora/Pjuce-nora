# Lumiere Specification

## Product vision and implementation scope

The owner's longer-term direction is recorded in
[Project Lumiere: product vision](ProjectLumiereVision.md), with the original
notes preserved alongside it. It describes two planned products on a shared
framework, independently selectable compression/coloration models, presentation
choices and Box Tone response curves. The tube-product vision specifies three
models plus Off in each of five independently combinable groups: tube saturation,
vari-mu compression circuit, input transformer, output transformer and PSU.
The proposed Easy/Expert workflow adds a Model selector with 3-6 curated
component-combination presets in Easy mode and individual component selection
in Expert mode. The analog GUI vision adds a front-faceplate/open-top view switch
for component selection, with disabled components disappearing from the unit;
the modern GUI uses component dropdowns. These visual choices share the same
component configuration. Those intentions are separate from the
implemented behavior below and do not activate deferred features or change the
current milestone, plugin identity, parameters, DSP or editor.

## Magnetic transformer (live and offline)

VST3, AU, Standalone and the measurement view share the voltage-driven
play/Preisach transformer. The former memoryless tanh/residual-ADAA audio stage
is removed; no model switch selects it. Historical measurements/recordings and
the old JSON preference remain archived. The tanh function inside the magnetic
constitutive law remains, as requested; its hot evaluation uses a double-precision
local polynomial table checked against the scalar function.

Signal flow is IN -> compressor (including makeup) -> magnetic transformer ->
OUT -> LFM -> Mix/bypass -> monitor. Detection, FB/FF, sidechain modes, release
and gain-reduction metering remain upstream. Output level meters still measure
the final monitored signal.

Host parameters retain their IDs, order, ranges and defaults:

- `xfrmOut`: bool, default false; true displays Out, false displays In.
- `xfrmAmount`: 0-100%, step 0.1, default 0; drive maps linearly in dB from
  -24 to +12 dB and smooths over 20 ms.

The normalized circuit is `B=H+k*M`, `D*x=H+tau*dB/dt`, `y=x-H/D`.
It retains magnetic history and a small-signal 5 Hz corner. It is a simplified,
uncalibrated circuit, not an identified VTB1148 or other named transformer.
Trial evaluations do not mutate history; a safeguarded implicit solve commits
once. Its scaled residual target remains 1e-13. A carried branch-slope predictor
reduces work; a 96-evaluation bounded fallback resolves the old cap failures.
Core tunables remain in `source/experimental/TransformerAudioSettings.h` and
`MagneticCoreSettings.h`, now shared with live targets.

The private LF pre-emphasis/inverse post-emphasis shelf pair has been removed.
The oversampled path is now input -> magnetic circuit -> output, with independent
per-channel magnetic history. This intentionally changes the sound of enabled
transformer processing, including recalled projects; the host Amount default and
all host parameter identities remain unchanged. No new stereo mode is added.

Fixed internal FIR oversampling is sample-rate aware:

| Host rate | Factor | Host-reported latency |
|---|---:|---:|
| 44.1 kHz | 16x | 144 samples |
| 48 kHz | 16x | 144 samples |
| 96 kHz | 8x | 137 samples |
| 192 kHz | 4x | 124 samples |

The FIR design targets -120 dB stopbands. Up/down filter lengths yield an exact
integer group delay without an added fractional-delay allpass. Latency remains
constant when Amount, Out, Mix, voicing or host bypass change. The untouched dry
and bypass signals receive the same pure integer delay, so Mix and Delta remain
aligned. The host is told the delay in prepareToPlay and a conservative two-second
magnetic tail. Bypass/zero Amount preserve original samples after this delay;
the magnetic solve is skipped and reset, while FIR histories stay warm for
click-free re-engagement. Component switching crossfades over 10 ms.

## Transformer voicing workbench and measurements

The collapsible Transformer Voicing panel is a permanent, model-agnostic tuning
workbench alongside the generic editor and meters. A/B, copy, paste, reset,
default audition, level matching, measurement capture and JSON/CSV exchange remain.
The owner tunes by ear and exports settings for code revisions. Future compression
and tube controls can use this framework; none are added in this phase.

A descriptor table in `source/TransformerVoicing.h` defines IDs, labels, units,
ranges, steps, defaults and skew. Only drive calibration is currently described:
-12 to +12 dB, step 0.1 dB, default 0. It remains private state. UI construction,
quantization, JSON, A/B slots and measurement metadata share this table. Packing
widths derive from its quantized ranges; both slots and flags still fit in one
lock-free atomic, with a compile-time capacity check. Audio reads once without
retrying. Voicing edits retain the existing fixed-bank 20 ms crossfade.

Version 3 exports identify `lumiere-voicing-workbench-v3`. Version 1 and 2 JSON
and project state migrate drive calibration, selection and audition flags. Removed
`emphasisDb` and `cornerHz` fields are ignored, even if malformed, with a visible
status notice. They never re-enable shelves. The notice survives save/restore.
Unknown future versions/models or incomplete current descriptor values are
rejected without replacing current private settings. Projects without private
state load defaults. The archived tanh favorite remains an immutable historical
record, not a promise to reproduce its former sound.

Measurements capture both slots on a worker, use the same current transformer
DSP as live audio, turn matching off, and retain progress, cancellation and stale
capture detection. Metadata and captured-control labels derive from descriptors;
removed shelf fields do not appear in new measurement settings.

Magnetization weight k, memory and the magnetic circuit corner are proposed future
descriptors, not implemented controls. They need approval and a decision about
preparing/warming different circuit states before fixed-bank crossfading. A
series commit stage is also deferred. No hardware-accuracy claim is made.

See [MagneticTransformerLive.md](MagneticTransformerLive.md) for before/after
measurements, numerical validation, CPU, latency and reproduction, and
[TransformerVoicing.md](TransformerVoicing.md) for the listening workflow.
Prior offline-only milestones remain historical in
[the audio prototype](TransformerAudioPrototype.md), [first CPU pass](TransformerAudioCpuOptimization.md)
and [magnetic artifact refinement](MagneticArtifactRefinement.md).

## FB/FF

`fbff` / **FB/FF** ranges from 0 to 100 in 0.1 steps, defaulting to 100
(full feedforward). Zero is full feedback and 50 is the equal blend. The generic
editor displays FB, 50/50, FF, or the percentage followed by `% FF`; those labels
and numeric values can be typed into the control. The feedback amount ramps
linearly over 20 ms when the parameter changes.

For each sample, with `b = 1 - fbff / 100` (smoothed):

```
levelDb = dB(peak of the filtered detector channels)
detectDb = max(-160, levelDb - b * previousAppliedReductionDb)
appliedReductionDb = envelope(gainComputer(detectDb))
```

The previous reduction is read before advancing the existing envelope. Detection
remains after input gain and before makeup. The single gain computer and envelope
preserve stereo linking and program-dependent release, and the meter still reads
the applied reduction. Full FF preserves the previous audio bit for bit. No second
compressor or output crossfade is used: blending inside one detector avoids two
independent envelope histories and keeps the feedback approximation independent
of the particular gain curve. The MXXX version used a literal blend of two
compressors; an A/B comparison against that version is pending.

## Sidechain Filter

`scFilter` / **SC Filter** is a stepped `AudioParameterChoice`, defaulting to
**Off**, with the following stable choice order. It shapes only the detector's
copy of each channel after input gain. The stereo detector takes the louder
absolute filtered sample. The audible signal is never passed through these
filters; only the resulting compression can change its gain.

| Index | Choice | Detector filters in series |
|---|---|---|
| 0 | Off | Exact bypass; unchanged detector signal |
| 1 | HP 80 Hz | Second-order Butterworth high-pass, 80 Hz, Q 0.7071; 12 dB/octave |
| 2 | HP 120 Hz | Second-order Butterworth high-pass, 120 Hz, Q 0.7071; 12 dB/octave |
| 3 | Tilt Wide | Low shelf: 250 Hz, -5.76 dB, Q 0.50; high shelf: 2041 Hz, +5.76 dB, Q 0.50 |
| 4 | Tilt Focused | Low shelf: 160 Hz, -5.76 dB, Q 0.68; high shelf: 3499 Hz, +5.76 dB, Q 0.68 |
| 5 | Band 730 Hz | Band-pass: 730 Hz, Q 0.15, constant 0 dB peak gain |
| 6 | Custom | Peak: 712 Hz, +5.76 dB, Q 0.13; peak: 15800 Hz, +5.36 dB, Q 1.57; low shelf: 88 Hz, -11.09 dB, Q 0.05 |

All mode names, filter types, frequencies, gains, and Q values are editable in
`source/dsp/SidechainFilterSettings.h`. The RBJ biquads use double-precision
coefficients and transposed direct form II state, independently for channel 0
and channel 1. Preparation computes every mode's coefficients at the current
sample rate. Switching modes is unsmoothed and resets both channels' filter
state; selecting the same mode preserves state. Storage is fixed for seven modes
and up to three stages, with no allocation or locking during processing.

The existing generic parameter controls and gain-reduction meter remain in use.
Program-dependent release, the gain computer, and audio gain staging are unchanged.

The future [USER sidechain EQ](ProjectLumiereVision.md#user-sidechain-eq) is
documented in the product vision: an additional USER choice opens a bottom EQ
graph with one HP band, one LP band and six peak/shelf bands. It retains all
existing choices, including the fixed Custom preset. USER and its editor are
not implemented; the current seven-choice parameter and DSP remain unchanged.

## BA bar

The existing compressor, detector filter, and program-dependent release run inside
the BA signal path. Parameter definitions are centralized in `source/BaParameters.h`.

| ID | Control | Range / choices | Default |
|---|---|---|---|
| `bypass` | Bypass (also the host bypass parameter) | Off / On | Off |
| `inGain` | IN | -24 to +24 dB, 0.1 dB steps | 0 dB |
| `outGain` | OUT | -24 to +24 dB, 0.1 dB steps | 0 dB |
| `ioLink` | IN/OUT Link | Off / On | Off |
| `lfm` | LFM | 0 to 250 Hz, 0.1 Hz steps; midpoint 70 Hz; below 20 Hz displays Off | Off |
| `mix` | Mix | 0 to 100%, 0.1% steps | 100% |
| `monitor` | Monitor | LR / M / S / D | LR |

Signal order is dry input copy -> IN gain -> existing compressor -> transformer -> OUT gain ->
LFM -> linear dry/wet mix -> dry bypass crossfade -> monitor. IN, OUT, and Mix
smooth over 20 ms. Bypass crossfades over 10 ms, keeps the entire wet chain
running, and reports zero gain reduction while enabled. Hosts using either the
bypass parameter or the bypassed processing callback receive this behavior.

LFM high-passes only the wet side signal `(L-R)/2` with a double-precision,
second-order Butterworth biquad, leaving `(L+R)/2` untouched. Cutoff changes and
on/off transitions smooth over 20 ms. Fully Off skips the M/S round-trip exactly;
mono layouts skip LFM. The sidechain and LFM share the same double-precision
transposed direct form II calculation without changing sidechain coefficients.

Monitor is applied last: LR passes the final signal; M sends its mid to both
channels; S sends its side to both channels; D sends the final mixed signal
minus the untouched dry input, after the bypass crossfade. Before bypass this
equals `Mix * (wet - dry)`: silent at 0%, half the full Delta at 50%, and
`wet - dry` at 100%. Dry is time-aligned by the reported transformer latency. Wet includes IN gain, compression, transformer, OUT gain, and LFM;
dry is captured before IN gain. Delta is silent when fully bypassed or when
the wet chain is neutral. Choices and bypass still crossfade over 10 ms.
In mono, M is the mono signal and S is silence.

IN/OUT Link compensates the other gain by the inverse change, retaining any
existing offset until a range limit is reached. Parameter listeners only record
deltas in atomics. A 60 Hz message-thread timer applies compensation with host
begin/end gestures and a re-entrancy guard. State restoration does not trigger
compensation. Audio processing only reads the resulting gain parameters.

Dry storage is allocated during preparation. Blocks larger than the prepared
capacity use non-owning mono/stereo views and are processed in bounded chunks,
without allocating, locking, or logging in the audio callback. The temporary
generic editor is enlarged to fit all controls beside the gain-reduction meter.

Oversampling and HR (headroom) remain deferred. Stereo CH MODE, DC Threshold,
Release Mod controls, and tube saturation remain out of scope. Input/output meters
are implemented below.

## Bottom bar meters

The IN and OUT stereo peak meters sit in a full-width bottom strip under the
generic controls and existing gain-reduction meter. IN measures each channel
after BA `inGain`, before the existing compressor chain. OUT measures the very
final samples after bypass and monitoring. Mono readings mirror L into R.

Four lock-free atomic floats retain the largest linear absolute peaks since
the UI's last read, including oversized blocks processed in chunks. Audio-side
publication uses compare-exchange maximum loops; the 30 Hz editor timer reads
each channel with `exchange(0.0f)`. Measurement does not modify audio samples,
allocate, lock, log, or invoke UI code from the audio thread.

Display ballistics are instant attack, 24 dB/second release, and a peak marker
held for 1.5 seconds before falling at 24 dB/second. The scale is -60 to +6 dB,
with ticks at -60, -48, -36, -24, -12, -6, 0, and +6. Bars are green below
-12 dB, yellow from -12 to 0 dB, and red above 0 dB. Silence displays at -60 dB.

`StereoLevelMeter` accepts only two dB values and settable channel labels,
defaulting to L/R, independently of any processor. `MeterBallistics` is a pure
display-state class without JUCE UI dependencies. M/S labels and display are
deferred until CH MODE is built. HR and a user-selectable oversampling control remain deferred; the magnetic transformer uses the fixed internal rate-aware FIR path described above.
