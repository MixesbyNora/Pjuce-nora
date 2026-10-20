#pragma once

// Stateless curve evaluation apart from the threshold setting. Positive return
// values represent attenuation in dB. This class has no envelope or audio I/O.
class SoftKneeGainComputer
{
public:
    static constexpr float ratio = 4.0f;

    void setThresholdDb (float newThresholdDb) noexcept
    {
        thresholdDb = newThresholdDb;
    }

    float getGainReductionDb (float inputLevelDb) const noexcept
    {
        const auto overDb = inputLevelDb - thresholdDb;
        constexpr auto halfKneeDb = kneeDb * 0.5f;
        constexpr auto slope = 1.0f - 1.0f / ratio;

        if (overDb <= -halfKneeDb)
            return 0.0f;

        if (overDb >= halfKneeDb)
            return slope * overDb;

        const auto kneePosition = overDb + halfKneeDb;
        return slope * kneePosition * kneePosition / (2.0f * kneeDb);
    }

private:
    static constexpr float kneeDb = 6.0f;
    float thresholdDb = 0.0f;
};
