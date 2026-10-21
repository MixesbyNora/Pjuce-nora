# Offline transformer CPU optimization, pass 1

> Historical milestone: the magnetic engine is now shared by live and offline audio. See [current implementation](MagneticTransformerLive.md). The measurements below describe this earlier step.

This optimizes only the Tests-only voltage-driven transformer prototype.
The live tanh transformer, saved voicing, compressor, parameters, editor,
meters and plugin latency are unchanged. The model remains
`voltage-driven-play-transformer-v1`; the circuit equations, constants,
16x FIR oversampling, initial root estimate, safeguarded Newton/bisection
policy, 48-evaluation cap and 1e-13 scaled residual tolerance are unchanged.

## Profile and implementation

A three-second macOS `sample` of the original Release-style standalone circuit
driver collected 2,300 main-thread samples. 1,365 landed in `tanh` and 260 in its
call stub: about 71% together. This isolates the circuit and includes stimulus
generation; it is not a profile of the FIR wrapper or a whole plugin. LTO inlines
the circuit into the driver, so this sample identifies library cost rather than
separating individual inlined solver functions.

Three exact-value reuse opportunities remove work:

- At an unchanged play coordinate, reuse its committed `tanh` value.
- A trial at the last accepted clipped field reuses its magnetic sum. All play
  coordinates are then in their closed intervals; the strict branch derivative
  contains only the reversible contribution. Out-of-guard fields still have zero
  constitutive slope.
- Commit the accepted coordinates and nonlinear values without calling `tanh`
  again. Preserve the original canonical summation order, including its final
  clamp. The differently grouped trial M is retained separately.

Equality checks compare double bit patterns, so signed zero cannot accidentally
reuse the opposite sign. Reset/prepare clear every cache with the demagnetized
history. Trial queries are const and cannot advance history. Fixed arrays and
bounded loops retain the no-allocation, no-lock, no-logging processing rule.
The canonical `PlayPreisachCore::processField` implementation is untouched; a
private friend declaration permits the coupled solver's accepted-state commit.

## Independent reference and checks

`tests/helpers/TransformerAudioReference.*`, its settings header and its FIR
harness freeze the original v1 source. Do not update them alongside candidate
optimizations: they are the numerical and timing oracle. The earlier
[audio v1 artifacts](measurements/transformer_audio_v1/README.md), including all
listening WAVs, remain untouched.
The frozen solver shares the unchanged canonical core implementation and FIR
library with the candidate; source hashes pin those dependencies for this pass.
The preserved v1 files provide an additional independent artifact comparison.

Normal tests compare raw double output and final magnetic states bit-for-bit
across the 108 waveform/rate/level combinations, with independent stereo
trajectories. They additionally cover 108 combinations of rate, memory, width
and drive at extreme corner/weight, DC steps, noise, silence, invalid input,
reset/reprepare, linear/zero-weight paths, exact/adjacent fields, field guards
and non-mutating branch queries. Existing plugin/audio tests still apply.

The expanded extreme trajectories exposed an inherited limitation: at corner
30 Hz, weight 16, drive +12 dB and some 44.1 kHz, memory-zero trajectories,
the frozen solver exhausts its 48-evaluation cap. Candidate output, history,
failure counts and worst residual match the frozen reference exactly. This
pass retains those diagnostics and does not relax the tolerance or alter the
solver policy. The default audio-validation and profiling grids are evaluated
separately for zero failures. Broader numerical robustness remains future work.

## Reproduce

Use the existing Xcode project; do not build Benchmarks:

```bash
cmake -B Builds -G Xcode
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone Tests
ctest --test-dir Builds -C Debug --verbose --output-on-failure -E '^Benchmarks'
cmake --build Builds --config Release --target Tests
LUMIERE_TRANSFORMER_AUDIO_REPORT_DIR=/private/tmp/lumiere-transformer-audio-cpu \
LUMIERE_TRANSFORMER_AUDIO_FULL_GRID=1 \
./Builds/Release/Tests --reporter compact
python3 tools/check_transformer_audio_optimization.py \
  /private/tmp/lumiere-transformer-audio-cpu docs/measurements/transformer_audio_v1
LUMIERE_TRANSFORMER_CPU_REPORT_DIR=/private/tmp/lumiere-transformer-cpu \
./Builds/Release/Tests '[transformer-cpu-report]' --reporter compact
```

