#include "OfflineCoupledReference.h"
#include <catch2/catch_test_macros.hpp>

using namespace ModuleSpike;

TEST_CASE ("Source test fixture contract and scalar two-way reference stay finite", "[module-coupling]")
{
    SourceFixture source;
    REQUIRE (source.prepare ({ 48000, 4096, 2 }));
    CHECK (source.info().input.kind == Impedance::Kind::ideal);
    CHECK (source.info().output.kind == Impedance::Kind::scalar);
    CHECK (source.info().latency == 0);
    std::array<double, 32> left {}, right {};
    left[0] = 0.8;
    source.process ({ { left.data(), right.data() }, left.size(), 2 });
    CHECK (std::abs (left[0]) > 0);
    for (auto sample : right) CHECK (std::abs (sample) < 1.0e-15);
    for (auto v : { Variant::smooth, Variant::play3 })
        for (auto t : { Topology::shunt, Topology::series })
            for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (double resistance : { 10.0, 1000.0, 100000.0 })
                {
                    CAPTURE (static_cast<int> (v), static_cast<int> (t), rate, resistance);
                    OfflineCoupledReference a, b;
                    TransformerParameters p;
                    a.prepare (rate * 64, v, t, p, resistance);
                    b.prepare (rate * 64, v, t, p, resistance);
                    bool finite = true, repeatable = true;
                    for (int n = 0; n < 4096; ++n)
                    {
                        const auto x = n < 2048 ? 16.0 : -16.0;
                        const auto y = a.process (x);
                        finite &= isFinite (y);
                        repeatable &= std::bit_cast<std::uint64_t> (y) == std::bit_cast<std::uint64_t> (b.process (x));
                    }
                    CHECK (finite); CHECK (repeatable);
                    CHECK (a.diagnostics.failures == 0);
                    CHECK (a.diagnostics.maximumResidual <= OfflineCoupledReference::tolerance);
                }
}
