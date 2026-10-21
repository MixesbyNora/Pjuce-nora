# Project Lumiere: product vision

Captured from the owner's [original notes](notes/2026-10-20-project-lumiere-ideas.md)
and subsequent model-count/bypass, USER sidechain EQ, Model selector and analog
component-view clarifications on 2026-10-20. This document organizes the intended
product direction. It does not describe completed features, establish a release schedule or authorize their
implementation. Current behavior and development status remain in [SPEC.md](SPEC.md).

## Beyond Analog

Project Lumiere should demonstrate the Beyond Analog philosophy: use DSP's
freedom to offer combinations and independent control that a single physical
compressor cannot conveniently provide. Analog-inspired behavior supplies the
sonic building blocks; the user chooses how to combine them.

The aim is an accessible compressor that can be configured deeply, with useful
choices presented clearly rather than requiring knowledge of circuit design.

## Two related products

The planned family consists of two separate plugins built on a shared framework:

| Product direction | Intended focus |
|---|---|
| Tube compressor | The current Lumiere project develops toward a tube/vari-mu-inspired product, with selectable tube coloration and transformer models. |
| Solid-state compressor | A separate future product, exploring selectable solid-state compression characteristics and transistor, VCA and op-amp-inspired coloration/building blocks. |

These are product directions, not a statement that the current processor already
models those circuits. Final product names, identifiers, packaging and the exact
shared-code boundary are undecided. The existing `Lumiere` project and plugin
identity remain unchanged.

## Independent sonic choices

The intended user-facing capabilities are:

- Choose the input transformer model independently of the output transformer model.
- Select tube coloration in the tube product, or appropriate solid-state building
  blocks in the solid-state product.
- Disable coloration stages independently, including operating without tube
  coloration, transformer coloration, or both.
- Choose compression characteristics independently of the coloration choices.
  Changing a saturation model should not require adopting its associated
  hardware's complete compressor behavior.
- Choose a **Box Tone** frequency-response curve as another sonic option.

The notes deliberately allow combinations that would be impractical in hardware.
They also distinguish coloration from the functional role a physical component
plays in an actual compressor. A tube compression characteristic and an optional
tube coloration stage therefore need separate definitions in the eventual DSP
design; their controls must have clear meanings.

The hardware examples in the notes include a blue-stripe 1176, SSL G bus/channel
compressors and the Distressor. They record creative combinations and tonal
references, not verified component inventories or established emulation targets.
Specific component identities and any hardware-accuracy claims need supporting
research or measurements before entering a model specification.

## Tube product: model groups

The owner has specified five independently selectable groups for the tube
product. Each group offers three models plus an explicit **Off** option:

| Group | Model choices | Independent Off behavior |
|---|---|---|
| Tube saturation | Three tube coloration models. | Disable tube saturation while retaining the other selected groups. |
| Compression circuit | Three distinct models, each based on a famous vari-mu compression circuit, including its remote-cutoff tube behavior as appropriate. | Disable compression while retaining the selected coloration and other enabled groups. |
| Input transformer | Three input transformer models. | Disable the input transformer model independently of the output transformer. |
| Output transformer | Three output transformer models. | Disable the output transformer model independently of the input transformer. |
| PSU | Three power-supply models. | Disable modeled power-supply behavior while retaining the other enabled groups. |

Any selection in one group can be combined with any selection in every other
group. No compression circuit forces a matching saturation tube, transformer or
PSU. All five groups can also be Off simultaneously. This is intentional DSP
freedom: disabling a modeled component removes its contribution rather than
requiring the corresponding physical circuit to remain operable. In particular,
PSU Off means bypassing the supply model, not silencing the plugin.

Tube saturation and the compression circuit are separate choices even when
their inspirations share tube technology. Compression Off must therefore allow
the remaining enabled coloration models to operate without gain reduction.
Turning all five groups Off disables those five contributions; behavior of
other controls such as Box Tone, gain and monitoring remains separately defined.

Specific tube types, circuit references, transformer identities and PSU models
have not yet been selected. PSU characteristics, coupling, ordering, switching
transitions and exact bypass contracts still need design and validation. These
counts apply to the tube product; equivalent counts for the solid-state product
have not been specified. Mixing and matching here means choosing across groups;
continuous blending between models within a group is not yet specified.

## Model selector and Easy/Expert workflow

Provide a **Model** selector for users who want a quick starting point or who
find the individual component choices overwhelming. The proposed presentation
uses **Easy** and **Expert** modes:

| Workflow | Component selection |
|---|---|
| Easy | Offer approximately 3-6 curated models. Selecting one applies a preset combination of components in the background. |
| Expert | Expose the individual component selectors so the user chooses each group's model or Off state independently. |

An Easy-mode model is a component configuration preset, not a separate
compression engine. For the tube product it selects a combination across tube
saturation, compression circuit, input transformer, output transformer and PSU.
The same underlying component choices remain available in Expert mode, including
all independent Off options. The curated model count is separate from the three
models offered inside each component group.

This selector concerns component combinations. Whether a curated model also
sets Box Tone or any ordinary operating controls needs explicit specification;
it does not currently define a whole-plugin preset recall operation.

The exact curated lineup, names and final count within the 3-6 range remain
undecided. Easy/Expert is a proposed way to present the workflow; the exact mode
control and layout remain design work. Before implementation, define how
switching views preserves a user's configuration, how a custom Expert combination
appears in Easy mode, and which changes occur only when a new Model is selected.
These policies must avoid silently replacing a user's custom combination.