The paired timing test warms both implementations, alternates their execution
order over seven runs, and reports each median for 16,384 base frames. It covers
mono/stereo, 44.1/48/96/192 kHz, low tones, multitone and repeated transients.
It includes 16x FIR, circuit, copies and stimulus generation; it excludes
preparation, FFT and file output. Failure counts aggregate both implementations
and every active channel. A frame includes all channels. Percentages are elapsed
processing time divided by audio duration on one core, not worst-case deadline
guarantees. Do not overlap profiling with builds, tests or other measurements.
The CTest exclusion removes the generated `Benchmarks_NOT_BUILT` placeholder;
the Benchmarks target remains unbuilt.

For call-stack sampling, compile the small independent driver:

```bash
clang++ -std=c++23 -O3 -g -flto -Isource -Itests \
  tools/profile_transformer_audio.cpp tests/helpers/TransformerAudioReference.cpp \
  source/experimental/TransformerAudioCircuit.cpp \
  source/experimental/PlayPreisachCore.cpp -o /private/tmp/lumiere-transformer-profile
/private/tmp/lumiere-transformer-profile reference &
lumiere_profile_pid=$!
sample "$lumiere_profile_pid" 3 -file /private/tmp/lumiere-transformer-reference-sample.txt
wait "$lumiere_profile_pid"
```

Use `candidate` to sample the optimized circuit. Preparation is outside the
driver's timed loop. Its 24 million internal samples are not base-rate frames;
use the paired Tests report for complete-chain CPU percentages.

## Paired Release results

![Paired stereo CPU measurements](measurements/transformer_cpu_v1/cpu.png)

On the measured Mac, speedup is 1.43-1.58x (30.1-36.6% less elapsed work).
Every paired checksum is identical; every active channel in both versions has
zero solver failures on these workloads.

| Workload | Rate Hz | Stereo before, one-core budget % | Stereo after % | Speedup |
|---|---:|---:|---:|---:|
| low_tone | 44100 | 79.2 | 55.3 | 1.43x |
| low_tone | 48000 | 86.1 | 60.2 | 1.43x |
| low_tone | 96000 | 170.0 | 118.3 | 1.44x |
| low_tone | 192000 | 334.2 | 230.4 | 1.45x |
| multitone | 44100 | 75.0 | 47.7 | 1.57x |
| multitone | 48000 | 81.6 | 52.0 | 1.57x |
| multitone | 96000 | 162.6 | 103.1 | 1.58x |
| multitone | 192000 | 322.9 | 205.3 | 1.57x |
| transient | 44100 | 78.9 | 52.7 | 1.50x |
| transient | 48000 | 85.8 | 57.6 | 1.49x |
| transient | 96000 | 169.9 | 113.1 | 1.50x |
| transient | 192000 | 335.9 | 224.5 | 1.50x |

The original low-tone workload at stereo 48 kHz drops from 86.1% to 60.2%;
96 kHz remains 118.3% and 192 kHz 230.4%. This is useful progress, but the
implementation remains offline. A single-default-setting median does not
establish live readiness, worst-case cost or suitability across tunings.
The remaining nonlinear evaluations still dominate the standalone sample;
further optimization and a separate robustness pass are needed before live use.

[All mono/stereo timings](measurements/transformer_cpu_v1/paired_timing.csv).

## Validation outcome

The fresh complete-chain export passes 108/108 screens, with all 108 references
converging at 64x. Worst DC-20 kHz residual remains -104.107709707692 dBr;
the maximum scaled equation residual is 9.999998336674936e-14, maximum observed
iterations is five and failures are zero. FIR delay remains 141.875 base samples.

All five measurement CSVs and all 21 regenerated WAVs match the preserved v1
files byte-for-byte. The existing listening files therefore remain valid;
this pass makes no subjective preference or hardware-accuracy claim.
The normal stereo equivalence test also checks 3,538,944 raw double output
samples across its 108 windows against the frozen implementation.

All 126 functional cases pass in both configurations: 282,070 assertions in
Debug, and 283,277 in Release with the full export. The separate paired timing
export passes 50 assertions. Debug Lumiere_VST3, Lumiere_AU,
Lumiere_Standalone and Tests, and Release Tests, build with zero warnings/errors.
No Benchmarks target is built. The generated unbuilt-Benchmarks CTest sentinel
is excluded from the final filtered runs.

All 41 live source files, the favorite voicing, the canonical core implementation
and all 38 original measurement artifacts are unchanged. The core header only
adds the private commit friendship. None of the three plugin binaries contains
the experimental circuit or frozen reference symbols.

[Captured data and provenance](measurements/transformer_cpu_v1/README.md).

The chart uses the existing ReportLab workflow:
`python3 tools/render_transformer_cpu_report.py docs/measurements/transformer_cpu_v1`.
Rasterize its PDF with `sips -s format png cpu.pdf --out cpu.png` if needed.
