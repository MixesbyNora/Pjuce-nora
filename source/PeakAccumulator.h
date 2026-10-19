#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

struct PeakAccumulator
{
    static_assert (std::atomic<float>::is_always_lock_free);

    static float findPeak (const float* samples, int count) noexcept
    {
        auto peak = 0.0f;
        for (int i = 0; i < count; ++i)
            peak = std::max (peak, std::abs (samples[i]));
        return peak;
    }

    static void publish (std::atomic<float>& destination, float peak) noexcept
    {
        auto previous = destination.load (std::memory_order_relaxed);
        while (peak > previous
            && ! destination.compare_exchange_weak (previous, peak,
                std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
    }

    static float read (std::atomic<float>& source) noexcept
    {
        return source.exchange (0.0f, std::memory_order_relaxed);
    }
};

struct LevelMeterPeaks
{
    struct Readings { float inL, inR, outL, outR; };
    std::atomic<float> inL { 0.0f }, inR { 0.0f }, outL { 0.0f }, outR { 0.0f };

    Readings read() noexcept
    {
        return { PeakAccumulator::read (inL), PeakAccumulator::read (inR),
            PeakAccumulator::read (outL), PeakAccumulator::read (outR) };
    }

    void reset() noexcept
    {
        inL.store (0.0f, std::memory_order_relaxed);
        inR.store (0.0f, std::memory_order_relaxed);
        outL.store (0.0f, std::memory_order_relaxed);
        outR.store (0.0f, std::memory_order_relaxed);
    }
};
