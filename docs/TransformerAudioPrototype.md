# Offline transformer audio prototype

> Historical milestone: the magnetic engine is now shared by live and offline audio. See [current implementation](MagneticTransformerLive.md). The measurements below describe this earlier step.

This begins the audio coupling step following
[magnetic artifact refinement](MagneticArtifactRefinement.md). The new circuit,
oversampling harness, tests and renders are linked only into Tests. The current
live tanh transformer, favorite voicing, compressor, editor, meters, parameter
IDs and plugin latency remain unchanged. The owner's broader modular product
vision remains separate from this single experimental model.

## Circuit and normalized units

Use an ideal normalized 1:1 transformer with a nonlinear magnetizing branch,
finite source/winding resistance and a resistive referred load. Fold the constant
load divider into the normalization; omit leakage inductance, winding capacitance
and eddy-current dynamics for this first prototype. The input/output interface
uses linear audio voltage (1 means full-scale peak). H, M and B are normalized
magnetic variables, not SI measurements or identified material properties.

The unchanged canonical play core supplies `M = play(H, history)`. Define:

```
B = H + k*M
D*x = H + tau * dB/dt
y = x - H/D = (tau/D) * dB/dt
```

The H term represents magnetizing-current voltage drop across finite source
resistance; output y represents normalized induced voltage. To see the circuit
reduction, let the primary induced voltage be v, source resistance R, referred
load Rl, and magnetizing current c*H. Kirchhoff gives
`u = (1 + R/Rl)*v + R*c*H`. With `v = N*A*dB/dt` and an input calibration
`u = alpha*x`, define `D = alpha/(R*c)` and
`tau = (1 + R/Rl)*N*A/(R*c)`. Normalize output by the same fixed divider to
obtain y. The prototype exposes lumped D/tau/k rather than claiming measured
R, Rl, turns, area or analog voltage calibration.

