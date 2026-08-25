#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/HarmonicGenerator.hpp>
#include <tezla/dsp/Waveshapers.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
using namespace tezla::measure;

namespace {

constexpr double kRate = 192000.0;
constexpr std::size_t kFftSize = 1 << 15;

/// Runs a bin-exact sine through the generator under ADAA and returns the
/// complex spectrum, with DC removed.
///
/// DC has to go: the even half sits on a pedestal by construction, and leaving
/// it in makes every level reading below it wrong.
struct Analysis
{
    Spectrum spectrum;
    Spectrum inputSpectrum;
    double   frequency {};
    double   binWidth {};

    [[nodiscard]] std::size_t binOf (double multiple) const
    {
        return static_cast<std::size_t> (std::llround (frequency * multiple / binWidth));
    }

    [[nodiscard]] double levelDb (double multiple) const
    {
        // Referenced to the input fundamental, so every number reads as
        // "how far below the signal that produced it".
        // A floor well below -100 dB: the default would clamp exactly the
        // numbers this file exists to prove.
        const auto bin = binOf (multiple);
        return gainToDb (std::abs (spectrum[bin]) / std::abs (inputSpectrum[binOf (1.0)]), -400.0);
    }
};

[[nodiscard]] Analysis analyse (const HarmonicGenerator& generator, double amplitude = 1.0)
{
    Analysis result;
    result.binWidth  = kRate / static_cast<double> (kFftSize);
    result.frequency = binExactFrequency (1000.0, kRate, kFftSize);

    const auto input = sine (result.frequency, amplitude, kRate, kFftSize);

    Adaa1<HarmonicGenerator> adaa;
    std::vector<double> output (kFftSize);
    double mean = 0.0;

    for (std::size_t i = 0; i < kFftSize; ++i)
    {
        output[i] = adaa.process (input[i], generator);
        mean += output[i];
    }

    mean /= static_cast<double> (kFftSize);
    for (auto& sample : output)
        sample -= mean;

    result.spectrum      = fftOfReal (output);
    result.inputSpectrum = fftOfReal (input);
    return result;
}

[[nodiscard]] double rmsOf (const HarmonicGenerator& generator, double amplitude = 1.0)
{
    const double frequency = binExactFrequency (1000.0, kRate, kFftSize);
    const auto input = sine (frequency, amplitude, kRate, kFftSize);

    Adaa1<HarmonicGenerator> adaa;
    std::vector<double> output (kFftSize);
    double mean = 0.0;

    for (std::size_t i = 0; i < kFftSize; ++i)
    {
        output[i] = adaa.process (input[i], generator);
        mean += output[i];
    }

    mean /= static_cast<double> (kFftSize);

    double sum = 0.0;
    for (const double sample : output)
        sum += (sample - mean) * (sample - mean);

    return std::sqrt (sum / static_cast<double> (kFftSize));
}

} // namespace

TEZLA_TEST (harmonic_generator_at_zero_drive_is_exactly_silence)
{
    // Bit-exact, not nearly. Drive is the neutral setting the plugin has to be
    // able to return to, and a generator that still emits there means every
    // existing project changes the day the plugin updates.
    HarmonicGenerator generator;
    generator.setDrive (0.0);

    for (const double colour : { 0.0, 0.5, 1.0 })
    {
        generator.setColour (colour);

        for (const double x : { -1.0e6, -2.0, -0.1, 0.0, 0.1, 2.0, 1.0e6 })
        {
            CHECK (generator.evaluate (x) == 0.0);
            CHECK (generator.antiderivative (x) == 0.0);
        }
    }
}

TEZLA_TEST (harmonic_generator_antiderivative_is_the_integral_of_the_curve)
{
    // ADAA divides differences of the antiderivative. If it is not really the
    // integral the result is a plausible-looking curve that is simply wrong,
    // and nothing else in the plugin will notice.
    HarmonicGenerator generator;
    constexpr double step = 1.0e-7;

    for (const double drive : { 0.01, 0.3, 1.0, 5.0, 30.0 })
    {
        generator.setDrive (drive);

        for (const double colour : { 0.0, 0.35, 1.0 })
        {
            generator.setColour (colour);

            for (const double x : { -2.0, -0.6, -0.02, 0.02, 0.6, 2.0 })
            {
                const double numerical = (generator.antiderivative (x + step)
                                        - generator.antiderivative (x - step)) / (2.0 * step);
                CHECK_NEAR (numerical, generator.evaluate (x), 1.0e-5);
            }
        }
    }
}

