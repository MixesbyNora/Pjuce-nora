# Archived: Magnetic transformer engine

**Retired 2026-10-22** by the owner, after listening, in favour of the VTB1148
output transformer: shunt variant C, calibrated to published Carnhill VTB1148
data. It was removed from the live build, the test suite and the research
harness. Nothing in this folder is compiled.

## Restoring it exactly

The git tag `magnetic-engine-final` is the last commit with the Magnetic engine
built into the plugin. There it is selectable next to VTB1148 through the
"Xfrm Model" parameter.

```bash
git checkout magnetic-engine-final
```

## What is here

The files as they were at that tag:

- `source/dsp/TransformerSaturation.*`: the live Magnetic stage. It held the
  16/8/4 oversampling, the voicing bank crossfades and level match.
- `source/experimental/*`: the voltage-driven play-Preisach circuit, the 16
  operators, the constitutive lookup table and their settings.
- `tests/*`: the Magnetic core, oversampling, refinement, live, voicing, audio
  optimization and commit-diagnosis tests.
- `tests/helpers/*`: the analysis, oversampling and reference helpers.
  - `MagneticValidation.*` here is the original version. The live tree keeps a
    reduced copy (stimuli, alignment, metrics) without the Magnetic reference
    renders.

Its design and measurement history stays in `docs/`, for example
`MagneticTransformerLive.md`, `TransformerVoicing.md` and `docs/measurements/`.

## What replaced it

- [`source/dsp/VtbOutputTransformer`](../../source/dsp/VtbOutputTransformer.h)
- Workbench controls: saturation onset, even harmonics, low-level grain,
  hysteresis memory.
- The full history is in [`docs/ShuntImprovement.md`](../../docs/ShuntImprovement.md).
