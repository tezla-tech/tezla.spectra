// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Bitcrusher.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/Waveshapers.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
using namespace tezla::measure;

TEZLA_TEST (folder_at_zero_is_exactly_a_straight_wire)
{
    // Not "close to" identity -- exactly it. This stage is always in the path,
    // so if its zero setting coloured the sound there would be no way to turn
    // the folder off.
    const SineFolder folder { 0.0 };

    for (const double x : { -4.0, -1.0, -0.3, 0.0, 0.3, 1.0, 4.0 })
    {
        CHECK (folder.evaluate (x) == x);
        CHECK (folder.antiderivative (x) == 0.5 * x * x);
    }
}

TEZLA_TEST (folder_approaches_identity_continuously)
{
    // And the approach has to be smooth, or automating Fold up from zero clicks.
    for (const double gain : { 1.0e-6, 1.0e-3, 0.01, 0.1 })
    {
        const SineFolder folder { gain };
        for (const double x : { -1.0, -0.25, 0.25, 1.0 })
            CHECK_NEAR (folder.evaluate (x), x, 0.02);
    }
}

TEZLA_TEST (folder_antiderivative_is_the_integral_of_the_folder)
{
    // ADAA divides differences of F1; if F1 is not really the integral, the
    // result is a plausible-looking curve that is simply wrong.
    for (const double gain : { 0.5, 1.5, 6.0, 40.0, 100.0 })
    {
        const SineFolder folder { gain };
        constexpr double step = 1.0e-7;

        for (const double x : { -1.2, -0.4, 0.0, 0.4, 1.2 })
        {
            const double numerical = (folder.antiderivative (x + step)
                                    - folder.antiderivative (x - step)) / (2.0 * step);
            CHECK_NEAR (numerical, folder.evaluate (x), 1.0e-4);
        }
    }
}

TEZLA_TEST (folder_antiderivative_stays_accurate_at_tiny_arguments)
{
    // The half-angle form exists for this case. Written as 1 - cos(g*x) it
    // would lose almost every significant digit here, and ADAA would then
    // divide the wreckage by an equally small number.
    const SineFolder folder { 1.0e-4 };

    for (const double x : { 1.0e-4, 1.0e-3, 0.01, 0.1 })
        CHECK_NEAR (folder.antiderivative (x), 0.5 * x * x, 0.5 * x * x * 1.0e-6);
}

TEZLA_TEST (folder_actually_folds_and_keeps_folding)
{
    // A clipper converges on a square wave and then stops changing. A folder
    // keeps generating new structure as the gain rises -- that is what makes a
    // x100 range worth having.
    const auto countTurningPoints = [] (double gain)
    {
        const SineFolder folder { gain };
        int turns = 0;
        double previous = folder.evaluate (-1.0);
        double previousSlope = 0.0;

        for (int i = 1; i <= 20000; ++i)
        {
            const double x = -1.0 + 2.0 * static_cast<double> (i) / 20000.0;
            const double value = folder.evaluate (x);
            const double slope = value - previous;

            if (previousSlope != 0.0 && ((slope > 0.0) != (previousSlope > 0.0)))
                ++turns;

            previous = value;
            previousSlope = slope;
        }
        return turns;
    };

    CHECK (countTurningPoints (0.5) == 0);          // not folding yet
    CHECK (countTurningPoints (2.0) >= 1);          // just past the first fold
    CHECK (countTurningPoints (10.0) >= 5);
    CHECK (countTurningPoints (100.0) >= 60);       // x100 territory
}

TEZLA_TEST (folder_holds_its_level_as_the_range_climbs)
{
    // The Range switch has to change the sound, not the volume. Without the
    // normalisation the output would shrink as 1/g and x100 would be silent.
    for (const double gain : { 2.0, 10.0, 50.0, 100.0 })
    {
        const SineFolder folder { gain };

        double peak = 0.0;
        for (int i = 0; i <= 4000; ++i)
            peak = std::max (peak, std::abs (folder.evaluate (-1.0 + 2.0 * static_cast<double> (i) / 4000.0)));

        CHECK_NEAR (peak, 2.0 / std::numbers::pi, 0.02);
    }
}

