# Lumiere Specification

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

Signal order is dry input copy -> IN gain -> existing compressor -> OUT gain ->
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
channels; S sends its side to both channels; D sends fully wet minus dry,
independent of Mix, and is silent when bypassed. Choices crossfade over 10 ms.
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
Release Mod controls, and saturation remain out of scope. Input/output meters
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