Workflow depth is a separate design consideration from analog versus modern
visual presentation. No Model selector or Easy/Expert UI is implemented by this
documentation update.

## USER sidechain EQ

Keep all seven existing sidechain choices and their current preset behavior:
**Off**, **HP 80 Hz**, **HP 120 Hz**, **Tilt Wide**, **Tilt Focused**,
**Band 730 Hz** and **Custom**. Add a separate future choice named **USER**.
The existing Custom mode remains its fixed preset; USER is the editable mode.

Selecting USER opens an interactive EQ response graph in a panel at the bottom
of the plugin. FabFilter Pro-Q 3 is the owner's visual reference for the graph
and band-editing presentation. The exact layout and styling remain design work.

Eight bands are provided:

| Bands | Available filter types |
|---|---|
| One dedicated cut band | High-pass (HP) |
| One dedicated cut band | Low-pass (LP) |
| Six remaining bands | Each independently selectable as a peak, low shelf or high shelf |

The user can shape a custom detector response with these eight bands. Filtering
applies only to the detector's copy of the signal, preserving the sidechain-only
role of the existing modes. The output audio is affected through the resulting
compression, not by passing through this EQ.

Band ranges, defaults, cut slopes, Q/resonance behavior, band enable controls and
panel sizing/closing behavior still need specification. The visual reference
does not select a DSP topology or add a spectrum analyzer, dynamic EQ or other
Pro-Q features to this requirement. The new panel must fit alongside the
existing bottom IN/OUT meters without obscuring them.

Adding USER must preserve the meaning of existing saved settings and automation.
The parameter/state migration needs design before implementation: retaining old
choice indices alone does not preserve normalized automation when a choice
range grows. No host parameter or existing sidechain mode changes in this
documentation update.

## Presentation choices

The vision includes an analog-style interface, a modern interface, and a modern
view with graphical compression feedback. Controls should remain accessible and
easy to use across those presentations.

### Analog GUI: open-top component view

Normal use shows the unit's front faceplate, in the familiar style of an analog
hardware emulation. A **component selector switch** changes the view to the top
of the unit, with its top plate opened to expose the modeled components. This
open-top view is where the analog GUI presents component selection.

The owner calls this a **visual gimmick**: a deliberately playful way to explore
the component combinations. Components that are turned Off literally disappear
from the illustrated unit. Turning every interchangeable component Off leaves
all of those components absent from that view. The illustration follows
the selected component configuration, including combinations that would not
form an operable physical compressor.

This presentation adds fun and visual feedback; it does not introduce an extra
sonic effect. Toggling the view alone must not change the selected components or
audio processing. Exact artwork, selection gestures, transition animation and
interaction with Easy/Expert mode remain design work.

### Modern GUI: component dropdowns

The modern interface uses straightforward component dropdown menus. It does not
use the open-top reveal. Both presentations operate on the same underlying
component configuration and independent Off states; changing visual presentation
must preserve that configuration.

Exact layouts and graphs remain design work. The existing generic editor,
meters and development panels remain the current implementation.

## Decisions still needed

- Which specific models fill the three slots in each tube-product group, and
  what qualifies a model for inclusion? What is the solid-state product's lineup?
- How should the independently selectable groups interact sonically while
  preserving their independent Off controls? Which PSU behaviors are modeled?
- What does disabling each stage preserve: gain, frequency response, latency and
  magnetic/dynamic state? These contracts need explicit definitions and tests.
- Where does Box Tone sit, and how does it differ from a model's own frequency
  response? Which curves are included?
- Which 3-6 curated component combinations appear in the Model selector, and
  what are their names? Do they include Box Tone or any operating settings?
- How do Easy/Expert switching, custom-combination display and state recall
  preserve the user's choices? Which non-component controls appear in each view?
- How does the analog component view interact with Easy/Expert mode, and what
  artwork, selection gestures and view transitions make it clear and enjoyable?
- What are the USER EQ band's ranges/defaults, cut slopes and enable behavior,
  and how does its bottom panel coexist with the meters and other views?
- How will future model choices preserve automation, saved projects, CPU budgets
  and latency consistency?

These questions are implementation planning prompted by the notes; they are not
additional features selected by the owner.

## Relationship to current work

The modular vision supports continuing to keep DSP components separate and
centralizing tuning values. It does not expand the current milestone into a
model selector, a second plugin, new routing, Box Tone, USER sidechain EQ or a
final custom UI.
Existing deferred features remain deferred until explicitly scheduled.

The live transformer remains the current tanh/residual-ADAA stage with the saved
favorite voicing. The magnetic model remains offline research. Its recent
[artifact refinement](MagneticArtifactRefinement.md) validates numerical behavior,
not a calibrated hardware transformer. On 2026-10-21 development resumed with a
Tests-only [audio-in/audio-out coupling prototype](TransformerAudioPrototype.md),
complete-chain measurements and level/latency-matched listening comparisons.
That work does not implement the future product features recorded here.

### Magnetic engine milestone (2026-10-21)

The voltage-driven magnetic transformer replaces the earlier tanh/ADAA stage in live formats, Standalone and current offline measurements. This is one uncalibrated transformer model, with fixed internal oversampling and reported latency; it does not implement the future interchangeable component families or a named hardware fit. See [current magnetic implementation](MagneticTransformerLive.md).
