# Hybrid model architecture: transformer family plus WDF tube stages

**PROPOSAL.** Owner direction, 2026-10-22: pursue a hybrid. One transformer model
family for every transformer, and wave digital filter (WDF) models for tube
stages. Nothing here is built yet. Measurements behind it are in
[ShuntImprovement.md](ShuntImprovement.md).

## Summary

| Part | Method | Why |
|---|---|---|
| Output and input transformers | Shunt variant C, retuned per part | Hysteresis, low-frequency saturation driven by flux, even harmonics; calibrated from published data; aliases little (−120 dBc at 4×) |
| Tube stages | WDF with local antiderivative anti-aliasing (ADAA) | Tubes are memoryless nonlinearities in a circuit, the case WDF local ADAA was built for; they clip harder, so aliasing matters |
| Between stages | Linear impedance ports (module contract `Impedance`) | Carries the coupling that matters most at low cost; validated against a fully coupled reference (below) |

## Transformers: one family, many tunings

- **One curve for every transformer.** Variant C (`OuterPlayElement`): play
  hysteresis, then one saturating curve, with polarity asymmetry. Every
  transformer, including a driven "vintage" option, is a *tuning* of it, not a
  new model.
- **The calibration process is reusable**, as done for the VTB1148 output part:
  1. Collect published data, with sources cited.
  2. Map circuit values from the datasheet.
  3. Fit the shape to distortion-vs-level, distortion-vs-frequency and
     harmonic points.
  4. Choose saturation onset by ear, inside what the data allows.
  5. Measure aliasing and set the oversampling factor.
- **Each tuning carries its own oversampling factor**, accepted only if the
  aliasing test shows it below an agreed limit across its whole drive range.
  - Gentle tunings: 4/2/1 (the VTB1148 output value).
  - Heavily driven tunings may need 8/4/2. Anti-aliasing work is justified only
    if a tuning needs more than that.
- **Input transformer, Carnhill VTB9045.** This is the Neve 10468 mic input
  position: mumetal core, 1+1:2+2 windings as tested.
  - The Woods (2019) thesis gives about 0.042% THD at −50 dBu, 20 Hz, and about
    +2 dB at 22 kHz.
  - That HF rise needs the on-hold leakage/capacitance stage: a second-order
    section with resonance after the core. It is the first time that stage has
    published data to fit.

## Tube stages: WDF with local anti-aliasing

- Each tube stage is a WDF tree: the tube is the nonlinear element, and the
  passive network is built from adaptors.
- Local ADAA follows Albertini, Bernardini & Sarti (DAFx-20; JAES 69, 2021).
  The anti-aliased element is built independently of the surrounding circuit.
- **Risks:**
  - A triode is a multiport nonlinearity (grid and plate). It needs a
    multiport root or local iteration.
  - Tube plus power-supply sag in one tree means more than one nonlinearity.
    That needs iterative solvers or a deliberate one-sample break.
  - Gain changes per sample (compression acting inside a stage) change port
    resistances. That costs adaptor updates and can cause artifacts.

## Coupling between stages

In the shunt model the transformer's output distortion is
**source resistance × magnetizing current**. When a tube drives a transformer,
that source resistance is the tube's output resistance. So the interface has to
carry impedance in both directions:

- **Tube → transformer.** The tube stage reports its output (plate) resistance
  through the module contract's `Impedance` metadata. The transformer uses it
  as `sourceOhms`. Plate resistance moves with the operating point; whether a
  fixed value is enough is part of the test below.
- **Transformer → tube.** The tube's WDF tree includes the transformer's
  *linear* input side: the load reflected through the turns ratio (VTB1148,
  600 Ω load → about 208 Ω), the winding resistance, and optionally the linear
  magnetizing inductance. The tube then works on the correct load line.
- **Not carried:** the transformer's *nonlinear* magnetizing current feeding
  back into the tube's operating point when bass saturates the core. This is
  the one effect the test must size.
- **The input transformer is the easy case.** It is fed by a mic or line source
  and loaded by a high-impedance grid, so its coupling is close to trivial.

## Test: tube-to-transformer coupling (PROPOSAL)

**RUN 2026-10-23**, see [TubeStage.md](TubeStage.md#step-2-tube-to-transformer-coupling-test).
- The fixture became a 12BH7 cathode follower (owner decision), because a
  common-cathode 12BH7 loses the bass into this primary.
- The stage was solved by trapezoidal nodal Newton, not a WDF tree. A WDF with
  trapezoidal capacitors gives the same discretization. WDF and ADAA wait for
  live integration.
- **Result:** the linear interface (a) holds to better than −40 dB until the
  iron saturates, then fails, as expected. Per-block resistance (b) does not
  fix it. No coupling (c) is 10–43 dB off everywhere.
- **Recommendation:** a joint tube + C solve for this pair. It is one scalar
  root per sample and costs about 35% more than (a), unoptimized.


**Question:** is the linear impedance interface close enough to a fully
coupled solve when a tube stage drives the VTB1148 output transformer?

1. **Fixture.** A single-triode stage as a WDF test fixture, in Tests only, like
   the module spike's source fixture.
   - Common-cathode, plate load coupled to the transformer primary.
   - Use a standard published triode equation, cited, not fitted to a product.
   - Report its small-signal plate resistance at the operating point.
2. **Reference.** An offline joint solve: tube equations plus the C transformer
   circuit, solved together each sample at a high fixed rate, with convergence
   flags. This extends `OfflineCoupledReference.h`.
3. **Candidates.**
   - (a) Linear interface: the tube WDF tree with reflected load and winding
     resistance; C with `sourceOhms` = small-signal plate resistance.
   - (b) As (a), with the plate resistance updated per block from the tube's
     operating point.
   - (c) No coupling: the tube into a fixed resistor, and C with a nominal
     source.
4. **Measurements.** Level-matched and unmatched difference to the reference,
   harmonic spectra and THD versus level at 20/50/100/1000 Hz, plus renders.
   - Drive from nominal up to the point where the transformer saturates.
   - The low-frequency saturated region is where (a) should fail first, if it
     fails.
5. **Owner listening.** The reference against (a), (b) and (c), on bass, drums
   and full mix.
6. **Decision gate.** If (a) or (b) matches within an owner-set limit, keep the
   linear interface. Otherwise, consider a joint solve for this pair only, or a
   WDF wrapper that puts C at the root of the tube's tree.

## Oversampling across a chain

Stages that run back to back should share one oversampling region, at the
highest factor any of them needs. The module spike measured 22–32% CPU savings
and half the latency from sharing. The rate negotiation between modules is
still undesigned.

## Open decisions

- The aliasing limit a tuning must meet.
- The CPU and latency budgets per chain.
- Which tube types and circuits come first.
- Whether power-supply sag joins the tube tree or stays a separate control
  stage.
