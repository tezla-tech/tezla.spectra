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
#include <tezla/dsp/Waveshapers.hpp>

#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
using namespace tezla::measure;

TEZLA_TEST (soft_clip_is_exactly_the_identity_below_the_knee)
{
    // The property the whole clip stage is built on. Not "close to", not
    // "within a dB": a mastering clipper that colours the signal it is not
    // clipping is a clipper you can never leave switched on.
    bool exact = true;

    for (const double knee : { 0.0, 0.25, 0.5, 0.9 })
    {
        SoftClip clip (knee);

        for (int i = 0; i < 2000; ++i)
        {
            // Right up to the threshold, and not a hair past it.
            const double x = clip.getThreshold() * (-1.0 + 2.0 * i / 1999.0);

            if (clip.evaluate (x) != x)
                exact = false;

            if (clip.excess (x) != 0.0)
                exact = false;

            if (clip.excessAntiderivative (x) != 0.0)
                exact = false;
        }
    }

    CHECK (exact);
}

TEZLA_TEST (soft_clip_at_zero_knee_is_the_hard_clipper)
{
    // The endpoint has to be the real thing rather than a very steep
    // approximation, because the hard clipper is the baseline every aliasing
    // figure in this project is quoted against.
    bool same = true;

    SoftClip clip (0.0);

    for (int i = 0; i < 4000; ++i)
    {
        const double x = -3.0 + 6.0 * i / 3999.0;

        if (clip.evaluate (x) != HardClip::evaluate (x))
            same = false;

        if (clip.antiderivative (x) != HardClip::antiderivative (x))
            same = false;
    }

    CHECK (same);
}

