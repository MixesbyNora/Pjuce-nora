# Manley Variable Mu — research handoff for Claude

Prepared for Beyond Analog / Lumiere. Research snapshot: 23 October 2026.

Companion: [tube datasheets, operating points and curve inventory](ManleyVariableMu-TubeData.md).

## Purpose and scope

This packet consolidates public documentation and measurement leads found in the
preceding research conversation. It is research input, not authorization to change
Lumiere's code, parameters, UI or architecture.

The owner's direction is physically motivated behaviour within swappable parts,
free combinations between parts, and tuning by ear, without hardware-accuracy
claims. Keep manufacturer specifications, independent measurements, and our own
inferences separate. Do not turn missing information into assumed circuit values.

No hardware measurements, schematic reconstruction, graph digitization or listening
assessment were performed while preparing this packet. Source PDFs remain at the
linked hosts; this is a summary and source index, not a reproduction of the manuals.

## 1. Published hardware specifications

The following are manufacturer claims for the standard Stereo Variable Mu, not
measurements of a particular unit. Source: [S1].

| Property | Published value / description |
|---|---|
| Compress mode | Approximately 1.5:1; soft knee |
| Limit mode | Approximately 4:1–20:1 |
| Standard attack | Continuously variable, 25–70 ms |
| Recovery / release | Five settings: 0.2, 0.4, 0.6, 4, 8 seconds |
| Maximum gain | 35 dB |
| Maximum output | +30 dBu, approximately 26 V RMS |
| Distortion | Less than 0.1% THD at 1 kHz; short listing does not fully specify operating conditions |
| Frequency response | Current product page advertises 20 Hz–25 kHz, ±0.1 dB |
| Input / output | Balanced; specified as 600 Ω |
| Noise | −85 dB typical; reference level and weighting are not clear in the short listing |

Interpretation limits:

- Treat attack/release figures as nominal control specifications. Their precise
  measurement convention and dependence on signal/history need verification.
- A THD figure does not identify individual harmonics, phase, level dependence or
  the stage responsible for distortion.
- Do not silently relabel the published noise number as dBu or dBFS.
- A nominal impedance specification is not a measured complex impedance curve.
- The response endpoint does not identify a filter order, pole location, leakage
  inductance, capacitance or resonance.

## 2. Circuit and tube information

Manley's manual explicitly describes the gain-control chain as a feedback circuit.
It describes a balanced variable-gain tube stage whose bias is changed by the
sidechain. This is a topology description, not a complete schematic. [S2, printed
pages 10 and 15]

The manufacturer's tube/version information identifies: [S1]

| Function / revision | Tube information |
|---|---|
| Standard gain-control stage | Two 5670s across the stereo unit |
| T-Bar gain-control modification | Four 6BA6s across the stereo unit |
| Amplification | Two 5751s or 12AX7s |
| Current output stage | Two 12BH7s; listed as production since April 2016 |
| Older output stage | Two 7044s or 5687s; serial-number qualification is on the product page |
| Sidechain rectification | Two 12AL5s |

The manuals also discuss earlier 6386-based units and the T-Bar substitution.
Consequently, “Manley Variable Mu” alone does not specify a unique tube/circuit
revision. [S2, printed page 12]

Modelling inference: gain-control tube type, output-stage revision, calibration,
loading and operating level are relevant variables to record before using any
hardware capture as a target. A tube designation alone does not establish its
operating point or its in-circuit transfer function.

## 3. Revision differences and documentation conflicts

These findings must remain separate rather than being merged into one target:

| Subject | Documentation difference |
|---|---|
| Attack | Standard specification: 25–70 ms [S1]. Mastering manual: approximately 15–90 ms in eleven steps [S2, printed page 11]. |
| Sidechain high-pass | Current product listing: −3 dB at 100 Hz [S1]. October 2023 manual: approximately −3 dB at 70 Hz, first order / 6 dB per octave [S2, printed page 14]. |
| Frequency-response tolerance | Current page: ±0.1 dB over 20 Hz–25 kHz [S1]. February 2004 manual: ±1 dB over 20 Hz–20 kHz [S3, printed page 14]. |
| Tube complement | Standard, T-Bar, original 6386, and older/current output-tube versions differ [S1, S2]. |

The sidechain discrepancy is unresolved here. It could reflect revisions or
inconsistent documentation; neither explanation was established. Verify the
specific hardware before choosing a detector filter target.

The PDFs' printed page numbers are used throughout this packet.

## 4. Manufacturer manual: useful material to inspect

The October 2023 manufacturer-authored manual is available from a distributor.
It covers regular and Mastering versions. [S2]

| Printed page | Relevant section |
|---|---|
| 10 | Operational notes, including feedback topology |
| 11 | Mastering controls and M/S modifications |
| 12 | T-Bar modification and tube history |
| 13 | Gain-reduction-curves section |
| 14 | Specifications |
| 15–16 | Technical notes and calibration information |

The gain-reduction-curves section is a lead for further inspection. No curves
were digitized, no numerical fit was obtained, and usable graph resolution was
not established in this research. Do not describe this packet as containing a
machine-readable manufacturer transfer-curve dataset.

## 5. Independent hardware-measurement study

**Author:** João Pedro Mendes de Oliveira.

