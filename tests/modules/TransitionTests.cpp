#include "ResearchHarness.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iomanip>

using namespace ModuleResearch;

TEST_CASE ("Experimental regions measure latency and exact Off across partitions", "[module-latency]")
{
    std::ofstream report;
    if (const auto* root = std::getenv ("LUMIERE_MODULE_REPORT_DIR"))
    {
        report.open (std::string (root) + "/latency.csv");
        report << "kind,rate_hz,channels,factor,partition,reported_samples,identity_centroid_samples,off_exact\n" << std::setprecision (17);
    }
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
            for (int factor : { 1, 2, 4, 8 })
                for (int partition : { 1, 127, 4096 })
                {
                    Processor p (rate, factor, channels, Variant::smooth, Topology::shunt);
                    p.identity = true;
                    juce::AudioBuffer<double> block (channels, partition);
                    double sum = 0, moment = 0;
                    for (int start = 0; start < 8192; start += partition)
                    {
                        block.clear(); if (start == 0) block.setSample (0, 0, 1);
                        p.process (block);
                        for (int n = 0; n < partition; ++n) { const auto x = block.getSample (0, n); sum += x; moment += (start + n) * x; }
                    }
                    const auto measured = moment / sum;
                    CHECK (std::abs (measured - p.latency()) < 1.0e-7);
                    p.off = true; p.reset();
                    std::array<double, 512> expected {};
                    const auto delay = static_cast<std::size_t> (std::llround (p.latency()));
                    std::size_t position = 0; bool exact = true;
                    for (int start = 0; start < 8192; start += partition)
                    {
                        for (int c = 0; c < channels; ++c)
                            for (int n = 0; n < partition; ++n) block.setSample (c, n, (start + n) % 3 == 0 ? -0.0 : 0.25 * std::sin ((start + n) * 0.1));
                        std::vector<double> reference (static_cast<std::size_t> (partition));
                        for (int n = 0; n < partition; ++n)
                        {
                            const auto x = block.getSample (0, n);
                            reference[static_cast<std::size_t> (n)] = delay > 0 ? expected[position] : x;
                            if (delay > 0) { expected[position] = x; position = (position + 1) % delay; }
                        }
                        p.process (block);
                        for (int c = 0; c < channels; ++c)
                            for (int n = 0; n < partition; ++n) exact &= std::bit_cast<std::uint64_t> (block.getSample (c, n)) == std::bit_cast<std::uint64_t> (reference[static_cast<std::size_t> (n)]);
                    }
                    CHECK (exact);
                    const auto initialLatency = p.latency();
                    for (auto v : { Variant::smooth, Variant::play3 })
                        for (auto t : { Topology::shunt, Topology::series })
                            for (auto c : { Coupling::buffered, Coupling::divider, Coupling::loadAware, Coupling::reference })
                            {
                                p.configure (v, t, {}, c, 1000);
                                CHECK (std::abs (initialLatency - p.latency()) < 1.0e-12);
                            }
                    if (report.is_open()) report << "measurement," << rate << ',' << channels << ',' << factor << ',' << partition << ',' << p.latency() << ',' << measured << ',' << exact << '\n';
                }
}

TEST_CASE ("Experimental switches retain finite output and report excess sample steps", "[module-transition]")
{
    std::ofstream report;
    if (const auto* root = std::getenv ("LUMIERE_MODULE_REPORT_DIR"))
    {
        report.open (std::string (root) + "/swaps.csv");
        report << "kind,rate_hz,event,reported_latency,maximum_step,control_maximum_step,maximum_excess_step\n" << std::setprecision (17);
    }
    constexpr double pi = juce::MathConstants<double>::pi;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int event = 0; event < 5; ++event)
        {
            const auto coupling = event >= 2 ? Coupling::buffered : Coupling::transformer;
            Processor p (rate, 8, 2, Variant::smooth, Topology::shunt, {}, coupling);
            Processor control (rate, 8, 2, Variant::smooth, Topology::shunt, {}, coupling);
            juce::AudioBuffer<double> a (2, capacity), b (2, capacity);
            double previous = 0, previousControl = 0, maximum = 0, controlMaximum = 0, excess = 0;
            for (int start = 0; start < 8192; start += capacity)
            {
                if (start == 4096)
                {
                    if (event == 0) p.configure (Variant::play3, Topology::shunt, {}, coupling, 1000);
                    if (event == 1) p.configure (Variant::smooth, Topology::series, {}, coupling, 1000);
                    if (event == 2) p.configure (Variant::smooth, Topology::shunt, {}, Coupling::divider, 1000);
                    if (event == 3) p.configure (Variant::smooth, Topology::shunt, {}, coupling, 100000);
                    if (event == 4) p.off = true;
                }
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < capacity; ++n)
                    {
                        const auto x = 0.8 * std::sin (2 * pi * (c == 0 ? 60 : 73) * (start + n) / rate);
                        a.setSample (c, n, x); b.setSample (c, n, x);
                    }
                p.process (a); control.process (b);
                for (int n = 0; n < capacity; ++n)
                {
                    const auto x = a.getSample (0, n), y = b.getSample (0, n);
                    const auto step = std::abs (x - previous), referenceStep = std::abs (y - previousControl);
                    maximum = std::max (maximum, step); controlMaximum = std::max (controlMaximum, referenceStep);
                    if (start >= 4096) excess = std::max (excess, step - referenceStep);
                    CHECK (isFinite (x)); CHECK (isFinite (a.getSample (1, n)));
                    previous = x; previousControl = y;
                }
            }
            if (report.is_open()) report << "measurement," << rate << ',' << event << ',' << p.latency() << ',' << maximum << ',' << controlMaximum << ',' << excess << '\n';
        }
}
