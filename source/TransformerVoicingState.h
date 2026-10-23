#pragma once

#include "TransformerVoicing.h"
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>

class TransformerVoicingState
{
public:
    struct Snapshot
    {
        TransformerVoicing::Values a, b;
        bool useB = true;
        bool reference = false;
        bool levelMatch = false;
        bool legacyVoicingIgnored = false; // A retired Magnetic workbench was loaded.
    };
    struct AudioValues
    {
        TransformerVoicing::Values voicing;
        bool levelMatch = false;
    };

    Snapshot read() const noexcept;
    AudioValues readAudio() const noexcept;
    void setCurrent (TransformerVoicing::Values values) noexcept;
    void selectB (bool useB) noexcept;
    void copyToOther() noexcept;
    void setReference (bool enabled) noexcept;
    void setLevelMatch (bool enabled) noexcept;
    void resetCurrent() noexcept;
    void resetAll() noexcept;
    juce::String toJson() const;
    bool restoreJson (const juce::String& json);
    static juce::String describe (TransformerVoicing::Values values, bool ids = true);

private:
    static std::uint64_t encode (Snapshot snapshot) noexcept;
    static Snapshot decode (std::uint64_t word) noexcept;
    template <typename Change>
    void update (Change change) noexcept
    {
        auto previous = state.load (std::memory_order_relaxed);
        for (;;)
        {
            auto snapshot = decode (previous);
            change (snapshot);
            if (state.compare_exchange_weak (previous, encode (snapshot), std::memory_order_relaxed))
                return;
        }
    }
    std::atomic<std::uint64_t> state { encode (Snapshot {}) };
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
};
