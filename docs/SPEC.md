# Lumiere Specification

## Offline magnetic artifact refinement

A separate Tests-only five-point quartic turning-point experiment targets the
close 19/20 kHz pair and band-limited impulse state offset. It compares original
16x point sampling, quadratic 8x/16x, and quartic 8x/16x/32x with independently
converged references across the prior 108-stimulus grid, including calibration,
tail checks, full-band diagnostics and Release CPU profiles. The extra two
internal samples of lookahead are included in offline alignment only. No live
DSP, model constants, parameters, voicing, UI, meters or latency change. See
[MagneticArtifactRefinement.md](MagneticArtifactRefinement.md).

## Offline multitone and transient validation

The Tests-only validation compares the unchanged 8x turning-point experiment and
original 16x core with analytic-input high-rate references. It checks multitone
intermodulation/off-grid artifacts, transient residuals and peak error, retained
magnetization and silent-tail drift across four sample rates and three field
scales. It adds no live DSP, host controls, UI or latency. See
[MagneticMultitoneTransientValidation.md](MagneticMultitoneTransientValidation.md).

## Offline magnetic antialiasing follow-up

The Tests-only Release comparison retains the original 16x point-sampled
reference and evaluates interval integration and quadratic turning-point
reconstruction. It records CPU, alias metrics, gain/phase, residuals and filter
response without changing any live DSP, voicing, parameters, editor or latency.
See [MagneticAntialiasingFollowup.md](MagneticAntialiasingFollowup.md).

## Offline magnetic oversampling comparison

The Tests-only harness compares 1x/2x/4x/8x/16x processing of the unchanged
experimental core using double-precision linear-phase FIR interpolation and
decimation. A 48-stimulus grid uses 32x/64x reference convergence checks and
records aliases, aligned residuals, fundamental gain/phase, filter response,
latency and local CPU cost. It adds no live DSP, host parameters, UI controls
or plugin latency. See [MagneticOversamplingComparison.md](MagneticOversamplingComparison.md).

## Offline magnetic-core experiment (step 1)

An experimental generalized Preisach/play core is linked exclusively into Tests.
It maps normalized H to bounded M using 16 continuous play states, with fixed
storage/work and offline checks for remanence, return-point memory, stability,
sample-rate consistency and spectral folding. All tuning is in
`source/experimental/MagneticCoreSettings.h`. It is not connected to the live
audio path, UI or host parameters. The saved tanh favorite remains unchanged.
The prototype is uncalibrated and requires further antialiasing/circuit work
before live use. See [MagneticCorePrototype.md](MagneticCorePrototype.md).

## Transformer development voicing

The generic controls and meters remain in place, with a collapsible right-hand
Transformer Voicing panel. Private controls adjust drive calibration (-12 to
+12 dB), LF emphasis (0 to +24 dB), and shelf frequency (30 to 500 Hz). Their
defaults preserve the original 0 dB calibration, +12 dB shelf and 120 Hz corner.
Voicing uses a 20 ms crossfade between fixed-size filter/state banks.

A/B slots, copy-to-other, fixed Original audition, reset, and versioned JSON
copy/paste are available. Both slots and audition state are saved with the
project separately from the unchanged 16 host parameters. Optional RMS level
matching defaults off and acts only on the transformer output; it skips exact
Out/zero-Amount bypass. Old project states restore the original voicing.

See [TransformerVoicing.md](TransformerVoicing.md) for the listening workflow,
report command, published VTB1148 reference constraints and their limitations.
The first panel tunes the existing tanh model. A Preisach core and further
harmonic/memory controls remain deferred.

### Transformer measurement view

The collapsible panel's Measurements tab compares captured A/B transformer
voicings using internal offline tones on a low-priority worker. It shows
fundamental gain versus input level, H2/H3/H5/H7 in dBc, RMS gain and a full
CSV copy. The grid is fixed at 48 kHz with seven frequencies, four input levels
and Amount 25/50/100%, discarding 250 ms startup and analyzing one second.
Matching is off and the transformer is In for measurement. The rest of the
plugin chain is excluded; live audio and parameter state are unchanged.
Results are marked stale after edits; cancellation discards partial results.
The display floor is -140 dBc, and harmonics at/above Nyquist are NA.
No oversampling or magnetic model is added. The chosen tanh voicing is stored
as `docs/voicings/tanh-favorite-v1.json`, without changing factory defaults.

## Transformer saturation (basic)

The wet path now runs IN -> compressor (including makeup) -> transformer ->
OUT -> LFM -> Mix -> bypass -> monitor. The compressor detector and input meter
remain upstream of the transformer; the output meter still measures the final
monitored signal. L/R have independent filter and ADAA histories with identical
settings. The generic editor retains all existing controls and meters.

`xfrmOut` is a boolean, default false: **In** when false, **Out** when true.
`xfrmAmount` is 0–100%, step 0.1%, default 0%. Amount controls drive, linearly
in dB from -24 dB to +12 dB, with 20 ms smoothing. It is not a wet/dry mix.
Zero amount and Out fade to exact unprocessed samples over 10 ms, then skip
the entire stage. Filter and previous-sample state reset while skipped and
re-engagement fades back in. Existing parameter IDs/order are retained.

The model uses a +12 dB low shelf at 120 Hz, Q 0.5, a normalized symmetric
`tanh(d*x)/d` curve, and the algebraic reciprocal shelf (-12 dB at the same
frequency/Q). The inverse pair cancels to numerical precision with an identity
nonlinearity. All shelf, drive, smoothing, and ADAA tunables live in
`source/dsp/TransformerSettings.h`; coefficients and state use double precision.

First-order ADAA uses `F(u) = log(cosh(u))`, evaluated stably as
`abs(u) + log1p(exp(-2*abs(u))) - log(2)`, with `tanh` of the midpoint for tiny
sample differences. As agreed, ADAA is applied to the nonlinear residual:
`y = x + (ADAA(d*x, previousDriven) - midpoint(d*x, previousDriven)) / d`.
The undelayed linear path preserves small-signal unity instead of the
high-frequency loss from literal first-order ADAA's two-sample average.
There is no allocation, locking, logging, or UI access in processing.

Tube saturation, SAT MIX, AIR, HR (headroom), oversampling, and CH MODE remain
deferred. This is a basic transformer-style curve, not a physical transformer
model.

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
`wet - dry` at 100%. Wet includes IN gain, compression, transformer, OUT gain, and LFM;
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
deferred until CH MODE is built; oversampling and HR remain deferred.
