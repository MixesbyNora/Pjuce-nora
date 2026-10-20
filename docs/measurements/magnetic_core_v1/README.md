# Offline magnetic-core report v1

Generated from `preisach-play-core-v1`, default memory 0.35 / width 1.0.
These are normalized magnetization-model results, not VTB1148 measurements or plugin audio.

![Modeled loops](loops.png)

## Findings

- History, remanence, nested-loop closure, stability and common-time rate checks passed.
- High-frequency folding is substantial; the current core is not ready for live integration.
- 4x/8x reference differences are retained in the full CSV, so the reference is not represented as exact.

At 48 kHz and normalized field peak 1:

| Actual tone Hz | Nonharmonic alias dBr | In-band residual dBr | 4x/8x difference dBr |
|---:|---:|---:|---:|
| 49.80 | -119.95 | -119.69 | -141.00 |
| 999.02 | -78.69 | -76.38 | -101.92 |
| 6999.02 | -42.36 | -40.59 | -69.53 |
| 9999.02 | -22.85 | -22.77 | -63.57 |

All dBr values are relative to the 8x reference in-band RMS magnetization.
In-band residual also includes sampling of history/extrema, not only aliasing.

## Storage and measured local cost

- Fixed core storage: 408 bytes; 16 play updates and 17 tanh evaluations per sample.
- Local Apple M2 Pro arm64 Debug median: 391.1 ns per mono sample (five runs of 32,768 samples).
- Multiplying this mono cost by two channels estimates 3.75% of one core at 48 kHz and 15.02% at 192 kHz.
- These estimates include loop/checksum overhead and exclude plugin/circuit/antialiasing costs; they are not realtime deadline guarantees.

See [the model notes](../../MagneticCorePrototype.md) for equations, scope, references, test/report commands and the spectral method.