**Title:** *Análise comparativa entre um compressor de áudio analógico e seus
simuladores digitais* (comparative analysis of an analog audio compressor and its
digital simulators).

**Date / type:** 2021 undergraduate mechatronics engineering thesis; public full
text on ResearchGate. [S4]

The study compares a physical Manley Variable Mu with software versions using
sine, white-noise, voice and music experiments. It includes waveform/envelope
plots, spectra and spectrograms. Its sine test uses a 1 kHz, −16 dBFS signal;
exported files are described as 44.1 kHz / 24-bit.

The author acknowledges that converter behaviour contributes to the hardware
captures, and that visually matching photographed knob settings introduces
uncertainty. These limitations matter when interpreting comparisons.

Use it as measured evidence for the particular tested setup. Do not assume it
provides an exhaustive multi-level/frequency characterization, exact physical
input/output calibration, or a general ranking of current software versions.
No underlying raw-WAV/CSV repository was verified during this research.

Suggested review questions — not established findings:

- Which hardware revision and calibration were used?
- What analog level corresponded to the digital test level?
- Is there a converter-only loopback reference?
- Can envelope and harmonic results be extracted with useful resolution?
- Are test settings sufficient to distinguish static transfer, timing and
  saturation effects, rather than combining them?

## 6. Archived component-level technical notes

A historical compilation of Manley material contains a 5670-to-6386 conversion
section. Its resistor substitutions include 3.57 kΩ to 6.8 kΩ, 1 kΩ to 511 Ω,
a balance trimmer change from 10 kΩ to 500 Ω, removal of a 200 Ω resistor, and
a 33 kΩ to 22 kΩ change. [S5, PDF page 107]

These are useful clues about a particular gain-control implementation. They are
not a complete circuit, not verified values for every production revision, and
not enough to infer a complete netlist or operating point. This packet is not
hardware modification guidance.

## 7. Information not established

The research did not locate or verify:

- A complete, revision-specific circuit schematic and bill of materials.
- A complete set of tube operating voltages, currents and loading conditions.
- Transformer turns ratios, winding resistances, magnetizing/leakage inductance,
  parasitic capacitance, or core dimensions/material characterization sufficient
  to identify the actual units.
- Transformer magnetic loops or fitted hysteresis parameters.
- A comprehensive harmonic/IMD dataset across input level, frequency, compression,
  drive and load, with documented analog calibration.
- A comprehensive measured attack/release surface across settings and signal
  histories.
- A public raw measurement archive suitable for independent re-analysis.

“Not located” does not mean the information cannot exist elsewhere or be obtained
from the manufacturer or a properly documented measurement session.

Inference for Lumiere: the available material can constrain a physically motivated
compression model and define questions for measurements. It cannot uniquely
identify the actual transformers, separate every source of distortion, or justify
a hardware-accuracy claim.

## 8. Source index

**[S1] Manley Laboratories — current Variable Mu product page**

[Open source](https://www.manley.com/products/pro-audio/dynamics/variable-mu)

Primary manufacturer source for published specifications and tube-version notes.
Dynamic page; values above reflect the research snapshot, not an independently
verified production-unit measurement.

**[S2] Manley Laboratories — owner’s manual, revision October 2023**

[Open source](https://www.ordioproductions.com.au/wp-content/uploads/Manley-Variable-MU-User-Manual.pdf)

Manufacturer-authored document hosted by a distributor. Use its actual title and
revision; automated browser extraction incorrectly labels its document title as
“Massive eManual Rev MSMPXxxxx.” The document itself identifies the Stereo Variable
Mu and October 2023 revision.

**[S3] Manley Laboratories — owner’s manual, revision February 26, 2004**

[Open source](https://static1.squarespace.com/static/582f5ce4e4fcb562b921bdc3/t/5f638fde828a553bcebb5b7d/1600360415169/VARIABLEMU%2Brev2-26-04.pdf)

Historical manufacturer document. Useful for revision comparison; do not apply
all historical values to current hardware.

**[S4] Oliveira — independent measurement thesis**

[Open source](https://www.researchgate.net/publication/363694427_Analise_comparativa_entre_um_compressor_de_audio_analogico_e_seus_simuladores_digitais)

Public full text. Access/download behaviour can depend on ResearchGate. The
researcher’s findings are distinct from manufacturer specifications.

**[S5] Archived compilation containing Manley technical material**

[Open source](https://audio.vn/wp-content/uploads/2017/10/MANLEY-2017.pdf#page=107)

Manufacturer material reproduced in a third-party compilation. The conversion
notes are historical and their applicability must be checked against a specific
revision.

## 9. Questions to resolve before any implementation proposal

1. Which reference is intended: standard 5670, T-Bar, original 6386, or a freely
   designed module informed by more than one?
2. Is the immediate objective the compression law/timing, tube-stage behaviour,
   transformer behaviour, or their interaction?
3. Which observations are supported directly, which can be measured, and which
   remain tuning choices for the owner?
4. Which missing values would materially affect the proposed model, rather than
   merely making a schematic look complete?

Keep this research separate from the existing shipped plugin. Any implementation
needs its own authorized scope and must preserve the project's established
realtime safety, parameter/state compatibility and validation requirements.
