#include <modules/ModuleContract.h>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <limits>

using namespace ModuleSpike;

TEST_CASE ("Module contract: delayed Off is exact, ideal and fixed across selections", "[module-contract]")
{
    Off off;
    Chain chain;
    REQUIRE (chain.prepare ({ 48000.0, 4096, 2 }));
    REQUIRE (chain.select (0, &off));
    CHECK (chain.latencySamples() == Chain::slots * Chain::slotDelay);
    CHECK (off.info().input.kind == Impedance::Kind::ideal);
    CHECK (off.info().output.kind == Impedance::Kind::ideal);
    CHECK (off.info().latency == 0);
    CHECK (std::bit_cast<std::uint64_t> (off.info().tailSamples) == 0);
    std::array<double, 400> left {}, right {}, original {};
    for (std::size_t n = 0; n < left.size(); ++n)
        original[n] = left[n] = n % 2 == 0 ? -0.0 : static_cast<double> (n) / 100;
    right = left;
    chain.process ({ { left.data(), right.data() }, left.size(), 2 });
    for (std::size_t n = static_cast<std::size_t> (chain.latencySamples()); n < left.size(); ++n)
    {
        CHECK (std::bit_cast<std::uint64_t> (left[n]) == std::bit_cast<std::uint64_t> (original[n - static_cast<std::size_t> (chain.latencySamples())]));
        CHECK (std::bit_cast<std::uint64_t> (right[n]) == std::bit_cast<std::uint64_t> (left[n]));
    }
    CHECK (chain.select (0, nullptr));
    CHECK (chain.latencySamples() == Chain::slots * Chain::slotDelay);
    chain.reset();
    left = {}; right = {}; left[0] = 1.0;
    chain.process ({ { left.data(), right.data() }, left.size(), 2 });
    CHECK (left[static_cast<std::size_t> (chain.latencySamples())] == 1.0);
    for (auto x : right) CHECK (x == 0.0);
}

TEST_CASE ("Module contract: block partition, reset and invalid input policies", "[module-contract]")
{
    std::array<double, 4096> input {}, whole {}, split {}, right {};
    for (std::size_t n = 0; n < input.size(); ++n) input[n] = std::sin (0.123 * static_cast<double> (n));
    input[2] = std::numeric_limits<double>::infinity();
    input[9] = std::numeric_limits<double>::quiet_NaN();
    input[14] = std::numeric_limits<double>::max();
    Chain a, b;
    REQUIRE (a.prepare ({ 44100.0, 4096, 1 }));
    REQUIRE (b.prepare ({ 44100.0, 4096, 1 }));
    whole = split = input;
    a.process ({ { whole.data(), nullptr }, whole.size(), 1 });
    for (std::size_t start = 0; start < split.size(); start += 127)
        b.process ({ { split.data() + start, nullptr }, std::min (std::size_t { 127 }, split.size() - start), 1 });
    CHECK (whole == split);
    for (auto x : whole) CHECK (isFinite (x));
    a.reset(); split = input;
    a.process ({ { split.data(), right.data() }, split.size(), 1 });
    CHECK (whole == split);
    CHECK_FALSE (a.prepare ({ 0.0, 1, 1 }));
    CHECK_FALSE (a.prepare ({ 48000.0, 0, 1 }));
    CHECK_FALSE (a.prepare ({ 48000.0, 4096, 3 }));
}

TEST_CASE ("Module contract: optional impedance never injects nonfinite gain", "[module-contract]")
{
    CHECK (divider ({}, {}) == 1.0);
    for (double z : { -1.0, 0.0, 1.0e-300, 1.0, 1.0e300, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
        for (double r : { 0.0, 1.0, 1.0e300 })
        {
            const auto gain = divider (Impedance::scalar (r), Impedance::scalar (z));
            CHECK (isFinite (gain));
            CHECK (gain >= 0.0);
            CHECK (gain <= 1.0);
        }
    CHECK (divider (Impedance::scalar (100), Impedance::scalar (100)) == 0.5);
}