TEZLA_TEST (folder_aliasing_at_each_range_setting)
{
    // Honest numbers rather than a claim. A folder at high gain generates
    // harmonics far beyond what any oversampling factor can contain, so the
    // point is not that x100 is clean -- it is knowing where it stops being.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 65536;
    constexpr int factor = 4;

    const double frequency = binExactFrequency (110.0, fs, fftSize);
    const auto input = sine (frequency, 0.5, fs, 2 * fftSize);

    std::printf ("        %-10s %12s %10s\n", "fold gain", "audible alias", "THD");

    for (const double gain : { 0.0, 1.0, 10.0, 100.0 })
    {
        const SineFolder folder { gain };
        Adaa1<SineFolder> adaa;
        Oversampler oversampler;
        oversampler.prepare (512, 1, factor);

        std::vector<double> output (input.size(), 0.0);
        for (std::size_t offset = 0; offset < input.size(); offset += 512)
        {
            const int numSamples = static_cast<int> (std::min<std::size_t> (512, input.size() - offset));
            const double* inputPointer = input.data() + offset;
            double* outputPointer = output.data() + offset;

            double* const* work = oversampler.upsample (&inputPointer, numSamples);
            for (int i = 0; i < numSamples * factor; ++i)
                work[0][i] = adaa.process (work[0][i], folder);

            oversampler.downsample (&outputPointer, numSamples);
        }

        const std::vector<double> steadyState (output.end() - static_cast<std::ptrdiff_t> (fftSize),
                                               output.end());
        const auto report = analyseHarmonics (steadyState, fs, frequency);

        std::printf ("        x%-9.0f %12.1f %10.2f\n", gain, report.audibleAliasingDb, report.thdDb);

        if (gain == 0.0)
        {
            // Zero fold must be transparent through the whole chain.
            CHECK (report.thdDb < -100.0);
            CHECK (report.audibleAliasingDb < -100.0);
        }
    }
}

TEZLA_TEST (rectifier_at_zero_is_a_straight_wire)
{
    const Rectifier rectifier { 0.0 };

    for (const double x : { -3.0, -0.7, 0.0, 0.7, 3.0 })
    {
        CHECK (rectifier.evaluate (x) == x);
        CHECK (rectifier.antiderivative (x) == 0.5 * x * x);
    }
}

TEZLA_TEST (rectifier_at_full_is_absolute_value)
{
    const Rectifier rectifier { 1.0 };

    for (const double x : { -3.0, -0.7, 0.0, 0.7, 3.0 })
        CHECK_NEAR (rectifier.evaluate (x), std::abs (x), 1.0e-15);
}

TEZLA_TEST (rectifier_antiderivative_is_the_integral_of_the_rectifier)
{
    // Including across the origin, where the two halves of x*|x|/2 join. ADAA
    // straddles that point constantly on any signal that crosses zero.
    for (const double amount : { 0.0, 0.35, 0.8, 1.0 })
    {
        const Rectifier rectifier { amount };
        constexpr double step = 1.0e-7;

        for (const double x : { -1.5, -0.2, -1.0e-5, 1.0e-5, 0.2, 1.5 })
        {
            const double numerical = (rectifier.antiderivative (x + step)
                                    - rectifier.antiderivative (x - step)) / (2.0 * step);
            CHECK_NEAR (numerical, rectifier.evaluate (x), 1.0e-5);
        }
    }
}

TEZLA_TEST (rectifier_produces_an_octave_up)
{
    // The point of the control: full-wave rectification doubles the
    // fundamental, so the second harmonic becomes the loudest thing present.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = binExactFrequency (500.0, fs, fftSize);
    const auto input = sine (frequency, 0.5, fs, fftSize);

    std::vector<double> output (input.size());
    const Rectifier rectifier { 1.0 };
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = rectifier.evaluate (input[i]);

    const auto report = analyseHarmonics (output, fs, frequency);

    // The original fundamental all but disappears and the octave dominates.
    CHECK (report.harmonicsDb[0] > 10.0);
}

TEZLA_TEST (bitcrusher_at_zero_is_bit_exact)
{
    Bitcrusher crusher;
    crusher.setAmount (0.0);

    for (const double x : { -0.9, -0.123456789, 0.0, 0.3333333, 0.9 })
        CHECK (crusher.process (x) == x);
}

