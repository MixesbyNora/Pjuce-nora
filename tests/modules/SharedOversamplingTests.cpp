#include "ResearchHarness.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>

namespace
{
using namespace ModuleResearch;
// Trivial second nonlinear stage, experiment only. Allocation in construction.
class ShaperRegion
{
public:
    explicit ShaperRegion (int factor) { MagneticAudio::prepareFilters (filters, factor); }
    void process (juce::AudioBuffer<double>& audio) noexcept
    {
        juce::dsp::AudioBlock<double> base (audio); auto high = filters.processSamplesUp (base);
        for (std::size_t c = 0; c < high.getNumChannels(); ++c)
            for (std::size_t n = 0; n < high.getNumSamples(); ++n)
                high.setSample (static_cast<int> (c), static_cast<int> (n), std::tanh (2 * high.getSample (static_cast<int> (c), static_cast<int> (n))) / 2);
        filters.processSamplesDown (base);
    }
    double latency() const noexcept { return filters.getLatencyInSamples(); }
private:
    juce::dsp::Oversampling<double> filters { 2 };
};
template <typename Process>
std::array<double, 4> profile (double rate, Process&& process)
{
    constexpr double pi = juce::MathConstants<double>::pi;
    juce::AudioBuffer<double> audio (2, capacity);
    std::array<double, 1024> times {};
    double checksum = 0;
    for (int block = -32; block < 1024; ++block)
    {
        for (int c = 0; c < 2; ++c) for (int n = 0; n < capacity; ++n)
            audio.setSample (c, n, 0.7 * std::sin (2 * pi * (c == 0 ? 60 : 73) * ((block + 32) * capacity + n) / rate));
        const auto start = std::chrono::steady_clock::now(); process (audio);
        const auto elapsed = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count();
        if (block >= 0) times[static_cast<std::size_t> (block)] = elapsed;
        checksum += audio.getSample (0, 127);
    }
    std::sort (times.begin(), times.end()); return { times[512], times[1022], times.back(), checksum };
}
}

TEST_CASE ("Shared oversampling experiment retains fixed delay accounting", "[module-shared]")
{
    using namespace ModuleResearch;
    const auto* directory = std::getenv ("LUMIERE_MODULE_REPORT_DIR");
    if (! directory) { SUCCEED ("Optional isolated CPU export"); return; }
    std::ofstream report (std::string (directory) + "/shared.csv");
    report << "kind,variant,topology,rate_hz,channels,factor,configuration,latency_samples,median_block_ns,p999_block_ns,worst_block_ns,checksum\n" << std::setprecision (17);
    for (double rate : { 48000.0, 96000.0, 192000.0 })
        for (auto v : { Variant::smooth, Variant::play3 })
            for (auto t : { Topology::shunt, Topology::series })
            {
                Processor first (rate, 8, 2, v, t), shared (rate, 8, 2, v, t);
                ShaperRegion second (8); shared.secondStage = true;
                CHECK (std::abs (first.latency() - shared.latency()) < 1.0e-12);
                const auto separateTime = profile (rate, [&] (auto& audio) { first.process (audio); second.process (audio); });
                const auto sharedTime = profile (rate, [&] (auto& audio) { shared.process (audio); });
                for (int mode : { 0, 1 })
                {
                    const auto& time = mode == 0 ? separateTime : sharedTime;
                    report << "measurement," << name (v) << ',' << name (t) << ',' << rate << ",2,8," << (mode == 0 ? "separate" : "shared") << ','
                        << (mode == 0 ? first.latency() + second.latency() : shared.latency()) << ',' << time[0] << ',' << time[1] << ',' << time[2] << ',' << time[3] << '\n';
                }
                report.flush();
            }
    REQUIRE (report.good());
}
