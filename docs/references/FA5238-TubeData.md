# FAA Type FA-5238 - tube data and source packet

Research snapshot: 23 October 2026. See the
[equipment handoff](FA5238-ClaudeHandoff.md) for identification limits.

This covers the **reported** FA-5238 tube inventory and the base types shown in a
related CA-1589 schematic. It is not a verified factory FA-5238 parts list.
All numerical data below are **published manufacturer or military-specification
data**, not new measurements or established FA-5238 operating points. No modelling,
curve fitting, hardware measurement or listening evaluation was performed.

## Original sources and suffix coverage

| Reported or related type | Inspected primary document | Coverage limit |
|---|---|---|
| 6AU6WC | [MIL-E-1/952G, 17 February 1981](https://www.tubezone.net/pdf/6AU6WC-JAN.pdf) | Exact WC type; acceptance tests/limits, not a full characteristic-curve atlas or proof of fitted manufacturer |
| 6AU6 base family | [GE 6AU6-A, ET-T916A, April 1960](https://frank.pocnet.net/sheets/093/6/6AU6A.pdf) | Extensive curves for the A type; not silently relabelled as measured WC curves |
| 6189W | [Tung-Sol 6189 / 12AU7WA](https://frank.pocnet.net/sheets/087/6/6189.pdf) | Manufacturer explicitly pairs those designations; no separate exact 6189W sheet verified |
| 12AU7 / 12AU7-A | [GE ET-T880A, February 1956](https://frank.pocnet.net/sheets/093/1/12AU7A.pdf) | Related base-type evidence for the CA-1589 output stage |
| 12AX7WA | [Tung-Sol, 1 August 1963](https://frank.pocnet.net/sheets/127/1/12AX7WA.pdf) | Exact WA designation in historical manufacturer sheet |
| 5726 | [Tung-Sol 5726 / 6AL5W / 6097](https://frank.pocnet.net/sheets/087/5/5726.pdf) | Exact 5726 designation, detector curves and ruggedized-type ratings |
| 6AL5 | [GE 6AL5 / 3AL5 / 12AL5, ET-T882, August 1954](https://frank.pocnet.net/sheets/093/6/6AL5.pdf) | Base-type detector reference; select the 6AL5 heater column, not 12AL5 |
| 6X4 | [GE ET-T865A, May 1958](https://frank.pocnet.net/sheets/093/6/6X4.pdf) | Power rectifier with common cathode; not the signal detector |

Scanned tables, graph labels and relevant footnotes were reviewed visually. Most
of these PDFs do not provide usable extracted text. The archive host is not the
datasheet author. Historical manufacturer data do not identify the production
samples or substitutions in any particular surviving unit.

## Published operating points

Heater values are per bottle. Triode current, gm, rp and mu are per section.
gm is transconductance in mS; rp is differential plate resistance in kilohms;
mu is intrinsic amplification factor, not circuit gain. Rk is a cathode resistor,
not a fixed grid-to-cathode bias. Source rounding is preserved.

| Type / source page | Heater V / A | Published test or typical conditions | Ia (mA) | gm (mS) | rp (kilohms) | mu |
|---|---|---|---:|---:|---:|---:|
| 6AU6WC, MIL p1-2 | 6.3 / 0.30 nominal; accepted heater current 0.275-0.325 A | Acceptance setup: plate test supply 250 V, screen 150 V, Rk 68 ohms, suppressor per note 2 | 8.0-13.5 acceptance range | 4.15-6.45 acceptance range | Not specified here | N/A |
| 6AU6-A, GE p2 | 6.3 / 0.30 | **Pentode:** plate 250 V, screen 150 V, Rk 68 ohms; screen current 4.3 mA | 10.6 | 5.2 | 1000 | Not a triode value |
| 6189 / 12AU7WA, Tung-Sol p2 | 6.3 / 0.30 or 12.6 / 0.15 | Plate 250 V, grid -8.5 V | 10.5 | 2.2 | 7.7 | 17 |
| 12AU7 / -A, GE p2 | 6.3 / 0.30 or 12.6 / 0.15 | Plate 250 V, grid -8.5 V | 10.5 | 2.2 | 7.7 | 17 |
| 12AX7WA, Tung-Sol p2 | 6.3 / 0.30 or 12.6 / 0.15 | Plate 250 V, grid -2 V | 1.2 | 1.65 | 62.5 | 100 |
| 5726 / 6AL5W, Tung-Sol p1 | 6.3 / 0.30 | Independent twin diode; p4 forward-current and rectified-output curves | Not a triode point | N/A | N/A | N/A |
| 6AL5, GE p1-2 | 6.3 / 0.30 | Independent twin diode; p3 forward-current and detector-load curves | Not a triode point | N/A | N/A | N/A |
| 6X4, GE p1-2 | 6.3 / 0.60 | Full-wave rectifier; separate choke-input and capacitor-input examples | Supply/load dependent | N/A | N/A | N/A |

The GE 6AU6-A sheet also has a triode-connected operating point and curves.
Those do **not** establish triode wiring in FA-5238; the related CA-1589 drawing
shows separately supplied screens. Do not substitute pentode and triode curves.

## Selected limits and interpretation

Limits are not nominal bias points, audio clipping levels or a PSU sag model.
Rating systems and conditions differ; consult complete sheets for simultaneous
constraints. These values must not be merged across tube variants.

| Type / primary source | Selected published limits |
|---|---|
| 6AU6WC, MIL p1 | Absolute maximum plate 330 V, screen 165 V, plate dissipation 3.3 W, screen dissipation 0.72 W. Note that these differ from the GE A-type ratings below. |
| 6AU6-A, GE p1/p3 | Pentode design-maximum plate 330 V, plate dissipation 3.5 W, screen dissipation 0.75 W; screen voltage must obey its rating chart. |
| 6189 / 12AU7WA, Tung-Sol p2 | Absolute maximum plate 330 V, plate dissipation 3.0 W per section, peak DC cathode current 22 mA per section. |
| 12AU7 / -A, GE p1 | Class A1 design-center plate 300 V, plate dissipation 2.75 W per section, DC cathode current 20 mA per section. Not interchangeable with the 6189 rating system above. |
| 12AX7WA, Tung-Sol p1 | Design-maximum plate 330 V, plate dissipation 1.0 W per section. Its published gm is 1650 micromhos; preserve it rather than replacing it with another manufacturer's nominal 1600. |
| 5726 / 6AL5W, Tung-Sol p1 | Absolute maximum inverse plate 360 V; peak plate current 60 mA, DC output 10 mA per section; surge current 350 mA per section. |
| 6AL5, GE p2 | Design-center inverse plate 330 V; steady-state peak plate current 54 mA, DC output 9 mA per section. These are base-type ratings, not the 5726 limits above. |
| 6X4, GE p1-4 | Design-maximum inverse plate 1250 V; steady-state peak plate current 245 mA per plate. Allowed DC output and AC supply voltage depend on filter type and the rating charts. |

The 6X4 p2 lists a 22 V forward drop at 70 mA DC per plate as a characteristic
point. It is not a constant drop or authorization for that DC current in each
plate of any full-wave supply. Its illustrative supply conditions are not the
FA-5238 power supply.

## Curves and useful modelling data

| Source | Where to look | Qualification |
|---|---|---|
| MIL 6AU6WC | p1 ratings and connections; p2 acceptance ranges and capacitances; p3-4 stability/life tests and notes | Sharp-cutoff pentode. No full plate/transfer curve families in this specification. |
| GE 6AU6-A | p3 plate/screen current families at several control biases; p4 screen-voltage families and separately labelled triode plate curves; p5 plate/screen transfer; p6 gm versus grid bias | Pentode transfer plots use plate 250 V and screens 50-150 V; they do not directly cover the related circuit's annotated 24 V screen-to-ground point. |
| Tung-Sol 6189 / 12AU7WA | p3 production ranges/matching criteria; p5 positive-grid plate/grid-current and transfer families; p6 mu/gm/rp variation and negative-bias plate family | p5 is marked tentative data. Matching tolerances are test conditions, not measured balance in the compressor. |
| GE 12AU7 / -A | p3 mu/gm/rp variation and resistance-coupled examples; p4 negative/positive-grid plate-current families; p5 transfer curves | Independent base-type reference, not proof of identical individual samples. |
| Tung-Sol 12AX7WA | p3 plate family and circuit example; p4 mu/gm/rp versus grid voltage | Relevant to a separately biased intermediate stage, not itself a compression law. |
| Tung-Sol 5726 / 6AL5W | p4 forward-current curve and rectified-output/load curves; p2 production/life criteria | Detector behaviour depends on source impedance and the connected RC network. |
| GE 6AL5 | p3 forward-current and rectified-output/load families | Provides a base-type comparison, not an interchangeable set of maximum ratings. |
| GE 6X4 | p3-4 rating charts; p4 forward-current curve; p5 loaded output curves for choke and capacitor filters | Useful for a later explicit PSU study only after the actual supply circuit is established. |

The original sheets also provide pin diagrams and interelectrode capacitances.
Preserve connection, shielding and section conditions when extracting them; do
not treat input capacitance as a universal fixed loaded stage capacitance.

## What remains missing

- Exact FA-5238 factory complement and suffix/manufacturer correspondence,
  particularly the reported 6189W designation.
- Verified in-circuit plate/screen/grid voltages versus regulation, component
  values, interstage loading and transformer behaviour for that revision.
- Exact 6AU6WC characteristic families near the circuit's low screen voltage,
  including control range, screen current, pair balance and positive-grid onset.
- Hardware response, harmonics and dynamics measured with documented source/load
  impedances, levels and control settings.

Research inference: these documents are useful evidence for a later tube-element
prototype, but no complete FA-5238 circuit model or ready-made coefficients have
been established. No sound-quality conclusion is drawn; that requires listening.
