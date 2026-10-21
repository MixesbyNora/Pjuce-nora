#include <PluginProcessor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <bit>
#include <chrono>
#include <fstream>
#include <iomanip>

TEST_CASE ("Default magnetic workbench retains the pre-control audio exactly", "[magnetic-voicing][baseline]")
{
    // Captured separately before implementation in Debug and Release. The two
    // captures were identical; retain one immutable, read-only reference.
    const auto path = juce::File (__FILE__).getParentDirectory().getChildFile ("data").getChildFile ("WorkbenchDefaultMagneticV3.bin");
    std::ifstream input (path.getFullPathName().toStdString(), std::ios::binary);
    REQUIRE (input.good());
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
            for (int mode = 0; mode < 4; ++mode)
            {
                TransformerSaturation stage;
                stage.prepare (rate, mode == 0 ? 0.0f : mode == 1 ? 50.0f : 100.0f, mode == 3);
                juce::AudioBuffer<float> block (channels, 256);
                bool identical = true;
                for (int start = 0; start < 4096; start += 256)
                {
                    for (int ch = 0; ch < channels; ++ch)
                        for (int n = 0; n < 256; ++n)
                        {
                            const auto time = (start + n) / rate;
                            const auto sample = static_cast<float> (0.7 * std::sin (2.0 * juce::MathConstants<double>::pi * (ch == 0 ? 60 : 73) * time)
                                + 0.03 * std::sin (2.0 * juce::MathConstants<double>::pi * 8000 * time));
                            block.setSample (ch, n, sample);
                        }
                    stage.process (block, channels);
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        std::array<float, 256> expected {};
                        input.read (reinterpret_cast<char*> (expected.data()), sizeof (expected));
                        REQUIRE (input.good());
                        identical &= std::memcmp (expected.data(), block.getReadPointer (ch), sizeof (expected)) == 0;
                    }
                }
                CAPTURE (rate, channels, mode);
                CHECK (identical);
            }
    CHECK (input.peek() == std::char_traits<char>::eof());
}

TEST_CASE ("Bounded magnetic bank configuration matches full preparation without changing the solver", "[magnetic-voicing][solver]")
{
    using Circuit = MagneticPrototype::TransformerAudioCircuit;
    std::ofstream report;
    if (const auto* directory = std::getenv ("LUMIERE_MAGNETIC_VOICING_REPORT_DIR"))
    {
        const juce::File root (juce::String::fromUTF8 (directory));
        REQUIRE (root.createDirectory().wasOk());
        report.open (root.getChildFile ("voicing_solver.csv").getFullPathName().toStdString());
        report << std::setprecision (17) << "rate_hz,k,memory,corner_hz,drive_db,exact_preparation_match,solver_failures,maximum_scaled_residual,maximum_iterations\n";
    }
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double k : { 0.0, 4.0, 16.0 })
            for (double memory : { 0.0, 0.35, 1.0 })
                for (double corner : { 0.5, 5.0, 30.0 })
                    for (double driveDb : { -36.0, 12.0, 24.0 })
                {
                    Circuit configured, prepared;
                    const auto internalRate = rate * MagneticPrototype::AudioSettings::factorForRate (rate);
                    configured.prepare (internalRate, {});
                    for (int n = 0; n < 256; ++n) configured.processSample (0.8 * std::sin (n * 0.071));
                    configured.configureMagnetics (k, memory, corner);
                    Circuit::Parameters parameters;
                    parameters.magnetizationWeight = k;
                    parameters.core.memory = memory;
                    parameters.cornerHz = corner;
                    prepared.prepare (internalRate, parameters);
                    const auto drive = std::pow (10.0, driveDb / 20.0);
                    configured.setDriveGain (drive);
                    prepared.setDriveGain (drive);
                    bool exact = true;
                    std::uint32_t noise = 12345;
                    for (int n = 0; n < 2048; ++n)
                    {
                        noise = noise * 1664525U + 1013904223U;
                        const auto x = n < 512 ? 16.0 : n < 1024 ? -16.0 : n < 1536 ? static_cast<double> (noise) / 4294967295.0 * 32.0 - 16.0 : 1.0e-20;
                        exact &= std::bit_cast<std::uint64_t> (configured.processSample (x)) == std::bit_cast<std::uint64_t> (prepared.processSample (x));
                    }
                    CAPTURE (rate, k, memory, corner, driveDb);
                    CHECK (exact);
                    CHECK (configured.getDiagnostics().solverFailures == 0);
                    CHECK (configured.getDiagnostics().maximumEquationResidual <= MagneticPrototype::AudioSettings::rootTolerance);
                    if (report.is_open())
                    {
                        const auto& d = configured.getDiagnostics();
                        report << rate << ',' << k << ',' << memory << ',' << corner << ',' << driveDb << ',' << exact << ','
                            << d.solverFailures << ',' << d.maximumEquationResidual << ',' << d.maximumIterations << '\n';
                    }
                }
}