TEZLA_TEST (bitcrusher_quantises_to_the_stated_depth)
{
    Bitcrusher crusher;

    // Full crush is one bit: three possible output values.
    crusher.setAmount (1.0);
    CHECK_NEAR (crusher.getBits(), 1.0, 1.0e-12);

    std::vector<double> distinct;
    for (int i = 0; i <= 1000; ++i)
    {
        const double y = crusher.process (-1.0 + 2.0 * static_cast<double> (i) / 1000.0);
        if (std::none_of (distinct.begin(), distinct.end(),
                          [y] (double v) { return std::abs (v - y) < 1.0e-9; }))
            distinct.push_back (y);
    }
    CHECK (distinct.size() <= 3);

    // And a mid setting has more steps than a heavy one, but still far fewer
    // than the input.
    crusher.setAmount (0.5);
    CHECK (crusher.getBits() > 8.0);
    CHECK (crusher.getBits() < 9.0);
}

TEZLA_TEST (downsampler_at_one_is_bit_exact)
{
    Downsampler downsampler;
    downsampler.setRatio (1.0);

    for (const double x : { -0.9, -0.123456789, 0.0, 0.3333333, 0.9 })
        CHECK (downsampler.process (x) == x);
}

TEZLA_TEST (downsampler_holds_for_the_stated_number_of_samples)
{
    Downsampler downsampler;
    downsampler.setRatio (4.0);
    downsampler.reset();

    std::vector<double> input (40);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<double> (i);

    std::vector<double> output (input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = downsampler.process (input[i]);

    // Count how many times the output actually changes: at a ratio of 4 it
    // should be about a quarter of the samples.
    int changes = 0;
    for (std::size_t i = 1; i < output.size(); ++i)
        if (output[i] != output[i - 1])
            ++changes;

    CHECK (changes >= 8);
    CHECK (changes <= 11);
}

TEZLA_TEST (downsampler_ratio_is_continuous)
{
    // Fractional ratios have to work, or automating the control jumps between
    // integer divisions instead of sweeping.
    for (const double ratio : { 1.5, 2.7, 6.25, 13.9 })
    {
        Downsampler downsampler;
        downsampler.setRatio (ratio);
        downsampler.reset();

        int changes = 0;
        double previous = 0.0;
        for (int i = 0; i < 2000; ++i)
        {
            const double y = downsampler.process (static_cast<double> (i));
            if (i > 0 && y != previous)
                ++changes;
            previous = y;
        }

        const double measuredRatio = 2000.0 / static_cast<double> (changes);
        CHECK_NEAR (measuredRatio, ratio, ratio * 0.05);
    }
}

// ---------------------------------------------------------------------------
// SoftEven -- the even-harmonic generator behind Halo's Colour control.
// ---------------------------------------------------------------------------

TEZLA_TEST (soft_even_at_zero_gain_is_exactly_silence)
{
    // Not "nearly nothing". Colour and Drive both scale this curve, and a
    // generator that still emits at its zero setting is one that can never be
    // turned off -- the same rule the folder follows.
    const SoftEven even { 0.0 };

    for (const double x : { -1.0e6, -4.0, -0.3, 0.0, 0.3, 4.0, 1.0e6 })
    {
        CHECK (even.evaluate (x) == 0.0);
        CHECK (even.antiderivative (x) == 0.0);
    }
}

TEZLA_TEST (soft_even_is_even_and_stays_bounded)
{
    // Even, so it generates the even harmonics the odd curve cannot. Bounded,
    // so a hot transient produces harmonics rather than an explosion -- this is
    // the property the ATSR device from the literature does not have.
    for (const double gain : { 0.1, 1.0, 20.0 })
    {
        const SoftEven even { gain };

        for (const double x : { 1.0e-8, 0.01, 0.5, 1.0, 100.0, 1.0e9 })
        {
            CHECK_NEAR (even.evaluate (-x), even.evaluate (x), 1.0e-15);
            CHECK (even.evaluate (x) >= 0.0);
            CHECK (even.evaluate (x) < 1.0);
            CHECK (std::isfinite (even.evaluate (x)));
            CHECK (std::isfinite (even.antiderivative (x)));
        }
    }
}

TEZLA_TEST (soft_even_has_no_linear_term_at_all)
{
    // The whole design of HarmonicGenerator rests on this: the even curve
    // contributes nothing to the fundamental, so there is nothing to cancel.
    // Small-signal behaviour must be g^2 x^2 / 2 -- pure second order.
    for (const double gain : { 0.25, 1.0, 8.0 })
    {
        const SoftEven even { gain };

        for (const double x : { 1.0e-6, 1.0e-4, 1.0e-3 })
        {
            // f(x) = u^2/2 - 3u^4/8 + ..., so the leading term is right to
            // within 3u^2/4. Asserting a fixed tolerance instead would either
            // pass vacuously or fail on the fourth-order term and look like a
            // bug in the curve.
            const double u = gain * x;
            const double expected = 0.5 * u * u;
            CHECK_NEAR (even.evaluate (x) / expected, 1.0, u * u);
        }

        // Slope at the origin, measured the way ADAA would see it.
        constexpr double step = 1.0e-9;
        const double slope = (even.evaluate (step) - even.evaluate (-step)) / (2.0 * step);
        CHECK_NEAR (slope, 0.0, 1.0e-12);
    }
}

TEZLA_TEST (soft_even_antiderivative_is_the_integral_of_the_curve)
{
    for (const double gain : { 0.05, 0.5, 2.0, 30.0 })
    {
        const SoftEven even { gain };
        constexpr double step = 1.0e-7;

        for (const double x : { -3.0, -0.7, -0.05, 0.05, 0.7, 3.0 })
        {
            const double numerical = (even.antiderivative (x + step)
                                    - even.antiderivative (x - step)) / (2.0 * step);
            CHECK_NEAR (numerical, even.evaluate (x), 1.0e-5);
        }
    }
}

TEZLA_TEST (soft_even_antiderivative_survives_tiny_arguments)
{
    // The reason the series branch exists, pinned as a number.
    //
    // x - asinh(g*x)/g subtracts two values that agree to within g^2 x^3 / 6.
    // At g*x = 1e-9 that difference is under one ULP of x, so the direct form
    // returns rounding noise and ADAA turns it into audible hiss on quiet
    // material. Both forms are computed here and only one of them is right.
    constexpr double gain = 1.0e-9;
    const SoftEven even { gain };

    volatile double guard = 1.0;      // keeps the compiler from folding this
    const double x = static_cast<double> (guard);

    const double exact = gain * gain * x * x * x / 6.0;
    CHECK_NEAR (even.antiderivative (x) / exact, 1.0, 1.0e-6);

    const double naive = x - std::asinh (gain * x) / gain;
    CHECK (std::abs (naive - exact) / exact > 0.1);
}

TEZLA_TEST (soft_even_generates_the_even_harmonic_series)
{
    // A sine in should come out with a strong second harmonic and, at this
    // gain, a fourth -- and essentially nothing at the third.
    constexpr double sampleRate = 192000.0;
    constexpr std::size_t fftSize = 1 << 15;

    const double frequency = binExactFrequency (1000.0, sampleRate, fftSize);
    const auto input = sine (frequency, 1.0, sampleRate, fftSize);

    const SoftEven even { 2.0 };
    Adaa1<SoftEven> adaa;

    std::vector<double> output (fftSize);
    double mean = 0.0;

    for (std::size_t i = 0; i < fftSize; ++i)
    {
        output[i] = adaa.process (input[i], even);
        mean += output[i];
    }

    // Even curves sit on a DC pedestal by construction; remove it so the
    // harmonic analysis is not reading the offset as signal.
    mean /= static_cast<double> (fftSize);
    for (auto& sample : output)
        sample -= mean;

    const auto spectrum = fftOfReal (output);
    const double binWidth = sampleRate / static_cast<double> (fftSize);

    const auto levelAt = [&] (int harmonic)
    {
        const auto bin = static_cast<std::size_t> (std::llround (frequency * harmonic / binWidth));
        return std::abs (spectrum[bin]);
    };

    const double second = levelAt (2);
    const double third  = levelAt (3);
    const double fourth = levelAt (4);

    CHECK (second > 0.0);
    CHECK (fourth > 0.0);
    CHECK (third < second * 1.0e-6);      // odd harmonics must be absent
    CHECK (fourth < second);              // and the series must decay
}
