# FAA Type FA-5238 - research handoff for Claude

Prepared for Beyond Analog / Lumiere. Research snapshot: 23 October 2026.
Companion: [tube data, operating points and curve inventory](FA5238-TubeData.md).

## Scope and evidence status

Research only: no plugin source, parameters, UI, latency, tests or build settings
were changed. No hardware measurements, curve digitization, model fitting or
listening assessment were performed. No hardware-accuracy claim is made.

The exact FA-5238 is less well documented in the material retrieved than the
Manley Variable Mu. Keep three evidence classes separate:

- **Primary component documents inspected:** tube manufacturers' datasheets and a
  military tube specification. A related schematic scan is marked **CA-1589** by
  hand; its complete manual and title-page provenance were not retrieved.
- **First-hand report, unverified unit:** the tube inventory reported by the
  owner in the FA-5238 discussion. It is a useful lead, not a factory parts list.
- **Unretrieved lead:** an FAA report URL and reports of other related drawings.
  Neither establishes specifications until the document is obtained and checked.

Do not promote the related CA-1589 drawing into a verified FA-5238 schematic.
Do not use Federal AM-864/U specifications for this equipment.

## Identification and provisional tube inventory

The [FA-5238 owner discussion](https://groupdiy.com/threads/federal-fa-5238-dept-of-commerce-limiter.33552/)
identifies the unit with the Department of Commerce limiter family. The opening
owner report describes the following fitted tubes:

| Reported tube | Quantity | Role suggested by the related CA-1589 drawing |
|---|---:|---|
| 6AU6WC | 2 | Balanced input / controlled-gain pentodes |
| 12AX7WA | 1 | Intermediate amplifier, two triode sections |
| 6189W, described as 12AU7 family | 1 | Output amplifier, two triode sections |
| 5726, described as 6AL5 family | 1 | Signal rectification / output regulation detector |
| 6X4 | 1 | Power-supply rectifier |

The role column is a **cross-model inference**, not verification of the exact
FA-5238 wiring. Fitted tubes may also be later replacements. The archive's
[equipment listing](https://retrogearshop.com/collections/all?page=55) associates
FA-5238 with Tri-Tronics; it is an identification lead, not an engineering manual.
No readable FA-5238 nameplate or factory tube-location chart was verified here.

The [family discussion](https://groupdiy.com/threads/dept-of-commerce-limiters-family.58486/)
is a useful index to CA-series variants, not proof that every variant has the same
values, tubes, transformers or response. In particular, reports about earlier
octal-tube machines must not become FA-5238 specifications.

## Related circuit evidence: attributed to CA-1589 only

[Deluxe Recording's documentation index](https://www.deluxerecordingstudio.com/copy-of-tech-projects)
attributes this [schematic, Figure 13](https://static.wixstatic.com/media/1c533f_8b5bafeb44d648b9a1aaa18ff8a6222b~mv2.jpg)
to CA-1589. The model label and other material are handwritten, including an
input-pad annotation; the handwriting must not be assumed to be part of the
original factory design. Without the complete manual, attribution remains limited
to this archive and the scan's annotation.

Direct observations from the drawing:

- V1/V2 are 6AU6 pentodes with separately supplied screens, followed by V3A/B
  12AX7 triodes and V4A/B 12AU7 output triodes.
- V5 is a 6AL5 labelled for regulation. Its signal comes from the output section;
  it is distinct from the V6 6X4 power rectifier.
- A control-voltage path returns to the input stage. **Circuit interpretation:**
  this is output-derived gain regulation, rather than evidence for a particular
  fixed compression ratio or a reusable static gain-reduction curve.
- Input and output attenuation, a regulation control and transformer coupling
  appear on the drawing. These alone do not establish loaded frequency response.

Selected **published drawing annotations**, not measurements made in this work:

| Location on CA-1589 drawing | Annotation |
|---|---|
| V1/V2 plates | 188 V |
| V1/V2 screens | 24 V |
| V1/V2 cathode node | 0.5 V |
| V3 plates | 92 V |
| V3 cathodes | 0.7 V |
| Output-stage cathode node | 11 V |
| Supply markings | 260 V and 290 V |

The drawing says DC readings are relative to chassis ground unless otherwise
noted, with zero signal and the input control counterclockwise. Its voltmeter
loading is part of the stated conditions. Do not mistake these for voltages
relative to each tube's cathode or use them as verified FA-5238 bias points.

## Exact-unit specifications still unverified

| Property | Status |
|---|---|
| Original manufacturer, contract and serial-specific revision | Not verified from a readable exact-unit nameplate/manual |
| Input/output impedance and transformer ratios | No verified FA-5238 engineering values retrieved |
| Frequency response and phase | No verified FA-5238 sweep or specification retrieved |
| Gain and maximum input/output levels | Not established |
| THD, harmonic spectrum, noise | Not established under defined conditions |
| Compression law, knee, attack and recovery | Not established by the retrieved primary material |
| Exact bias points and circuit component values | Related CA-1589 annotations exist; FA-5238 correspondence unverified |
| Transformer magnetic/electrical characteristics | No exact-unit core, winding or measured nonlinear dataset retrieved |

Do not fill these gaps with AM-864/U data, other CA/FA variants, dealer adjectives,
or generic tube maxima. No universal bandwidth or timing figures are assigned.

## Remaining document leads

The owner discussion points to the legacy FAA report URL
[rd76183.pdf](https://www.tc.faa.gov/its/worldpac/techrpt/rd76183.pdf), describing it
as a VOR tube-availability study. The host timed out during retrieval. Its title,
report number, tables, equipment quantities and any FA-5238 tube inventory remain
**unverified here**. A report number must not be inferred solely from that URL.

The same discussions mention related schematics and service material. The
CA-1589 image above was obtained; a complete original manual specifically for
FA-5238 was not. Listings of other models and inaccessible attachments are leads,
not inspected evidence. No seller or owner was contacted.

## Modelling implications and next evidence to seek

The exact 6AU6WC military sheet calls it **sharp-cutoff**, while the retrieved
CA-1589 drawing uses 6AU6s as pentodes. Do not copy a 6386 remote-cutoff law or
assume triode connection. The drawing's low screen-voltage annotation also means
that the usual higher-screen-voltage datasheet operating point is not a direct
substitute for the circuit's behaviour. See the tube packet for the actual curves.

Research inference: a credible later prototype needs both plate and screen current
behaviour, bias-dependent gain, detector loading and the surrounding network.
Datasheets support tube elements; they do not identify a complete compressor.
Any statement about the eventual sound remains an untested listening hypothesis.

Highest-value remaining evidence, in order:

1. Exact FA-5238 nameplate, contract/serial information, tube-location chart and
   complete drawing/manual revision.
2. Verified correspondence to the CA-1589 network, including screen supply,
   detector connections, regulation network and transformer part numbers.
3. Curve data around the actual low screen voltage and control-bias excursion,
   including matched-pair spread and imbalance.
4. Defined hardware captures: loaded response/phase, level-dependent harmonics,
   static regulation and transient recovery at recorded control settings.

This handoff preserves the owner's direction: physically motivated parts, freely
combined, tuned by ear, without hardware-accuracy claims. It does not authorize
implementation or choose an architecture.
