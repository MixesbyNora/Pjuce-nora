#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

// Contract experiment. Only TransformerModule.h (variant C, the live VTB1148
// output stage) is used by shipped targets; the rest remains Tests-only.
namespace ModuleSpike
{
inline bool isFinite (double x) noexcept
{
    return (std::bit_cast<std::uint64_t> (x) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
inline double sanitized (double x) noexcept { return isFinite (x) ? x : 0.0; }
inline double bounded (double x, double lo, double hi, double fallback) noexcept
{
    return isFinite (x) ? std::clamp (x, lo, hi) : fallback;
}
struct Spec
{
    double sampleRate = 48000.0;
    std::size_t maxBlock = 4096;
    int channels = 2;
    bool valid() const noexcept { return isFinite (sampleRate) && sampleRate >= 8000 && sampleRate <= 100000000 && maxBlock > 0 && maxBlock <= 4096 && channels >= 1 && channels <= 2; }
};
struct Block
{
    std::array<double*, 2> data {};
    std::size_t samples = 0;
    int channels = 1;
};
struct ControlBlock { std::span<const double> input; std::span<double> output; };
struct Modulation { double supplyRail = 1.0; };
struct Descriptor
{
    const char* id; const char* label; const char* unit;
    double minimum, maximum, step, defaultValue, skew;
};
struct Impedance
{
    enum class Kind { ideal, scalar, table };
    Kind kind = Kind::ideal;
    double ohms = 0.0;
    // Reserved metadata only: no time-domain table interpretation is implied.
    struct Point { double frequency = 0, real = 0, imaginary = 0; };
    std::array<Point, 8> points {};
    std::size_t count = 0;
    static Impedance scalar (double value) noexcept
    {
        if (! isFinite (value) || value < 0) return {};
        Impedance result; result.kind = Kind::scalar; result.ohms = value; return result;
    }
};
// Ideal tags mean no inter-stage loading, avoiding infinity in DSP arithmetic.
// Table metadata is unsupported by this scalar experiment, hence ideal fallback.
inline double divider (const Impedance& source, const Impedance& load) noexcept
{
    if (source.kind != Impedance::Kind::scalar || load.kind != Impedance::Kind::scalar) return 1.0;
    const auto a = source.ohms, b = load.ohms;
    if (! isFinite (a) || ! isFinite (b) || a < 0 || b < 0) return 1.0;
    if (a <= 0) return 1.0;
    if (b <= 0) return 0.0;
    return a > b ? (b / a) / (1.0 + b / a) : 1.0 / (1.0 + a / b);
}
struct Info
{
    const char* partId = "off";
    const char* version = "1";
    int latency = 0;
    double tailSamples = 0;
    bool nonlinear = false;
    int preferredFactor = 1;
    // A kernel receives the actual processing rate. Region owns all resampling.
    std::span<const Descriptor> parameters;
    Impedance input {}, output {};
};
class Module
{
public:
    virtual ~Module() = default;
    virtual bool prepare (Spec) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void process (Block, Modulation = {}) noexcept = 0;
    virtual Info info() const noexcept = 0;
};
class ControlModule
{
public:
    virtual ~ControlModule() = default;
    virtual bool prepare (Spec) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void processControl (ControlBlock, Modulation = {}) noexcept = 0;
    virtual Info info() const noexcept = 0;
};
class Off final : public Module
{
public:
    bool prepare (Spec spec) noexcept override { return spec.valid(); }
    void reset() noexcept override {}
    void process (Block block, Modulation = {}) noexcept override
    {
        for (int c = 0; c < block.channels; ++c)
            for (std::size_t n = 0; n < block.samples; ++n)
                block.data[static_cast<std::size_t> (c)][n] = sanitized (block.data[static_cast<std::size_t> (c)][n]);
    }
    Info info() const noexcept override { return {}; }
};
// Non-owning fixed slots; caller keeps modules alive. Transport delays live here,
// not in Off. No loading is implicitly inferred from the optional metadata.
class Chain
{
public:
    static constexpr std::size_t slots = 2;
    static constexpr int slotDelay = 32;
    bool prepare (Spec value) noexcept
    {
        if (! value.valid()) return false;
        spec = value;
        for (auto* module : selected)
            if (module && (! module->prepare (spec) || module->info().latency < 0 || module->info().latency > slotDelay)) return false;
        reset(); return true;
    }
    bool select (std::size_t slot, Module* module) noexcept
    {
        if (slot >= slots || (module && (! module->prepare (spec) || module->info().latency < 0 || module->info().latency > slotDelay))) return false;
        selected[slot] = module; delay[slot] = {}; positions[slot] = 0; return true;
    }
    void reset() noexcept
    {
        delay = {}; positions = {};
        for (auto* module : selected) if (module) module->reset();
    }
    int latencySamples() const noexcept { return static_cast<int> (slots) * slotDelay; }
    void process (Block block, Modulation modulation = {}) noexcept
    {
        if (block.channels != spec.channels || block.channels < 1 || block.channels > 2) return;
        for (auto* pointer : block.data) if (block.samples > 0 && pointer == nullptr && block.channels == 2) return;
        if (! block.data[0]) return;
        for (std::size_t offset = 0; offset < block.samples; offset += spec.maxBlock)
        {
            Block part { { block.data[0] + offset, block.channels > 1 ? block.data[1] + offset : nullptr }, std::min (spec.maxBlock, block.samples - offset), block.channels };
            off.process (part);
            for (std::size_t slot = 0; slot < slots; ++slot)
            {
                if (selected[slot]) selected[slot]->process (part, modulation);
                const auto length = static_cast<std::size_t> (slotDelay - (selected[slot] ? selected[slot]->info().latency : 0));
                if (length == 0) continue;
                for (std::size_t n = 0; n < part.samples; ++n)
                {
                    auto& position = positions[slot];
                    for (int c = 0; c < part.channels; ++c)
                    {
                        const auto ch = static_cast<std::size_t> (c);
                        const auto x = part.data[ch][n];
                        part.data[ch][n] = delay[slot][ch][position];
                        delay[slot][ch][position] = sanitized (x);
                    }
                    position = (position + 1) % length;
                }
            }
        }
    }
private:
    Spec spec;
    Off off;
    std::array<Module*, slots> selected {};
    std::array<std::array<std::array<double, slotDelay>, 2>, slots> delay {};
    std::array<std::size_t, slots> positions {};
};
}
