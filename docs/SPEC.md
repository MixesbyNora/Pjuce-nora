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