The electrical/magnetic distinction follows the terminal-voltage and magnetic
circuit treatment in [MIT's Electromagnetic Fields and Energy, chapter 9](https://ocw.mit.edu/courses/res-6-001-electromagnetic-fields-and-energy-spring-2008/pages/chapter-9/).
The general motivation for coupling a hysteretic element into an electrical
circuit is also described by [Massi et al. (2023)](https://link.springer.com/article/10.1186/s13636-023-00277-8).
This reduction and numerical implementation are our engineering prototype; they
do not implement that paper's RNN/WDF model or claim its calibration.

Magnetization can remain nonzero after excitation. Constant flux does not
generate output voltage: after settling, silence produces silence while M is
retained. There is no added DC blocker, tail subtraction or silent-input reset.
A sustained DC input likewise settles to zero induced output with a biased
field. Reset explicitly demagnetizes and clears the circuit/filter histories.

## Settings

All new tuning, bounds, safety and solver constants live in
`source/experimental/TransformerAudioSettings.h`. Core density/radii remain in
the original `MagneticCoreSettings.h`.

| Setting | Default | Role |
|---|---:|---|
| Small-signal corner | 5 Hz | Sets the initial linearized low-frequency pole. |
| Field scale | 40 | Maps normalized input voltage into the field-driving force. |
| Drive calibration | 0 dB | Scales excitation; normalized small-signal output gain stays fixed. |
| Magnetic weighting k | 4 | Weights M in B = H + k*M. |
| Memory / width | 0.35 / 1 | Existing play-core settings. |
| Oversampling | 16x | Existing double-precision linear-phase FIR design. |

The initial differential flux slope is
`mu = 1 + k*(1-memory)/saturationField`; use
`tau = 1/(2*pi*cornerHz*mu)`. Thus the quiet, demagnetized transfer is the
first-order high-pass `s/(s + 2*pi*cornerHz)`. Its expected small-signal loss at
20 Hz is about 0.26 dB, and at 50 Hz about 0.04 dB. This deliberate linear
response is retained in the reference and measurements, not normalized away.
The finite-amplitude permeability and response can differ as play states move.

Values are heuristic starting points for measurement/listening, not a VTB1148
fit. The published observations retained in [TransformerVoicing.md](TransformerVoicing.md)
are still reference constraints, not a calibration dataset. The source/load
normalization, physical units and additional losses require further work before
a named hardware model is justified.

The canonical core still clips its own H input to +/-32. The circuit can retain
a larger linear H contribution in B while the M contribution saturates at that
limit. This explicit saturated extension avoids clamping the entire flux state;
it is a numerical definition, not measured extreme-field behavior. Voltage input
is limited to +/-16; NaN/Inf become zero excitation without clearing history.
Safety limits are not level controls. Their diagnostic counts should be reviewed
when rendering material near those bounds.

## Implicit state update

Trapezoidal integration of the circuit equation gives:

```
q = dt/(2*tau)
F(Hn) = Hn-Hprev + k*(Mtrial(Hn)-Mprev)
        - q*(D*(xn+xprev)-Hprev-Hn) = 0
```

Trial evaluation reads the original play states and calculates M plus the
active branch slope without modifying history. Positive weights and the linear
H term make F strictly increasing. The memoryless estimate brackets the root
within +/-`2*k/(1+q)` because |M| <= 1. Safeguarded Newton steps use the
nonnegative branch derivative, then bounded bisection if needed. At most 48
evaluations occur; convergence uses `abs(F)/max(1,abs(H)) <= 1e-13`.
Only the accepted field commits the canonical play history once. The original
implementation called `PlayPreisachCore::processField`; the first CPU optimization
reuses the accepted trial's exact values, retaining the canonical commit's
summation order. Its separate trial sum has different rounding and must not be
substituted for the committed M. See
[CPU optimization](TransformerAudioCpuOptimization.md).
Diagnostics expose the maximum residual, maximum iteration count and failures.
All storage is fixed; the solve/process methods allocate nothing, log nothing,
lock nothing and call no UI code.

The standalone quartic 16x candidate assumes a prescribed field trajectory and
two future field samples. It cannot be inserted unchanged inside this causal
solve, where H is the unknown and depends on magnetic history. Its implementation
and reports remain intact. The new audio circuit uses point commits at 16x and
must earn its own complete-chain convergence/alias checks. The earlier 108/108
prescribed-H result is not an audio-circuit certification.

## Tests and measurement workflow

The default circuit passes all 108 complete-chain numerical screens with
converged 64x references. Its worst DC-20 kHz residual is -104.11 dBr, compared
with the -80 dBr screen; the worst linear calibration residual is -104.16 dBr.
All 108 chosen reference steps also meet the separate full-band maximum-error
diagnostic. Maximum scaled equation residual stays below 1e-13; the largest
observed solver iteration count is five, with zero failures. The finite test
grid does not certify arbitrary inputs, other tunings or hardware accuracy.

All 122 tests pass: 280,760 assertions in Debug and 281,859 in Release including
the complete export. Debug VST3/AU/Standalone/Tests and Release Tests build
without warnings or errors. Existing live-audio regressions continue to pass.

CPU is the immediate implementation limit: the original local Release benchmark uses
about 86.0% of one core's audio-time budget for stereo at 48 kHz, 169.9% at
96 kHz and 334.2% at 192 kHz. Those are median costs for the measured signal,
not worst-case guarantees. This implementation remains an offline reference
and listening prototype. Optimize trial evaluation/accepted-state processing
before considering live use; do not infer real-time readiness from the passing
accuracy tests.

Those v1 artifacts are preserved as the pre-optimization baseline. Updated paired
timings, frozen-reference comparisons and build/test results are recorded in
[TransformerAudioCpuOptimization.md](TransformerAudioCpuOptimization.md).

Tests check trial evaluation against the canonical law, absence of trial-state
mutation, the committed-state circuit equation, exact bilinear linear gain/phase,
small-signal behavior, DC rejection with retained magnetization, extreme settings,
invalid input, reset, channel isolation and sample/block partitioning. Preparation
allocates the FIR buffers; blocks up to 4096 are handled in bounded 256-frame
views, including one-sample blocks.

The complete measurement method, results, plots and audio fixtures are in
[transformer_audio_v1](measurements/transformer_audio_v1/README.md). References
evaluate analytic voltage at increasing internal rates and use independent real
FIR decimation. Input waveforms reuse the prior nine shapes, now as voltage
instead of prescribed H. Numerical screens are independent of hardware realism
and preference. Physical flux relaxation means a changing output tail is not
automatically a failure; differences from the converged circuit reference matter.

Listening exports include synthetic stereo bass, percussion and low/high tones,
plus a matching linear circuit and the unchanged favorite tanh implementation.
Raw and linked whole-file RMS-matched WAVs are both retained. FIR delay is
compensated; physical phase is preserved. These comparisons change complete
models, including frequency response and antialiasing, and do not isolate
hysteresis alone. No user music clips have been provided for this step.

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
./Builds/Debug/Tests --reporter compact
cmake --build Builds --config Release --target Tests
LUMIERE_TRANSFORMER_AUDIO_REPORT_DIR=/private/tmp/lumiere-transformer-audio \
LUMIERE_TRANSFORMER_AUDIO_FULL_GRID=1 \
./Builds/Release/Tests --reporter compact
```

Omit `LUMIERE_TRANSFORMER_AUDIO_FULL_GRID` for the 24 close-tone/impulse cases.
Without the output directory, the long export is skipped. Timing runs occur last
and should not overlap other builds/tests. No Benchmarks target is built. With
ReportLab/NumPy available, run
`python3 tools/render_transformer_audio_report.py <export-directory>` and use
`sips` to rasterize the generated validation/tones/impulse PDFs on macOS.
