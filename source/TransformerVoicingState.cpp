#include "TransformerVoicingState.h"

std::uint64_t TransformerVoicingState::encode (Snapshot snapshot) noexcept
{
    return static_cast<std::uint64_t> (TransformerVoicing::encode (snapshot.a))
        | (static_cast<std::uint64_t> (TransformerVoicing::encode (snapshot.b)) << 25)
        | (static_cast<std::uint64_t> (snapshot.useB) << 50)
        | (static_cast<std::uint64_t> (snapshot.reference) << 51)
        | (static_cast<std::uint64_t> (snapshot.levelMatch) << 52);
}

TransformerVoicingState::Snapshot TransformerVoicingState::decode (std::uint64_t word) noexcept
{
    constexpr auto mask = (1ULL << 25) - 1;
    return { TransformerVoicing::decode (static_cast<std::uint32_t> (word & mask)),
        TransformerVoicing::decode (static_cast<std::uint32_t> ((word >> 25) & mask)),
        ((word >> 50) & 1) != 0, ((word >> 51) & 1) != 0, ((word >> 52) & 1) != 0 };
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
        object->setProperty ("driveCalibrationDb", values.driveCalibrationDb);
        object->setProperty ("emphasisDb", values.emphasisDb);
        object->setProperty ("cornerHz", values.cornerHz);
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
    return juce::JSON::toString (juce::var (root.release()), true);
}

bool TransformerVoicingState::restoreJson (const juce::String& json)
{
    const auto root = juce::JSON::parse (json);
    if (! root.isObject()) return false;
    const auto current = static_cast<int> (root["version"]) == TransformerVoicing::version && root["model"].toString() == TransformerVoicing::model;
    const auto legacy = static_cast<int> (root["version"]) == 1 && root["model"].toString() == TransformerVoicing::legacyModel;
    if (! current && ! legacy)
        return false;
    const auto readValues = [] (const juce::var& object, TransformerVoicing::Values& values)
    {
        if (! object.isObject())
            return false;
        const std::array<const char*, 3> keys { "driveCalibrationDb", "emphasisDb", "cornerHz" };
        std::array<float, 3> numbers {};
        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            const auto value = object[keys[i]];
            if (! value.isDouble() && ! value.isInt() && ! value.isInt64())
                return false;
            numbers[i] = static_cast<float> (value);
        }
        values = TransformerVoicing::sanitized ({ numbers[0], numbers[1], numbers[2] });
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
    state.store (encode (snapshot), std::memory_order_relaxed);
    return true;
}
