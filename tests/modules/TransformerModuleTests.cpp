#include <modules/TransformerModule.h>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace ModuleSpike;

TEST_CASE ("Experimental transformer variants obey reset partition stereo and latency contracts", "[module-transformer]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto variant : { Variant::smooth, Variant::play3 })
            for (auto topology : { Topology::shunt, Topology::series })
            {
                CAPTURE (rate, static_cast<int> (variant), static_cast<int> (topology));
                TransformerModule a, b;
                REQUIRE (a.prepare ({ rate, 4096, 2 })); REQUIRE (b.prepare ({ rate, 4096, 2 }));
                TransformerParameters p; p.driveDb = 24;
                a.configure (variant, topology, p); b.configure (variant, topology, p);
                std::array<double, 4096> left {}, right {}, split {}, zero {};
                for (std::size_t n = 0; n < left.size(); ++n) left[n] = std::sin (static_cast<double> (n) * 0.071) * 15.0;
                left[3] = std::numeric_limits<double>::quiet_NaN();
                left[5] = std::numeric_limits<double>::infinity();
                left[7] = std::numeric_limits<double>::max();
                split = left;
                a.process ({ { left.data(), right.data() }, left.size(), 2 });
                for (std::size_t offset = 0; offset < split.size(); ++offset)
                    b.process ({ { split.data() + offset, zero.data() + offset }, 1, 2 });
                CHECK (left == split);
                CHECK (right == zero);
                for (auto sample : left) CHECK (isFinite (sample));
                for (auto sample : right) CHECK (std::abs (sample) < 1.0e-15);
                CHECK (a.circuits[0].diagnostics.failures == 0);
                CHECK (a.circuits[0].diagnostics.maximumResidual <= TransformerCircuit::tolerance);
                a.reset(); b.reset();
                left = {}; right = {}; left[0] = 0.1; split = left;
                a.process ({ { left.data(), right.data() }, left.size(), 2 });
                b.process ({ { split.data(), zero.data() }, split.size(), 2 });
                CHECK (left == split);
                CHECK (a.info().latency == 0);
                CHECK (std::abs (left[0]) > 1.0e-10);
                Chain chain;
                REQUIRE (chain.prepare ({ rate, 4096, 2 }));
                REQUIRE (chain.select (0, &a));
                REQUIRE (chain.select (0, nullptr));
                CHECK (chain.latencySamples() == 64);
            }
}

TEST_CASE ("Experimental high-rate quiet tails match independent linear circuit recurrence", "[module-transformer]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto variant : { Variant::smooth, Variant::play3 })
            for (auto topology : { Topology::shunt, Topology::series })
            {
                TransformerParameters p;
                TransformerCircuit circuit; circuit.prepare (rate * 512, variant, topology, p);
                const auto dt = 0.5 / (rate * 512);
                const auto rs = p.sourceOhms / 1000 + p.loss, rl = p.loadOhms / 1000;
                const auto a = topology == Topology::shunt ? 1 + rs / rl : 1;
                const auto b = topology == Topology::shunt ? rs : rs + rl;
                const auto inductance = TransformerCircuit::fluxScale * (0.1 + p.saturation / p.knee * (variant == Variant::smooth ? 1 : 1 - p.memory));
                const auto pole = (inductance - dt * b / a) / (inductance + dt * b / a);
                const auto forcing = dt / a / (inductance + dt * b / a);
                double current = 0, previous = 0, error = 0;
                for (int n = 0; n < 4096; ++n)
                {
                    const auto x = n < 64 ? 0.0001 : 0.0;
                    current = pole * current + forcing * (x + previous); previous = x;
                    const auto expected = topology == Topology::shunt ? (x - rs * current) / a : rl * current;
                    error = std::max (error, std::abs (circuit.process (x) - expected));
                }
                CAPTURE (rate, static_cast<int> (variant), static_cast<int> (topology), error);
                CHECK (error < 1.0e-12); CHECK (circuit.diagnostics.failures == 0);
            }
}
