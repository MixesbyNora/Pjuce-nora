#pragma once

#include "Voicing.h"

// Output tube workbench: physical values of the 12BH7 cathode follower that
// drives the VTB1148 (docs/TubeStage.md). Defaults are the coupling-test design.
namespace TubeVoicing
{
using Descriptor = Voicing::Descriptor;
struct Schema
{
    // Version 2 (2026-10-24): Wiring added, coupling default 220 uF (docs/Overdrive.md).
    // Version 3 (2026-10-24): Output grid added; version 2 reads with it at Ideal.
    static constexpr int version = 3, readableSince = 2;
    static constexpr const char* model = "lumiere-12bh7-follower-workbench-v1";
    static constexpr std::array<const char*, 0> retiredModels {};
    static constexpr std::array descriptors {
        // Quiescent current per section. Less current raises the output resistance:
        // the tube leans harder on the iron and cuts off sooner on bass peaks.
        Descriptor { "plateMa", "Plate current", "mA", 3.0, 20.0, 0.25, 10.0, 1.0 },
        // Coupling capacitor to the primary; small values resonate with its inductance.
        // Large values resonate chaotically with a deeply saturated core (ferroresonance).
        Descriptor { "couplingUf", "Coupling cap", "uF", 22.0, 1000.0, 1.0, 220.0, 0.35 },
        // Plate supply. The cathode sits at half of it, so it sets the swing headroom.
        Descriptor { "supplyVolts", "Supply (B+)", "V", 200.0, 400.0, 5.0, 300.0, 1.0 },
        // Single-ended only: one 12BH7 section, or both in parallel (half the output resistance).
        Descriptor { "sections", "Sections", "", 1.0, 2.0, 1.0, 2.0, 1.0 },
        // Push-pull: a White cathode follower that pushes and pulls current, one
        // section each. Single-ended: a plain follower that can only sink its idle current.
        Descriptor { "wiring", "Wiring", "", 0.0, 1.0, 1.0, 0.0, 1.0, "Push-pull|Single-ended" },
        // The (upper) grid's drive from the 5751 (owner decision 2026-10-24, docs/Overdrive.md,
        // "Output grid current"). Ideal: a voltage source, no grid current. DC-coupled:
        // the 5751's plate resistance and grid current, which clamps the hardest peaks.
        // Capacitor: the same through 0.1 uF into a 1 MOhm leak, so grid current also
        // biases the tube colder after hits and it recovers (blocking). Only extreme
        // overdrive changes, and it gets grittier.
        Descriptor { "outputGrid", "Output grid", "", 0.0, 2.0, 1.0, 0.0, 1.0, "Ideal|DC-coupled|Capacitor" }
    };
};
inline constexpr std::size_t plateCurrent = 0, coupling = 1, supply = 2, sections = 3, wiring = 4, outputGrid = 5;
using Word = Voicing::Word;
using Values = Voicing::Values<Schema>;
inline double dspValue (Values values, std::size_t i) noexcept { return Voicing::dspValue<Schema> (values, i); }
inline Word encode (Values values) noexcept { return Voicing::encode<Schema> (values); }
inline Values decode (Word word) noexcept { return Voicing::decode<Schema> (word); }
inline Values sanitized (Values values) noexcept { return Voicing::sanitized<Schema> (values); }
inline bool equal (Values a, Values b) noexcept { return Voicing::equal<Schema> (a, b); }
}
