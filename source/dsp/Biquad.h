#pragma once

struct DoubleBiquad
{
    struct Coefficients
    {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
    };
    struct State
    {
        double z1 = 0.0;
        double z2 = 0.0;
    };

    static double processSample (double sample, const Coefficients& c, State& s) noexcept
    {
        const auto output = c.b0 * sample + s.z1;
        s.z1 = c.b1 * sample - c.a1 * output + s.z2;
        s.z2 = c.b2 * sample - c.a2 * output;
        return output;
    }
};
