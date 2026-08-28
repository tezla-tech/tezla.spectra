// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/ChebyshevGenerator.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
using namespace tezla::measure;

namespace
{
constexpr double kRate = 48000.0;
constexpr std::size_t kFftSize = 1 << 15;

/// Absolute level of every harmonic, index 0 = DC, 1 = fundamental, n = nth.
///
/// analyseHarmonics() would be the obvious tool and is the wrong one here:
/// everything it reports is scaled to the fundamental, and this generator's
/// whole claim is that the fundamental is not there. Dividing by it turns every
/// number into noise about noise.
struct Levels
{
    std::vector<double> magnitude;

    [[nodiscard]] double db (int harmonic) const
    {
        return gainToDb (magnitude[static_cast<std::size_t> (harmonic)], -400.0);
    }

    [[nodiscard]] double loudestExcept (int harmonic) const
    {
        double loudest = 0.0;
        for (int n = 2; n <= 8; ++n)
            if (n != harmonic)
                loudest = std::max (loudest, magnitude[static_cast<std::size_t> (n)]);
        return loudest;
    }
};

Levels levelsOf (const std::vector<double>& signal, double fundamentalHz)
{
    const auto spectrum = fftOfReal (signal);
    const double binWidth = kRate / static_cast<double> (signal.size());
    const auto bin = static_cast<std::size_t> (std::llround (fundamentalHz / binWidth));

    Levels levels;
    levels.magnitude.assign (9, 0.0);

    // A bin-exact tone still spills a little into its neighbours through
    // rounding, so a component is its bin plus one either side.
    for (int n = 0; n <= 8; ++n)
    {
        const std::size_t centre = bin * static_cast<std::size_t> (n);
        double power = 0.0;

        for (std::size_t k = (centre > 0 ? centre - 1 : 0); k <= centre + 1; ++k)
            power += std::norm (spectrum[k]);

        levels.magnitude[static_cast<std::size_t> (n)] = std::sqrt (power);
    }

    return levels;
}

/// Feeds a bin-exact unit sine straight through the curve -- no ADAA, no
/// oversampling -- so what comes back is the polynomial's own harmonic content
/// and not a measurement of the machinery around it.
std::vector<double> shapeUnitSine (const ChebyshevGenerator& generator, double& frequencyHz)
{
    frequencyHz = binExactFrequency (300.0, kRate, kFftSize);
    const auto input = sine (frequencyHz, 1.0, kRate, kFftSize);

    std::vector<double> output (input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = generator.evaluate (input[i]);

    return output;
}

Levels shapeAndAnalyse (const ChebyshevGenerator& generator)
{
    double frequencyHz = 0.0;
    const auto shaped = shapeUnitSine (generator, frequencyHz);
    return levelsOf (shaped, frequencyHz);
}

ChebyshevGenerator withOnly (int harmonic, double gain = 1.0)
{
    ChebyshevGenerator generator;
    generator.setHarmonicGain (harmonic, gain);
    return generator;
}

ChebyshevGenerator withEverything (double gain, double indexValue)
{
    ChebyshevGenerator generator;
    const std::array<double, ChebyshevGenerator::kNumHarmonics> gains
        { gain, gain, gain, gain, gain, gain, gain };

    generator.setAll (gains.data(), 0.0, indexValue);
    return generator;
}

double meanOf (const std::vector<double>& signal)
{
    double sum = 0.0;
    for (const double x : signal)
        sum += x;
    return sum / static_cast<double> (signal.size());
}
} // namespace

TEZLA_TEST (chebyshev_selects_one_harmonic_and_nothing_else)
{
    // The whole claim of the mode, and the only test that really matters. Ask
    // for the 5th and the 5th is what arrives -- not "mostly", not "dominantly".
    // Every other harmonic has to be at the numerical floor, because T_n of a
    // unit cosine *is* cos(n t) and contains no other term at all.
    for (const int harmonic : { 2, 3, 4, 5, 6, 7, 8 })
    {
        const auto levels = shapeAndAnalyse (withOnly (harmonic));

        CHECK (levels.db (harmonic) - levels.db (0) > 120.0);   // and no DC either

        for (int n = 1; n <= 8; ++n)
            if (n != harmonic)
                CHECK (levels.db (n) < levels.db (harmonic) - 120.0);
    }
}

TEZLA_TEST (chebyshev_never_produces_a_fundamental_at_any_index)
{
    // Away from index 1 the odd polynomials put energy back at the fundamental:
    // T_3(a cos t) = 3a(a^2 - 1) cos t + a^3 cos 3t, so at index 0.6 the
    // fundamental is *five times* the third harmonic. Left alone that would
    // quietly turn precision mode into the band-copying exciter the whole plugin
    // exists to avoid, and no single-index test would have caught it.
    //
    // So it is cancelled, and this is the test that says so at every index
    // rather than only at the one where it was free.
    for (const double indexValue : { 0.2, 0.45, 0.7, 1.0, 1.35, 1.9 })
    {
        const auto levels = shapeAndAnalyse (withEverything (0.7, indexValue));
        const double loudest = levels.loudestExcept (-1);

        CHECK (loudest > 0.0);
        CHECK (levels.magnitude[1] < loudest * 1.0e-5);
    }
}

TEZLA_TEST (chebyshev_leaves_no_dc_at_any_index)
{
    // The other correction, and the one this plugin has already been bitten by:
    // the mean is multiplied by a signal envelope downstream, so it arrives as a
    // *moving* offset rather than a static one a blocker could remove.
    for (const double indexValue : { 0.1, 0.35, 0.7, 1.0, 1.3, 2.0 })
    {
        for (const int harmonic : { 2, 3, 4, 5, 6, 7, 8 })
        {
            ChebyshevGenerator generator;
            generator.setIndex (indexValue);
            generator.setHarmonicGain (harmonic, 1.0);

            double frequencyHz = 0.0;
            const auto shaped = shapeUnitSine (generator, frequencyHz);

            CHECK_NEAR (meanOf (shaped), 0.0, 1.0e-6);
        }
    }
}

TEZLA_TEST (chebyshev_corrections_agree_across_the_clamp_boundary)
{
    // Below index 1 both corrections are closed forms; above it they are a
    // quadrature, because a flat-topped cosine is not a polynomial. Two
    // different pieces of arithmetic meeting at index 1 is exactly where a
    // discontinuity hides, and a discontinuity there is a click.
    ChebyshevGenerator below, above;

    for (int n = 2; n <= 8; ++n)
    {
        below.setHarmonicGain (n, 0.8);
        above.setHarmonicGain (n, 0.8);
    }

    below.setIndex (1.0 - 1.0e-7);
    above.setIndex (1.0 + 1.0e-7);

    CHECK_NEAR (below.getFundamentalTrim(), above.getFundamentalTrim(), 1.0e-5);

    for (const double x : { -0.9, -0.4, 0.0, 0.4, 0.9 })
        CHECK_NEAR (below.evaluate (x), above.evaluate (x), 1.0e-5);

    // And at index 1 exactly, the closed forms are structurally zero: nothing is
    // being corrected because there is nothing to correct.
    ChebyshevGenerator exact;
    for (int n = 2; n <= 8; ++n)
        exact.setHarmonicGain (n, 0.8);

    CHECK (exact.getFundamentalTrim() == 0.0);
}

TEZLA_TEST (chebyshev_all_gains_off_is_exactly_the_zero_function)
{
    // Not "very quiet" -- zero, so the wet path adds literally nothing and the
    // bit-exact bypass the rest of the plugin depends on survives.
    const ChebyshevGenerator generator;

    for (const double x : { -8.0, -1.5, -1.0, -0.3, 0.0, 0.3, 1.0, 1.5, 8.0 })
    {
        CHECK (generator.evaluate (x) == 0.0);
    }

    CHECK (generator.getHighestActiveHarmonic() == 0);
}

TEZLA_TEST (chebyshev_index_zero_is_exactly_the_zero_function)
{
    // The pedestal subtraction is exactly T_n(0) at index 0 and the fundamental
    // term is zero, so the generator collapses to nothing and Index is a real
    // fade rather than a fade to a constant. Without it, index 0 would leave a
    // DC offset the size of the harmonics -- multiplied by a signal envelope,
    // which is the moving pedestal this plugin has already been bitten by once.
    const auto generator = withEverything (1.0, 0.0);

    for (const double x : { -3.0, -1.0, -0.2, 0.0, 0.2, 1.0, 3.0 })
        CHECK (generator.evaluate (x) == 0.0);
}

TEZLA_TEST (chebyshev_tilt_at_centre_is_a_bit_exact_identity)
{
    // pow(10, 0) is exactly 1, so a tilt at centre multiplies a hand-set recipe
    // by one to the bit. A macro that quietly rounded every level would make
    // "exact harmonic selection" a slogan rather than a property.
    ChebyshevGenerator plain, tilted;

    for (int n = 2; n <= 8; ++n)
    {
        plain .setHarmonicGain (n, 0.3 + 0.1 * n);
        tilted.setHarmonicGain (n, 0.3 + 0.1 * n);
    }

    tilted.setTilt (0.0);

    for (const double x : { -1.2, -0.6, -0.1, 0.0, 0.1, 0.6, 1.2 })
        CHECK (plain.evaluate (x) == tilted.evaluate (x));
}

TEZLA_TEST (chebyshev_tilt_moves_the_ends_and_leaves_the_centre)
{
    // What the macro is for: one gesture from dark to bright across a recipe
    // that would otherwise take seven. Harmonic 5 is the pivot, so it must not
    // move at all.
    ChebyshevGenerator generator;
    for (int n = 2; n <= 8; ++n)
        generator.setHarmonicGain (n, 1.0);

    generator.setTilt (0.0);
    const auto flat = shapeAndAnalyse (generator);

    generator.setTilt (1.0);
    const auto up = shapeAndAnalyse (generator);

    // 4 dB per step, pivoting at 5: harmonic 2 drops 12 dB, harmonic 8 gains 12.
    CHECK_NEAR (up.db (2) - flat.db (2), -12.0, 0.01);
    CHECK_NEAR (up.db (5) - flat.db (5),   0.0, 0.01);
    CHECK_NEAR (up.db (8) - flat.db (8),  12.0, 0.01);
}

TEZLA_TEST (chebyshev_gain_sets_the_harmonic_level_in_decibels)
{
    // The recipe is written in numbers, so the numbers have to mean something:
    // doubling a gain has to raise that harmonic by 6 dB and leave the rest of
    // the recipe exactly where it was.
    ChebyshevGenerator generator;
    generator.setHarmonicGain (3, 1.0);
    generator.setHarmonicGain (4, 1.0);

    const auto before = shapeAndAnalyse (generator);

    generator.setHarmonicGain (3, 2.0);
    const auto after = shapeAndAnalyse (generator);

    CHECK_NEAR (after.db (3) - before.db (3), 6.0206, 0.01);
    CHECK_NEAR (after.db (4) - before.db (4), 0.0, 0.01);
}

TEZLA_TEST (chebyshev_is_continuous_across_the_clamp)
{
    // A step at the clamp boundary would be a click every time a transient
    // crossed it, and a kink in F1 would make ADAA produce a spike rather than
    // suppress one. The curve has a corner there by design -- its slope jumps --
    // so the probe is taken close enough that a finite slope cannot fake a step.
    const auto generator = withEverything (0.6, 1.4);
    const double boundary = 1.0 / 1.4;

    for (const double side : { -1.0, 1.0 })
    {
        const double x = side * boundary;
        constexpr double tiny = 1.0e-10;

        CHECK_NEAR (generator.evaluate (x - tiny), generator.evaluate (x + tiny), 1.0e-6);
    }
}

TEZLA_TEST (chebyshev_clamp_keeps_growth_linear_against_a_bad_amplitude_estimate)
{
    // The design risk, in one test. Outside [-1, 1] the polynomials grow as
    // cosh(n arccosh x): T_8(1.414) is 576, and 1.414 is exactly what
    // sqrt(2)*RMS reports for a band holding two equal partials. Without the
    // clamp a bass note with one overtone is a detonation, not an edge case.
    //
    // With it, the shaped part is bounded outright and the only thing left that
    // grows is the linear fundamental trim -- which is what "cannot detonate"
    // actually means here.
    const auto generator = withEverything (1.0, 1.0);

    const double bound = generator.getPeakBound();
    const double trim  = std::abs (generator.getFundamentalTrim());

    CHECK (bound > 0.0);
    CHECK (bound <= 8.0);      // eight harmonics at unity cannot exceed eight

    for (const double x : { -1000.0, -50.0, -1.4142, -1.0001, 1.0001, 1.4142, 50.0, 1000.0 })
        CHECK (std::abs (generator.evaluate (x)) <= bound + trim * std::abs (x));

    // A naive implementation would be off by orders of magnitude here, so the
    // check is worth stating in the terms that would have failed.
    CHECK (std::abs (generator.evaluate (1.4142)) < 20.0);
}

TEZLA_TEST (chebyshev_clamp_stays_out_of_the_way_at_the_exact_setting)
{
    // Exactness depends on the clamp never engaging for the case it is supposed
    // to be exact on. A unit sine at index 1 touches +/-1 and must not be bent
    // there, which is why the clamp is hard at 1.0 rather than soft-kneed below
    // it. If it had engaged it would have added harmonics of its own.
    const auto levels = shapeAndAnalyse (withOnly (4));

    for (const int n : { 1, 2, 3, 5, 6, 7, 8 })
        CHECK (levels.db (n) < levels.db (4) - 130.0);
}

TEZLA_TEST (chebyshev_index_below_one_blends_the_harmonics)
{
    // The creative half of the control. Below the exact point T_n(a cos t) stops
    // being one harmonic and becomes a mixture of everything up to n -- Le Brun's
    // waveshaping index, the analogue of an FM modulation index. If this did
    // nothing, Index would be a volume control.
    ChebyshevGenerator generator;
    generator.setHarmonicGain (6, 1.0);

    generator.setIndex (1.0);
    const auto exact = shapeAndAnalyse (generator);

    generator.setIndex (0.6);
    const auto blended = shapeAndAnalyse (generator);

    const auto neighbours = [] (const Levels& levels)
    {
        double loudest = -400.0;
        for (const int n : { 2, 3, 4, 5 })
            loudest = std::max (loudest, levels.db (n));
        return loudest;
    };

    CHECK (neighbours (exact) < exact.db (6) - 120.0);       // only the 6th, at the exact point
    CHECK (neighbours (blended) > neighbours (exact) + 60.0);  // a real spread below it
}

TEZLA_TEST (chebyshev_highest_active_harmonic_drives_the_band_limit)
{
    // The caller band-limits its input to internalNyquist / this, so an answer
    // that is too low aliases and one that is too high throws away bandwidth for
    // nothing.
    ChebyshevGenerator generator;
    CHECK (generator.getHighestActiveHarmonic() == 0);

    generator.setHarmonicGain (3, 1.0);
    CHECK (generator.getHighestActiveHarmonic() == 3);

    generator.setHarmonicGain (7, 0.25);
    CHECK (generator.getHighestActiveHarmonic() == 7);

    generator.setHarmonicGain (7, 0.0);
    CHECK (generator.getHighestActiveHarmonic() == 3);

    // Index off silences everything, so there is no band to limit.
    generator.setIndex (0.0);
    CHECK (generator.getHighestActiveHarmonic() == 0);
}

TEZLA_TEST (chebyshev_bulk_setter_matches_the_individual_ones)
{
    // setAll exists so the engine runs one correction per block rather than
    // nine. It is worth pinning that the shortcut is not a different generator.
    ChebyshevGenerator individually, together;

    const std::array<double, ChebyshevGenerator::kNumHarmonics> gains
        { 0.9, 0.0, 0.4, 1.2, 0.0, 0.3, 0.7 };

    for (int n = 2; n <= 8; ++n)
        individually.setHarmonicGain (n, gains[static_cast<std::size_t> (n - 2)]);

    individually.setTilt (0.35);
    individually.setIndex (1.25);

    together.setAll (gains.data(), 0.35, 1.25);

    for (const double x : { -2.0, -0.7, 0.0, 0.7, 2.0 })
        CHECK (individually.evaluate (x) == together.evaluate (x));
}

TEZLA_TEST (chebyshev_rejects_the_fundamental_far_below_what_adaa_would_allow)
{
    // Why this shaper alone in the repository is not run through ADAA, pinned
    // as a number rather than left in a comment.
    //
    // A polynomial of degree n applied to a band-limited signal is exactly
    // band-limited to n times that band, so with the caller's band limit
    // honoured there is nothing for ADAA to remove. Measured on this case, ADAA
    // changed the audible aliasing by 0.2 dB and moved the fundamental from
    // -292 dB to -42 dB -- because its difference quotient averages the curve
    // over a segment in x, and the map from time to x is nonlinear, so the
    // residue lands squarely on the fundamental.
    //
    // The threshold below is deliberately far beyond anything ADAA could reach.
    // If someone wraps this in Adaa1 again, this is the test that says no.
    const double frequencyHz = binExactFrequency (3000.0, kRate, kFftSize);
    const auto input = sine (frequencyHz, 1.0, kRate, kFftSize);

    const auto generator = withOnly (5);

    std::vector<double> output (input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = generator.evaluate (input[i]);

    const auto levels = levelsOf (output, frequencyHz);

    CHECK (levels.db (1) < levels.db (5) - 200.0);
}