TEZLA_TEST (harmonic_generator_even_half_matches_the_reference_curve)
{
    // The generator fuses both curves so one square root serves both, which
    // duplicates the even formula that Waveshapers.hpp also states plainly.
    // This is what stops the two drifting apart: SoftEven is the readable
    // reference and the generator is the fast path.
    //
    // They are not identical, and the two ways they differ are both deliberate:
    // the generator scales the even half by the amplitude it is being fed, and
    // subtracts that amplitude's DC pedestal. Stating the relationship in full
    // is the point -- an equivalence written loosely enough to survive a real
    // change is not guarding anything.
    HarmonicGenerator generator;
    generator.setColour (1.0);

    for (const double drive : { 0.05, 0.5, 4.0, 50.0 })
        for (const double amplitude : { 0.25, 1.0, 3.0 })
        {
            generator.setDrive (drive);
            generator.setInputAmplitude (amplitude);

            const SoftEven reference { drive };
            const double pedestal = evenPedestal (drive * amplitude);

            for (const double x : { -3.0, -0.5, -1.0e-4, 1.0e-4, 0.5, 3.0 })
            {
                CHECK_NEAR (generator.evaluate (x),
                            amplitude * (reference.evaluate (x) - pedestal), 1.0e-14);
                CHECK_NEAR (generator.antiderivative (x),
                            amplitude * (reference.antiderivative (x) - pedestal * x), 1.0e-14);
            }
        }
}

TEZLA_TEST (fitted_pedestal_matches_the_real_integral)
{
    // evenPedestal() is a closed-form fit to a complete elliptic integral of the
    // first kind, and the even half's DC removal depends on it. Check it against
    // the integral rather than against numbers copied out of the same fit.
    //
    // Absolute error is what matters here, not relative: this is a pedestal
    // being subtracted, so what is left over is what leaks through.
    const auto integrated = [] (double v)
    {
        constexpr int steps = 20001;
        double total = 0.0;

        for (int i = 0; i < steps; ++i)
        {
            const double t = 0.5 * std::numbers::pi * (static_cast<double> (i) + 0.5)
                           / static_cast<double> (steps);
            const double s = std::sin (t);
            total += 1.0 / std::sqrt (1.0 + v * v * s * s);
        }

        return 1.0 - (2.0 / std::numbers::pi) * total
                     * (0.5 * std::numbers::pi / static_cast<double> (steps));
    };

    for (const double v : { 0.0, 0.1, 0.5, 1.0, 2.0, 5.0, 15.0, 40.0, 120.0, 500.0 })
        CHECK_NEAR (evenPedestal (v), integrated (v), 4.0e-3);
}

TEZLA_TEST (harmonic_generator_halves_have_the_symmetry_they_claim)
{
    // Everything else rests on this. The even half must be an even function of
    // its input -- that is *why* it cannot contain a fundamental -- and the odd
    // half must be an odd one. A sign error anywhere in either expression shows
    // up here and nowhere else.
    HarmonicGenerator generator;

    for (const double drive : { 0.1, 1.0, 12.0 })
    {
        generator.setDrive (drive);

        for (const double x : { 4.0, 0.8, 0.01 })
        {
            generator.setColour (1.0);
            CHECK_NEAR (generator.evaluate (-x), generator.evaluate (x), 1.0e-15);

            generator.setColour (0.0);
            CHECK_NEAR (generator.evaluate (-x), -generator.evaluate (x), 1.0e-15);
        }
    }
}

TEZLA_TEST (harmonic_generator_odd_half_is_the_saturator_residual)
{
    // With the fundamental trim switched off -- which is what an input
    // amplitude of zero means -- the odd half must be exactly "the saturator
    // minus a straight wire", scaled. If it drifts from that it has stopped
    // being a residual and become an effect of its own.
    HarmonicGenerator generator;
    generator.setColour (0.0);
    generator.setInputAmplitude (0.0);

    for (const double drive : { 0.1, 1.0, 12.0 })
    {
        generator.setDrive (drive);

        // One point fixes the scale; the rest have to follow it exactly.
        const double u1 = drive * 1.0;
        const double reference = 1.0 / std::sqrt (1.0 + u1 * u1) - 1.0;
        const double scale = generator.evaluate (1.0) / reference;

        for (const double x : { -4.0, -0.8, -0.01, 0.01, 0.8, 4.0 })
        {
            const double u = drive * x;
            const double expected = scale * (x / std::sqrt (1.0 + u * u) - x);
            CHECK_NEAR (generator.evaluate (x), expected, 1.0e-12);
        }
    }
}

TEZLA_TEST (fitted_describing_function_matches_the_real_integral)
{
    // fundamentalGain() is a closed-form fit to an elliptic integral, and the
    // whole odd half depends on it being right. Check it against the integral
    // it approximates rather than against a table of numbers copied from the
    // same fit.
    const auto integrated = [] (double v)
    {
        constexpr int steps = 20001;
        double total = 0.0;

        for (int i = 0; i < steps; ++i)
        {
            const double t = std::numbers::pi * (static_cast<double> (i) + 0.5)
                           / static_cast<double> (steps);
            const double s = std::sin (t);
            total += s * s / std::sqrt (1.0 + v * v * s * s);
        }

        return (2.0 / std::numbers::pi) * total * (std::numbers::pi / static_cast<double> (steps));
    };

    // Including values well outside the range the fit was tuned over: both
    // asymptotes are exact by construction, so it has to hold up out there.
    for (const double v : { 0.0, 0.1, 0.5, 1.0, 2.0, 5.0, 15.0, 40.0, 120.0, 500.0 })
    {
        const double exact = integrated (v);
        CHECK_NEAR (fundamentalGain (v) / exact, 1.0, 2.0e-3);
    }
}

