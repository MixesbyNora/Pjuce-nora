#include "TransformerVoicingState.h"

std::uint64_t TransformerVoicingState::encode (Snapshot snapshot) noexcept
{
    constexpr auto offset = 2 * TransformerVoicing::valueBits;
    return TransformerVoicing::encode (snapshot.a)
        | (TransformerVoicing::encode (snapshot.b) << TransformerVoicing::valueBits)
        | (static_cast<std::uint64_t> (snapshot.useB) << offset)
        | (static_cast<std::uint64_t> (snapshot.reference) << (offset + 1))
        | (static_cast<std::uint64_t> (snapshot.levelMatch) << (offset + 2))
        | (static_cast<std::uint64_t> (snapshot.legacyVoicingIgnored) << (offset + 3));
}

TransformerVoicingState::Snapshot TransformerVoicingState::decode (std::uint64_t word) noexcept
{
    constexpr auto offset = 2 * TransformerVoicing::valueBits;
    return { TransformerVoicing::decode (word & TransformerVoicing::valueMask),
        TransformerVoicing::decode ((word >> TransformerVoicing::valueBits) & TransformerVoicing::valueMask),
        ((word >> offset) & 1) != 0, ((word >> (offset + 1)) & 1) != 0,
        ((word >> (offset + 2)) & 1) != 0, ((word >> (offset + 3)) & 1) != 0 };
}

TransformerVoicingState::Snapshot TransformerVoicingState::read() const noexcept
{
    return decode (state.load (std::memory_order_relaxed));
}

TransformerVoicingState::AudioValues TransformerVoicingState::readAudio() const noexcept
{
    const auto snapshot = read();
    return { snapshot.reference ? TransformerVoicing::Values {} : snapshot.useB ? snapshot.b : snapshot.a,
        snapshot.levelMatch };
}

void TransformerVoicingState::setCurrent (TransformerVoicing::Values values) noexcept
{
    update ([values] (Snapshot& s) { (s.useB ? s.b : s.a) = values; s.reference = false; });
}
void TransformerVoicingState::selectB (bool useB) noexcept
{
    update ([useB] (Snapshot& s) { s.useB = useB; s.reference = false; });
}
void TransformerVoicingState::copyToOther() noexcept
{
    update ([] (Snapshot& s) { (s.useB ? s.a : s.b) = s.useB ? s.b : s.a; });
}
void TransformerVoicingState::setReference (bool enabled) noexcept
{
    update ([enabled] (Snapshot& s) { s.reference = enabled; });
}
void TransformerVoicingState::setLevelMatch (bool enabled) noexcept
{
    update ([enabled] (Snapshot& s) { s.levelMatch = enabled; });
}
void TransformerVoicingState::resetCurrent() noexcept
{
    update ([] (Snapshot& s) { (s.useB ? s.b : s.a) = {}; s.reference = false; s.levelMatch = false; });
}
void TransformerVoicingState::resetAll() noexcept
{
    state.store (encode (Snapshot {}), std::memory_order_relaxed);
}

juce::String TransformerVoicingState::toJson() const
{
    const auto snapshot = read();
    const auto valuesToVar = [] (TransformerVoicing::Values values)
    {
        auto object = std::make_unique<juce::DynamicObject>();
        for (std::size_t i = 0; i < TransformerVoicing::descriptors.size(); ++i)
            object->setProperty (TransformerVoicing::descriptors[i].id, values[i]);
        return juce::var (object.release());
    };
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("version", TransformerVoicing::version);
    root->setProperty ("model", TransformerVoicing::model);
    root->setProperty ("A", valuesToVar (snapshot.a));
    root->setProperty ("B", valuesToVar (snapshot.b));
    root->setProperty ("selected", snapshot.useB ? "B" : "A");
    root->setProperty ("reference", snapshot.reference);
    root->setProperty ("levelMatch", snapshot.levelMatch);
    root->setProperty ("legacyVoicingIgnored", snapshot.legacyVoicingIgnored);
    return juce::JSON::toString (juce::var (root.release()), true);
}

bool TransformerVoicingState::restoreJson (const juce::String& json)
{
    const auto root = juce::JSON::parse (json);
    if (! root.isObject()) return false;
    const auto modelName = root["model"].toString();
    if (std::any_of (TransformerVoicing::retiredModels.begin(), TransformerVoicing::retiredModels.end(),
            [&modelName] (const char* retired) { return modelName == retired; }))
    {
        // The Magnetic engine is retired; its controls have no VTB1148 meaning.
        Snapshot snapshot;
        snapshot.legacyVoicingIgnored = true;
        state.store (encode (snapshot), std::memory_order_relaxed);
        return true;
    }
    if (static_cast<int> (root["version"]) != TransformerVoicing::version || modelName != TransformerVoicing::model)
        return false;
    if (! root["legacyVoicingIgnored"].isVoid() && ! root["legacyVoicingIgnored"].isBool())
        return false;
    const auto readValues = [] (const juce::var& object, TransformerVoicing::Values& values)
    {
        if (! object.isObject())
            return false;
        for (std::size_t i = 0; i < TransformerVoicing::descriptors.size(); ++i)
        {
            const auto value = object[TransformerVoicing::descriptors[i].id];
            if (! value.isDouble() && ! value.isInt() && ! value.isInt64())
                return false;
            values[i] = static_cast<float> (value);
        }
        values = TransformerVoicing::sanitized (values);
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

juce::String TransformerVoicingState::describe (TransformerVoicing::Values values, bool ids)
{
    juce::String text;
    for (std::size_t i = 0; i < TransformerVoicing::descriptors.size(); ++i)
    {
        const auto& d = TransformerVoicing::descriptors[i];
        if (i != 0) text += ", ";
        auto decimals = 0;
        auto step = d.step;
        while (decimals < 6 && std::abs (step - std::round (step)) > 1.0e-8) { ++decimals; step *= 10.0; }
        text += juce::String (ids ? d.id : d.label) + (ids ? "=" : " ") + juce::String (values[i], decimals);
        if (! ids) text += " " + juce::String (d.unit);
    }
    return text;
}