TEST_CASE ("Workbench v3 migrates new magnetic defaults and v4 retains all descriptors", "[magnetic-voicing][state]")
{
    TransformerVoicingState state;
    state.setCurrent ({ -6.1f, 12.3f, 0.77f, 14.5f });
    state.copyToOther();
    state.selectB (false);
    state.setCurrent ({ 3.0f, 1.2f, 0.12f, 0.5f });
    state.setLevelMatch (true);
    const auto saved = state.toJson();
    TransformerVoicingState restored;
    REQUIRE (restored.restoreJson (saved));
    CHECK (restored.toJson() == saved);
    auto v3 = juce::JSON::parse (saved);
    v3.getDynamicObject()->setProperty ("version", 3);
    v3.getDynamicObject()->setProperty ("model", TransformerVoicing::legacyWorkbenchModel);
    for (const auto slot : { "A", "B" })
        for (std::size_t i = 1; i < TransformerVoicing::descriptors.size(); ++i)
            v3[slot].getDynamicObject()->removeProperty (TransformerVoicing::descriptors[i].id);
    REQUIRE (restored.restoreJson (juce::JSON::toString (v3)));
    PluginProcessor project;
    juce::MemoryBlock projectState;
    project.getStateInformation (projectState);
    auto xml = juce::AudioProcessor::getXmlFromBinary (projectState.getData(), static_cast<int> (projectState.getSize()));
    REQUIRE (xml);
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.getChildWithName ("TRANSFORMER_VOICING").setProperty ("json", juce::JSON::toString (v3), nullptr);
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), projectState);
    project.setStateInformation (projectState.getData(), static_cast<int> (projectState.getSize()));
    CHECK (project.getTransformerVoicing().toJson() == restored.toJson());
    CHECK_FALSE (restored.read().useB);
    CHECK (restored.read().levelMatch);
    CHECK_FALSE (restored.read().legacyShelvesIgnored);
    CHECK (restored.read().a[0] == Catch::Approx (3.0f));
    CHECK (restored.read().b[0] == Catch::Approx (-6.1f));
    for (const auto& values : { restored.read().a, restored.read().b })
        for (std::size_t i = 1; i < TransformerVoicing::descriptors.size(); ++i)
            CHECK (values[i] == Catch::Approx (TransformerVoicing::descriptors[i].defaultValue));
    for (std::size_t i = 0; i < TransformerVoicing::descriptors.size(); ++i)
    {
        auto incomplete = juce::JSON::parse (saved);
        incomplete["B"].getDynamicObject()->removeProperty (TransformerVoicing::descriptors[i].id);
        const auto before = restored.toJson();
        CHECK_FALSE (restored.restoreJson (juce::JSON::toString (incomplete)));
        CHECK (restored.toJson() == before);
    }
    // A default float snapshot maps back to the original double constants.
    CHECK (std::bit_cast<std::uint64_t> (TransformerVoicing::dspValue ({}, TransformerVoicing::memory))
        == std::bit_cast<std::uint64_t> (MagneticPrototype::Settings::defaultMemory));
}