TEZLA_TEST (harmonic_generator_even_half_adds_no_fundamental_at_all)
{
    // The headline property. An even function of the input has only DC and even
    // harmonics in its Fourier series, so at Colour = 1 the wet path cannot
    // contain a copy of the source at any level or any drive -- which is what
    // makes Amount a harmonics control rather than a disguised EQ.
    HarmonicGenerator generator;
    generator.setColour (1.0);

    for (const double drive : { 0.5, 4.0, 30.0 })
    {
        generator.setDrive (drive);

        const auto analysis = analyse (generator);

        const double fundamental = analysis.levelDb (1.0);
        const double third       = analysis.levelDb (3.0);

        CHECK (fundamental < -100.0);
        CHECK (analysis.levelDb (2.0) > -60.0);    // second: the point of the exercise

        // What is left at the fundamental has to be the ADAA approximation
        // floor rather than a leaked copy of the source, and the way to tell is
        // that it sits at exactly the same level as the third harmonic -- which
        // an even function cannot produce at all. If a real copy were leaking,
        // the fundamental would rise above the odd-harmonic floor.
        CHECK_NEAR (fundamental, third, 1.0);
    }
}

TEZLA_TEST (harmonic_generator_odd_half_cancels_its_own_fundamental)
{
    // Subtracting the linear term is not enough on its own, and this is the
    // number that says so. A cubic residual carries three times as much
    // fundamental as third harmonic, so before the describing-function trim was
    // added the odd half measured -0.4 dB at the fundamental at drive 30: it
    // was cancelling the band, not exciting it.
    //
    // With the trim the fundamental has to sit well below the harmonic the
    // stage exists to make, at every drive.
    HarmonicGenerator generator;
    generator.setColour (0.0);
    generator.setInputAmplitude (1.0);

    for (const double drive : { 0.25, 0.5, 2.0, 8.0, 30.0, 100.0 })
    {
        generator.setDrive (drive);

        const auto analysis = analyse (generator);

        CHECK (analysis.levelDb (1.0) < -55.0);
        CHECK (analysis.levelDb (3.0) - analysis.levelDb (1.0) > 30.0);
    }
}

TEZLA_TEST (harmonic_generator_two_halves_stay_level_matched_across_drive)
{
    // The odd half shrinks like 1/drive as the saturator approaches a square
    // wave, while the even half simply saturates and does not shrink at all.
    // Untreated the two drift 15 dB apart by drive 30, which turns Colour into
    // a volume control at the top of the Drive range.
    HarmonicGenerator odd, even;
    odd.setColour (0.0);
    even.setColour (1.0);

    for (const double drive : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 30.0 })
    {
        odd.setDrive (drive);
        even.setDrive (drive);

        const double difference = gainToDb (rmsOf (odd) / rmsOf (even));
        CHECK (std::abs (difference) < 3.0);
    }
}

TEZLA_TEST (harmonic_generator_colour_does_not_double_as_a_volume_control)
{
    // Sweeping Colour must change what is generated, not how much. The two
    // curves have equal magnitude at x = 1 by construction, and equal-power
    // mixing is what carries that through the middle of the travel.
    HarmonicGenerator generator;

    for (const double drive : { 0.5, 4.0, 20.0 })
    {
        generator.setDrive (drive);

        double quietest = 1.0e30;
        double loudest  = 0.0;

        for (const double colour : { 0.0, 0.2, 0.4, 0.5, 0.6, 0.8, 1.0 })
        {
            generator.setColour (colour);
            const double rms = rmsOf (generator);

            quietest = std::min (quietest, rms);
            loudest  = std::max (loudest, rms);
        }

        CHECK (gainToDb (loudest / quietest) < 3.0);
    }
}

TEZLA_TEST (harmonic_generator_survives_brutal_input)
{
    // 20 plugins deep on a dubstep master, something will hand this stage a
    // number it was not expecting. It must bend rather than produce a NaN that
    // then propagates through every downstream filter's state.
    HarmonicGenerator generator;

    for (const double drive : { 0.0, 1.0, 100.0 })
    {
        generator.setDrive (drive);

        for (const double colour : { 0.0, 0.5, 1.0 })
        {
            generator.setColour (colour);

            for (const double x : { -1.0e12, -1.0e6, -1.0e-30, 0.0, 1.0e-30, 1.0e6, 1.0e12 })
            {
                CHECK (std::isfinite (generator.evaluate (x)));
                CHECK (std::isfinite (generator.antiderivative (x)));
            }
        }
    }
}
