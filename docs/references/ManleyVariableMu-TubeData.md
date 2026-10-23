# Manley Variable Mu - tube data handoff

Research snapshot: 23 October 2026. Companion to
[the compressor research handoff](ManleyVariableMu-ClaudeHandoff.md).

This is research only. No plugin code, models, parameters, latency or builds were
changed. Values below are **published manufacturer characteristics and ratings,
not our measurements and not established Manley operating points**. No listening
assessment, curve digitization, model fitting or hardware-accuracy claim is made.

## Tube complement and revision scope

[Manley's current specification](https://www.manley.com/products/pro-audio/dynamics/variable-mu)
lists these quantities across the stereo unit:

| Role | Tube complement |
|---|---|
| Standard gain control | 2 x 5670 |
| T-Bar gain control, replacing the 5670s | 4 x 6BA6 |
| Amplification | 2 x 5751 or 12AX7 |
| Current output stage | 2 x 12BH7; listed as production since April 2016 |
| Earlier output stage | 2 x 7044 or 5687; page specifies units before MSLC94006 |
| Sidechain rectification | 2 x 12AL5 |

The [October 2023 manufacturer manual](https://www.ordioproductions.com.au/wp-content/uploads/Manley-Variable-MU-User-Manual.pdf),
printed page 12, also documents original **6386** gain-control tubes and **12BA6**
T-Bar variants. Each T-Bar pair replaces one dual triode. The 6BA6/12BA6 pentodes
are **wired as triodes**; the choice depends on heater voltage. These are revision
alternatives, not tubes all installed together. The product page's FAQ calls
T-Bar standard on modern units while its specification distinguishes standard
5670 from T-Bar; verify the particular unit rather than resolving this by assumption.

## Electrical operating points

All triode figures are **per section**; heater figures are for the whole bottle.
Va is plate-to-cathode voltage; Vg is grid-to-cathode bias; Ia is plate current;
Rk is the cathode-bias resistor. A listed Rk must not be silently replaced with an
assumed fixed bias. gm is in mS, rp in kilohms. Intrinsic amplification factor
mu is **not** loaded circuit gain. Values are rounded as in the sources.

| Tube / selected original sheet | Heater V / A (alternatives) | Published typical point | mu | gm (mS) | rp (kilohms) |
|---|---|---|---:|---:|---:|
| [5670 - GE, ET-T1083A, Aug 1956](https://frank.yueksel.org/sheets/084/5/5670.pdf), p2 | 6.3 / 0.35 | Va 150 V, Rk 240 ohms, Ia 8.2 mA | 35 | 5.5 | 6.4 |
| [5751 - GE, ETX-245A, Jun 1952](https://bms.isjtr.ro/sheets/093/5/5751.pdf), p2 | 6.3 / 0.35 or 12.6 / 0.175 | Va 250 V, Vg -3 V, Ia 1.0 mA | 70 | 1.2 | 58 |
| [12AX7 - GE, ET-T509B, Jun 1953](https://frank.pocnet.net/sheets/093/1/12AX7.pdf), p1 | 6.3 / 0.30 or 12.6 / 0.15 | Va 250 V, Vg -2 V, Ia 1.2 mA | 100 | 1.6 | 62.5 |
| [12BH7-A - RCA, tentative data, Mar 1955](https://frank.yueksel.org/sheets/049/1/12BH7A.pdf), PDF p2 | 6.3 / 0.60 or 12.6 / 0.30 | Va 250 V, Vg -10.5 V, Ia 11.5 mA | 16.5 | 3.1 | 5.3 |
| [7044 - RCA, Mar 1962](https://frank.pocnet.net/sheets/049/7/7044.pdf), PDF p1 | 6.3 / 0.90 or 12.6 / 0.45 | Va 120 V, Vg -2 V, Ia 36 mA | 21 | 12.0 | 1.75 |
| [5687 - Tung-Sol, May 1962](https://frank.pocnet.net/sheets/127/5/5687.pdf), PDF p2 | 6.3 / 0.90 or 12.6 / 0.45 | Va 120 V, Vg -2 V, Ia 36 mA | 18 | 11.5 | 1.56 |
| [6386 - GE, ET-T1113, Aug 1954](https://frank.pocnet.net/sheets/142/6/6386.pdf), p2 | 6.3 / 0.35 | Va 100 V, Rk 200 ohms, Ia 9.6 mA | 17 | 4.0 | 4.25 |
| [6BA6 / 12BA6 - GE, ET-T897A, Nov 1956](https://frank.pocnet.net/sheets/093/6/6BA6.pdf), p1-2 | 6BA6: 6.3 / 0.30; 12BA6: 12.6 / 0.15 | **Pentode only:** Va 250 V, screen 100 V, Rk 68 ohms, Ia 11 mA, screen current 4.2 mA | Not a triode value | 4.4 | 1000 |
| [12AL5 - Tung-Sol, Mar 1953](https://frank.pocnet.net/sheets/127/1/12AL5.pdf), PDF p1-2 | 12.6 / 0.15 | Independent twin diode; forward-current and detector/load curves | N/A | N/A | N/A |

For the 12BH7 family, the selected original sheet is explicitly **12BH7-A**;
Manley's listing says 12BH7. This is not evidence of which suffix or manufacturer
was fitted to any specific unit. Historical sheets likewise do not establish the
manufacturer of today's fitted tubes.

## Selected limits - preserve rating systems and service conditions

These are limits, **not recommended bias points**, clipping thresholds or maximum
audio signal voltages. Refer to the complete sheets for simultaneous limits,
heater/cathode restrictions and circuit conditions.

| Tube | Published limits in the selected sheet |
|---|---|
| 5670 | Absolute maximum: Va 330 V, plate dissipation 1.35 W per section, DC cathode current 18 mA. These are specific to the selected GE revision; do not mix ratings from other 5670 sheets. |
| 5751 | Design-center: Va 300 V, plate 1.0 W per section. Absolute: 330 V, 1.1 W. These are different rating systems. |
| 12AX7 | Design-center: Va 300 V, plate 1.0 W per section. |
| 12BH7-A | Class A1 audio, design-center: Va 300 V, plate 3.5 W per section. The 450 V rating elsewhere in this sheet is for deflection service; do not quote it as the audio rating. |
| 7044 | Absolute maximum average Va 300 V; either plate 4.5 W, both plates together 8 W. Sheet is for computer/on-off service and separately lists pulse limits. |
| 5687 | Design-center: Va 300 V; either plate 4.2 W, both together 7.5 W. |
| 6386 | Design-center: Va 300 V, plate 1.5 W per section, DC cathode current 18 mA. |
| 6BA6 / 12BA6 | GE design-center: plate 300 V, plate 3.0 W, screen 0.6 W; screen voltage requires the rating chart. Heater/cathode limits differ between heater variants. These are pentode ratings. |
| 12AL5 | Peak inverse plate voltage 330 V; steady-state peak plate current 54 mA and DC output current 9 mA per plate. Peak and DC current are not interchangeable. |

## Curve inventory for later modelling

Page references below use PDF sequence unless a printed page is explicit. The
links above are original manufacturer documents hosted by archives, not new
measurements. Scans were visually inspected because text extraction is incomplete.

| Tube | Available curve/data locations | Important qualification |
|---|---|---|
| 5670 | p5 plate and transfer families, including positive-grid/grid-current information; p6 mu/gm/rp variation; p3 production ranges | GE calls it a medium-mu general-purpose twin triode; do not assume the remote-cutoff law of a 6386. |
| 5751 | p2 plate family; p3 transfer family and mu/gm/rp versus current | Separate model from 12AX7; mu 70 versus 100 is not a universal circuit-gain ratio. |
| 12AX7 | p3 plate and transfer families; p2/p4 mu/gm/rp variation; p2 resistance-coupled amplifier table | Table and curves specify operating conditions. |
| 12BH7-A | PDF p4 plate family; p1-2 audio ratings and characteristics | Graph orientation is rotated; avoid switching axes when digitizing. |
| 7044 | PDF p4 plate/grid-current families; p5 mu/gm/rp curves; p3 tolerance ranges | Includes positive-grid behaviour; not identical to 5687. |
| 5687 | PDF p3 plate and transfer families; p4 positive-grid plate/grid-current and mu/gm/rp curves | Additional typical points: 180 V/-7 V/23 mA and 250 V/-12.5 V/12 mA. |
| 6386 | p3 individual-section mu/gm/rp; **p4 lower graph** individual transfer; **p5 lower graph** individual plate family | Upper graphs include cascode characteristics; p6 is a cascode example. Do not fit cascode curves as one triode. |
| 6BA6 / 12BA6 | p3 plate/screen and transfer families; p4 transfer and screen-rating chart; p5 gm versus control-grid voltage | All are pentode-connected. **No triode-connected curves found in this inspected sheet.** |
| 12AL5 | PDF p2 forward-current curve and rectified-output/load curves | Detector model needs this conduction characteristic plus the actual external circuit; no fixed silicon-diode threshold assumption. |

## What this supports - and what remains missing

The sources provide starting evidence for tube element equations, incremental
parameters, capacitance estimates, cutoff behaviour and grid-current modelling.
They do **not** identify complete compressor stages or provide a ready-to-use DSP
model. No curve samples or fitted coefficients are supplied here.

The most consequential missing data are:

- Actual plate/cathode/grid operating points, load lines, coupling networks and
  transformer loading for the chosen hardware revision.
- Triode-connected 6BA6/12BA6 plate/transfer curves across the relevant bias range,
  and the adapter's exact electrode connections. Manley describes curve-tracer
  confirmation, but the manual does not publish that dataset.
- Selected-pair matching over changing control bias, imbalance and its effect in
  the balanced gain-control circuit. Generic typical curves do not establish it.
- In-circuit positive-grid behaviour, recovery, interstage interaction and
  detector time constants. The 12AL5's own curves do not determine attack/release.
- Measured variation for the tube manufacturers and production samples actually
  being targeted. Modern reissues must not silently inherit historical curves.

Research inference: these documents are enough to begin a separately authorized,
physically motivated prototype; they are insufficient to claim that it reproduces
a specific hardware unit. Sound and feel require later listening. This packet
does not authorize that prototype or change the module-spike decisions.
