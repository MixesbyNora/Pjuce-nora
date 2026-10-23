#pragma once

#include "Voicing.h"
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>

// A workbench's A/B snapshots and audition flags, published to the audio thread
// through one lock-free 64-bit word.
template <typename Schema>
class VoicingState
{
public:
    using Values = Voicing::Values<Schema>;
    struct Snapshot
    {
        Values a, b;
        bool useB = true;
        bool reference = false;
        bool levelMatch = false;
        bool legacyVoicingIgnored = false; // A retired workbench model was loaded.
    };
    struct AudioValues
    {
        Values voicing;
        bool levelMatch = false;
    };

    Snapshot read() const noexcept { return decode (state.load (std::memory_order_relaxed)); }
    AudioValues readAudio() const noexcept
    {
        const auto snapshot = read();
        return { snapshot.reference ? Values {} : snapshot.useB ? snapshot.b : snapshot.a, snapshot.levelMatch };
    }
    void setCurrent (Values values) noexcept { update ([values] (Snapshot& s) { (s.useB ? s.b : s.a) = values; s.reference = false; }); }
    void selectB (bool useB) noexcept { update ([useB] (Snapshot& s) { s.useB = useB; s.reference = false; }); }
    void copyToOther() noexcept { update ([] (Snapshot& s) { (s.useB ? s.a : s.b) = s.useB ? s.b : s.a; }); }
    void setReference (bool enabled) noexcept { update ([enabled] (Snapshot& s) { s.reference = enabled; }); }
    void setLevelMatch (bool enabled) noexcept { update ([enabled] (Snapshot& s) { s.levelMatch = enabled; }); }
    void resetCurrent() noexcept { update ([] (Snapshot& s) { (s.useB ? s.b : s.a) = {}; s.reference = false; s.levelMatch = false; }); }
    void resetAll() noexcept { state.store (encode (Snapshot {}), std::memory_order_relaxed); }

    juce::String toJson() const
    {
        const auto snapshot = read();
        const auto valuesToVar = [] (Values values)
        {
            auto object = std::make_unique<juce::DynamicObject>();
            for (std::size_t i = 0; i < Schema::descriptors.size(); ++i)
                object->setProperty (Schema::descriptors[i].id, values[i]);
            return juce::var (object.release());
        };
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty ("version", Schema::version);
        root->setProperty ("model", Schema::model);
        root->setProperty ("A", valuesToVar (snapshot.a));
        root->setProperty ("B", valuesToVar (snapshot.b));
        root->setProperty ("selected", snapshot.useB ? "B" : "A");
        root->setProperty ("reference", snapshot.reference);
        root->setProperty ("levelMatch", snapshot.levelMatch);
        root->setProperty ("legacyVoicingIgnored", snapshot.legacyVoicingIgnored);
        return juce::JSON::toString (juce::var (root.release()), true);
    }
    bool restoreJson (const juce::String& json)
    {
        const auto root = juce::JSON::parse (json);
        if (! root.isObject()) return false;
        const auto modelName = root["model"].toString();
        if (std::any_of (Schema::retiredModels.begin(), Schema::retiredModels.end(),
                [&modelName] (const char* retired) { return modelName == retired; }))
        {
            // A retired engine's controls have no meaning here: load defaults.
            Snapshot snapshot;
            snapshot.legacyVoicingIgnored = true;
            state.store (encode (snapshot), std::memory_order_relaxed);
            return true;
        }
        if (static_cast<int> (root["version"]) != Schema::version || modelName != Schema::model)
            return false;
        if (! root["legacyVoicingIgnored"].isVoid() && ! root["legacyVoicingIgnored"].isBool())
            return false;
        const auto readValues = [] (const juce::var& object, Values& values)
        {
            if (! object.isObject())
                return false;
            for (std::size_t i = 0; i < Schema::descriptors.size(); ++i)
            {
                const auto value = object[Schema::descriptors[i].id];
                if (! value.isDouble() && ! value.isInt() && ! value.isInt64())
                    return false;
                values[i] = static_cast<float> (value);
            }
            values = Voicing::sanitized<Schema> (values);
            return true;
        };
        Snapshot snapshot;
        if (! readValues (root["A"], snapshot.a) || ! readValues (root["B"], snapshot.b)
            || (root["selected"].toString() != "A" && root["selected"].toString() != "B")
            || ! root["reference"].isBool() || ! root["levelMatch"].isBool())
            return false;
        snapshot.useB = root["selected"].toString() == "B";
        snapshot.reference = static_cast<bool> (root["reference"]);
        snapshot.levelMatch = static_cast<bool> (root["levelMatch"]);
        snapshot.legacyVoicingIgnored = static_cast<bool> (root["legacyVoicingIgnored"]);
        state.store (encode (snapshot), std::memory_order_relaxed);
        return true;
    }
    static juce::String describe (Values values, bool ids = true)
    {
        juce::String text;
        for (std::size_t i = 0; i < Schema::descriptors.size(); ++i)
        {
            const auto& d = Schema::descriptors[i];
            if (i != 0) text += ", ";
            auto decimals = 0;
            auto step = d.step;
            while (decimals < 6 && std::abs (step - std::round (step)) > 1.0e-8) { ++decimals; step *= 10.0; }
            text += juce::String (ids ? d.id : d.label) + (ids ? "=" : " ") + juce::String (values[i], decimals);
            if (! ids) text += " " + juce::String (d.unit);
        }
        return text;
    }

private:
    static constexpr auto valueBits = Voicing::valueBits<Schema>;
    static std::uint64_t encode (Snapshot snapshot) noexcept
    {
        constexpr auto offset = 2 * valueBits;
        return Voicing::encode<Schema> (snapshot.a)
            | (Voicing::encode<Schema> (snapshot.b) << valueBits)
            | (static_cast<std::uint64_t> (snapshot.useB) << offset)
            | (static_cast<std::uint64_t> (snapshot.reference) << (offset + 1))
            | (static_cast<std::uint64_t> (snapshot.levelMatch) << (offset + 2))
            | (static_cast<std::uint64_t> (snapshot.legacyVoicingIgnored) << (offset + 3));
    }
    static Snapshot decode (std::uint64_t word) noexcept
    {
        constexpr auto offset = 2 * valueBits;
        constexpr auto mask = Voicing::valueMask<Schema>;
        return { Voicing::decode<Schema> (word & mask), Voicing::decode<Schema> ((word >> valueBits) & mask),
            ((word >> offset) & 1) != 0, ((word >> (offset + 1)) & 1) != 0,
            ((word >> (offset + 2)) & 1) != 0, ((word >> (offset + 3)) & 1) != 0 };
    }
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
