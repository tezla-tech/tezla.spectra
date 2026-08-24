#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/Waveshapers.hpp>
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