TEZLA_TEST (soft_clip_at_full_knee_is_tanh)
{
    SoftClip clip (1.0);
    double worst = 0.0;

    for (int i = 0; i < 4000; ++i)
    {
        const double x = -6.0 + 12.0 * i / 3999.0;
        worst = std::max (worst, std::abs (clip.evaluate (x) - std::tanh (x)));
    }

    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (soft_clip_never_exceeds_unity_and_stays_monotonic)
{
    bool bounded = true;
    bool monotonic = true;

    for (const double knee : { 0.0, 0.1, 0.33, 0.5, 0.75, 1.0 })
    {
        SoftClip clip (knee);
        double previous = clip.evaluate (-40.0);

        for (int i = 0; i < 20000; ++i)
        {
            const double x = -40.0 + 80.0 * i / 19999.0;
            const double y = clip.evaluate (x);

            if (std::abs (y) > 1.0 + 1.0e-12)
                bounded = false;

            if (y < previous - 1.0e-15)
                monotonic = false;

            previous = y;
        }
    }

    CHECK (bounded);
    CHECK (monotonic);
}

TEZLA_TEST (soft_clip_antiderivative_is_the_integral_of_the_curve)
{
    // ADAA is a difference of two antiderivatives; a wrong constant in one
    // branch shows up as a step in the output exactly where the curve bends,
    // which is the least forgiving place for one.
    for (const double knee : { 0.0, 0.2, 0.5, 0.8, 1.0 })
    {
        SoftClip clip (knee);

        constexpr int steps = 200000;
        constexpr double limit = 3.0;
        const double h = limit / steps;

        double integral = 0.0;
        double worst = 0.0;

        for (int i = 0; i < steps; ++i)
        {
            const double a = i * h;
            const double b = a + h;

            // Trapezium, which is exact on the linear part and third-order on
            // the rest.
            integral += 0.5 * h * (clip.evaluate (a) + clip.evaluate (b));

            worst = std::max (worst, std::abs (integral - clip.antiderivative (b)));
        }

        CHECK (worst < 1.0e-9);
    }
}

TEZLA_TEST (soft_clip_is_continuous_in_value_and_slope)
{
    // The join is where a wrong scale factor hides. A jump in value is a click;
    // a jump in slope is a second clipper hiding inside the first.
    for (const double knee : { 0.05, 0.25, 0.5, 0.75, 1.0 })
    {
        SoftClip clip (knee);
        const double t = clip.getThreshold();

        constexpr double h = 1.0e-7;

        const double below = clip.evaluate (t - h);
        const double above = clip.evaluate (t + h);
        CHECK (std::abs (above - below) < 1.0e-6);

        const double slopeBelow = (clip.evaluate (t - h) - clip.evaluate (t - 2.0 * h)) / h;
        const double slopeAbove = (clip.evaluate (t + 2.0 * h) - clip.evaluate (t + h)) / h;
        CHECK (std::abs (slopeAbove - slopeBelow) < 1.0e-3);
    }
}

TEZLA_TEST (adaa_on_the_excess_leaves_a_quiet_signal_bit_exact)
{
    // The reason SoftClipExcess exists. ADAA over the whole curve averages
    // consecutive samples, so it lowpasses even where the curve is a straight
    // line; over the excess it returns exactly zero there.
    //
    // Measured both ways on the same signal, so the comparison is of the two
    // formulations rather than of two different tests.
    constexpr int length = 4000;
    std::vector<double> x (length);

    for (int i = 0; i < length; ++i)
    {
        // Half the threshold, near Nyquist, which is where an averager costs
        // the most and where a clipper must be doing nothing at all.
        x[static_cast<std::size_t> (i)]
            = 0.4 * std::sin (2.0 * std::numbers::pi * 0.4 * i);
    }

    SoftClipExcess excess;
    excess.setKnee (0.0);

    Adaa1<SoftClipExcess> excessAdaa;
    excessAdaa.reset();

    SoftClip whole (0.0);
    Adaa1<SoftClip> wholeAdaa;
    wholeAdaa.reset();

    double worstExcess = 0.0;
    double worstWhole  = 0.0;

    for (int i = 1; i < length; ++i)     // skip the priming sample
    {
        const double v = x[static_cast<std::size_t> (i)];

        const double viaExcess = v + excessAdaa.process (v, excess);
        const double viaWhole  = wholeAdaa.process (v, whole);

        worstExcess = std::max (worstExcess, std::abs (viaExcess - v));
        worstWhole  = std::max (worstWhole,  std::abs (viaWhole  - v));
    }

    // Bit exact. Not approximately.
    CHECK (worstExcess == 0.0);

    // And the whole-curve form is visibly not, on the same samples.
    CHECK (worstWhole > 0.1);
}

TEZLA_TEST (adaa_on_the_excess_still_band_limits_the_clipping)
{
    // The other half of the trade. Bit-exactness below the knee would be worth
    // nothing if it had been bought with the antialiasing, so the excess form
    // has to alias no worse than the same clipper ADAA'd whole.
    //
    // Two full periods are rendered and only the second analysed. That is not
    // caution, it is the difference between a meaningful number and a wrong
    // one: the DFT treats its block as circular, and ADAA's first call primes
    // its state and returns the pointwise value instead of the averaged one.
    // Analysing the first block compares sample 0 against a neighbour it never
    // saw -- one wrong sample of about 0.6, which spreads a flat floor over
    // every bin and reads as 25 dB of aliasing that does not exist. It read
    // exactly that here, and made the excess form look 28 dB better than the
    // whole one, which is not a thing a change of variable can do.
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t length = 1 << 14;
    const double frequency = binExactFrequency (1000.0, sampleRate, length);

    const auto render = [&] (int mode, double knee, double amplitude)   // 0 excess, 1 whole, 2 naive
    {
        std::vector<double> y (length);

        SoftClipExcess excess;
        excess.setKnee (knee);
        Adaa1<SoftClipExcess> excessAdaa;
        excessAdaa.reset();

        SoftClip whole (knee);
        Adaa1<SoftClip> wholeAdaa;
        wholeAdaa.reset();

        for (std::size_t i = 0; i < 2 * length; ++i)
        {
            const double v = amplitude * std::sin (2.0 * std::numbers::pi * frequency
                                                   * static_cast<double> (i) / sampleRate);

            const double viaExcess = v + excessAdaa.process (v, excess);
            const double viaWhole  = wholeAdaa.process (v, whole);

            if (i >= length)
                y[i - length] = mode == 0 ? viaExcess
                              : mode == 1 ? viaWhole
                                          : whole.evaluate (v);
        }

        return analyseHarmonics (y, sampleRate, frequency).audibleAliasingDb;
    };

    for (const double knee : { 0.0, 0.5 })
    {
        const double viaExcess = render (0, knee, 4.0);
        const double viaWhole  = render (1, knee, 4.0);
        const double naive     = render (2, knee, 4.0);

        // The two forms differ only in the treatment of the linear part, which
        // carries no harmonics -- so their aliasing is the same number.
        // Measured: -52.85 against -52.67 at knee 0, -72.86 against -72.67 at
        // knee 0.5. Two tenths of a decibel, and the excess form is the better
        // of the two both times, which is the direction removing a lowpass from
        // the linear path should move it.
        CHECK (std::abs (viaExcess - viaWhole) < 0.3);

        // And both are clear of the shaper they are correcting. Ten decibels,
        // measured, with no oversampling at all: -52.9 against -42.9 at knee 0
        // and -72.9 against -61.9 at knee 0.5, at 48 kHz. The engine runs this
        // inside an oversampled block as well, which is where the rest comes
        // from -- ADAA alone was never going to be enough on a hard corner.
        CHECK (viaExcess < naive - 8.0);
    }
}