namespace
{
bool finiteSample (float value)
{
    return (std::bit_cast<std::uint32_t> (value) & 0x7f800000U) != 0x7f800000U;
}
std::vector<float> transitionRender (double rate, int channels, int partition, bool dc)
{
    TransformerSaturation stage;
    stage.prepare (rate, 100.0f, false);
    const auto count = static_cast<int> (rate * 0.4);
    const std::array<int, 5> events { static_cast<int> (rate * 0.05), static_cast<int> (rate * 0.055),
        static_cast<int> (rate * 0.1), static_cast<int> (rate * 0.15), count };
    const std::array<TransformerVoicing::Values, 4> settings {
        TransformerVoicing::Values { 12.0f, 16.0f, 1.0f, 30.0f }, { -12.0f, 0.0f, 0.0f, 0.5f },
        { 6.0f, 12.0f, 0.75f, 20.0f }, { -6.1f, 8.0f, 0.1f, 2.0f }
    };
    juce::AudioBuffer<float> block (channels, partition);
    std::vector<float> result (static_cast<std::size_t> (count * channels));
    int start = 0;
    float previousInput = 0.0f, previousOutput = 0.0f, worstExcessStep = 0.0f;
    bool finite = true;
    for (std::size_t event = 0; event < events.size(); ++event)
    {
        while (start < events[event])
        {
            const auto size = std::min (partition, events[event] - start);
            block.setSize (channels, size, false, false, true);
            for (int n = 0; n < size; ++n)
                for (int ch = 0; ch < channels; ++ch)
                    block.setSample (ch, n, dc ? 0.8f : static_cast<float> (0.8 * std::sin (2 * juce::MathConstants<double>::pi * (ch == 0 ? 60 : 73) * (start + n) / rate)));
            stage.process (block, channels);
            for (int n = 0; n < size; ++n)
            {
                const auto input = dc ? 0.8f : static_cast<float> (0.8 * std::sin (2 * juce::MathConstants<double>::pi * 60 * (start + n) / rate));
                const auto output = block.getSample (0, n);
                if (start + n > events[0])
                    worstExcessStep = std::max (worstExcessStep, std::abs (output - previousOutput) - std::abs (input - previousInput));
                previousOutput = output; previousInput = input;
                for (int ch = 0; ch < channels; ++ch)
                {
                    finite &= finiteSample (block.getSample (ch, n));
                    result[static_cast<std::size_t> ((start + n) * channels + ch)] = block.getSample (ch, n);
                }
            }
            start += size;
        }
        if (event < settings.size()) stage.setVoicing (settings[event]);
    }
    CAPTURE (rate, channels, partition, dc, worstExcessStep);
    CHECK (finite);
    CHECK (worstExcessStep < 0.05f);
    CHECK_FALSE (stage.isVoicingTransitioning());
    CHECK (TransformerVoicing::equal (stage.getAppliedVoicing(), settings.back()));
    CHECK (stage.getCircuit().getDiagnostics().solverFailures == 0);
    if (channels == 2) CHECK (stage.getCircuit (1).getDiagnostics().solverFailures == 0);
    return result;
}
}

TEST_CASE ("Magnetic voicing transitions warm coalesce settle and remain partition independent", "[magnetic-voicing][transitions]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
            for (bool dc : { false, true })
            {
                const auto single = transitionRender (rate, channels, 1, dc);
                for (int partition : { 127, 4096 })
                {
                    const auto blocked = transitionRender (rate, channels, partition, dc);
                    CHECK (std::memcmp (single.data(), blocked.data(), single.size() * sizeof (float)) == 0);
                }
            }
}

TEST_CASE ("Magnetic controls stay finite at extremes and retain exact delayed bypass", "[magnetic-voicing][finite][bypass]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
            for (const auto voice : { TransformerVoicing::Values { 12.0f, 16.0f, 1.0f, 30.0f }, { -12.0f, 0.0f, 0.0f, 0.5f } })
                for (int mode = 0; mode < 3; ++mode)
                {
                    TransformerSaturation stage;
                    stage.prepare (rate, mode == 0 ? 0.0f : 100.0f, mode == 1, voice);
                    const int latency = stage.getLatencySamples();
                    juce::AudioBuffer<float> input (channels, 4096);
                    for (int ch = 0; ch < channels; ++ch)
                        for (int n = 0; n < 4096; ++n)
                            input.setSample (ch, n, n < 256 ? 0.0f : n < 1024 ? 15.848932f : n < 2048 ? -15.848932f
                                : n < 3072 ? 1.0e-20f : static_cast<float> (std::sin (n * (ch == 0 ? 0.071 : 0.019))));
                    auto actual = input;
                    stage.process (actual, channels);
                    bool finite = true, exact = true;
                    for (int ch = 0; ch < channels; ++ch)
                        for (int n = 0; n < 4096; ++n)
                        {
                            finite &= finiteSample (actual.getSample (ch, n));
                            const auto expected = n < latency ? 0.0f : input.getSample (ch, n - latency);
                            exact &= std::bit_cast<std::uint32_t> (actual.getSample (ch, n)) == std::bit_cast<std::uint32_t> (expected);
                        }
                    CAPTURE (rate, channels, mode);
                    CHECK (finite);
                    if (mode != 2) CHECK (exact);
                    CHECK (stage.getCircuit().getDiagnostics().solverFailures == 0);
                }
}

TEST_CASE ("Export Release magnetic workbench steady and rapid-edit CPU", "[magnetic-voicing-cpu]")
{
    const auto* directory = std::getenv ("LUMIERE_MAGNETIC_VOICING_REPORT_DIR");
    if (directory == nullptr) { SUCCEED ("Optional isolated Release CPU report"); return; }
    const juce::File root (juce::String::fromUTF8 (directory));
    REQUIRE (root.createDirectory().wasOk());
    std::ofstream output (root.getChildFile ("voicing_cpu.csv").getFullPathName().toStdString());
    output << std::setprecision (17) << "rate_hz,workload,frames,block_size,median_realtime_percent,p95_block_percent,max_block_percent,checksum\n";
    for (double rate : { 48000.0, 96000.0, 192000.0 })
        for (int workload = 0; workload < 3; ++workload)
        {
            std::array<double, 3> times {};
            std::vector<double> blockTimes;
            const auto frames = static_cast<int> (std::ceil (rate * 0.6 / 512)) * 512;
            double checksum = 0.0;
            const TransformerVoicing::Values extreme { 12.0f, 16.0f, 1.0f, 30.0f };
            for (auto& time : times)
            {
                PluginProcessor processor;
                auto* amount = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getLast());
                REQUIRE (amount != nullptr);
                REQUIRE (amount->paramID == "xfrmAmount");
                amount->setValueNotifyingHost (amount->convertTo0to1 (100.0f));
                if (workload == 1) processor.getTransformerVoicing().setCurrent (extreme);
                processor.prepareToPlay (rate, 512);
                juce::AudioBuffer<float> block (2, 512);
                juce::MidiBuffer midi;
                time = 0.0;
                for (int start = -2048; start < frames; start += 512)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int n = 0; n < 512; ++n)
                            block.setSample (ch, n, static_cast<float> (0.7 * std::sin (2 * juce::MathConstants<double>::pi * (ch == 0 ? 60 : 73) * (start + n) / rate)));
                    if (workload == 2 && start >= 0)
                    {
                        auto voice = extreme;
                        // Every block is a different request; exercise coalescing
                        // rather than repeatedly requesting the active bank.
                        voice[TransformerVoicing::driveCalibration] = static_cast<float> (start / 512 % 241 - 120) * 0.1f;
                        voice[TransformerVoicing::memory] = static_cast<float> (start / 512 % 101) * 0.01f;
                        processor.getTransformerVoicing().setCurrent (voice);
                    }
                    const auto begin = std::chrono::steady_clock::now();
                    processor.processBlock (block, midi);
                    const auto elapsed = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - begin).count();
                    if (start >= 0)
                    {
                        time += elapsed;
                        blockTimes.push_back (elapsed * rate / (512.0 * 1.0e7));
                        checksum += block.getSample (0, 511);
                    }
                }
                time = time * rate / (frames * 1.0e7);
            }
            std::sort (times.begin(), times.end());
            std::sort (blockTimes.begin(), blockTimes.end());
            output << rate << ',' << (workload == 0 ? "default" : workload == 1 ? "extreme" : "rapid_edits") << ',' << frames << ",512,"
                << times[1] << ',' << blockTimes[blockTimes.size() * 95 / 100] << ',' << blockTimes.back() << ',' << checksum << '\n';
            REQUIRE (output.good());
        }
}
