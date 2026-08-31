// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <tezla/dsp/Comb.hpp>
#include <tezla/dsp/Scales.hpp>
#include <tezla/dsp/Tuning.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

Tuning tuned (const Scale& scale)
{
    Tuning tuning;

    tuning.setScale (scale);

    return tuning;
}

/// How far apart two pitches are, in cents, sign and all.
double cents (double a, double b)
{
    return 1200.0 * std::log2 (a / b);
}
} // namespace

// ---------------------------------------------------------------------------
// The snap itself
// ---------------------------------------------------------------------------

TEZLA_TEST (a_scale_degree_snaps_to_itself_exactly)
{
    // The fixed points. Anything already in the tuning must come back
    // unchanged, or the lock would detune the very notes it exists to serve.
    for (const auto& built : { scales::twelveToneEqual(), scales::justMajor(),
                               scales::pythagorean(), scales::bohlenPierce() })
    {
        const auto tuning = tuned (built);
        const auto& scale = tuning.getScale();

        if (! scale.isUsable())
            continue;

        const double root = tuning.frequencyFor (tuning.getRootNote());

        CHECK (root > 0.0);

        int checked = 0;

        for (int repeat = -2; repeat <= 2; ++repeat)
            for (int degree = 0; degree < scale.size(); ++degree)
            {
                const double hz = root * std::pow (scale.repeat, static_cast<double> (repeat))
                                    * scale.ratios[static_cast<std::size_t> (degree)];

                CHECK_NEAR (cents (tuning.nearestScaleHz (hz), hz), 0.0, 1.0e-9);

                ++checked;
            }

        CHECK (checked > 0);
    }
}

TEZLA_TEST (a_frequency_between_degrees_goes_to_the_nearer_one_in_cents)
{
    const auto tuning = tuned (scales::twelveToneEqual());
    const double root = tuning.frequencyFor (tuning.getRootNote());

    // A semitone is 100 cents, so 40 cents up from a degree still belongs to
    // it and 60 belongs to the next one. Nearness in **cents**, which is the
    // only measure that reads the same in every octave -- 40 cents is 40 cents
    // whether it is 5 Hz or 500.
    for (const int repeat : { -2, 0, 3 })
    {
        const double here = root * std::pow (2.0, static_cast<double> (repeat));

        CHECK_NEAR (cents (tuning.nearestScaleHz (here * std::pow (2.0, 40.0 / 1200.0)), here),
                    0.0, 1.0e-9);

        const double next = here * std::pow (2.0, 1.0 / 12.0);

        CHECK_NEAR (cents (tuning.nearestScaleHz (here * std::pow (2.0, 60.0 / 1200.0)), next),
                    0.0, 1.0e-9);
    }
}

TEZLA_TEST (the_snap_is_the_nearest_degree_in_cents_and_not_in_hertz)
{
    // Brute force is the honest check here: for a grid of frequencies across
    // four decades, no degree of the scale within five repeats either way may
    // be nearer **in cents** than the one that came back.
    //
    // Written this way after a weaker version -- "40 cents up stays, 60 cents
    // up moves" -- turned out not to distinguish the two measures at all: over
    // one semitone a hertz measure happens to give the same answers. The two
    // part company across wide intervals and in scales with uneven steps, and
    // only a search over the actual candidates catches that.
    for (const auto& built : { scales::twelveToneEqual(), scales::justMajor(),
                               scales::partch43(), scales::bohlenPierce() })
    {
        const auto tuning = tuned (built);
        const auto& scale = tuning.getScale();
        const double root = tuning.frequencyFor (tuning.getRootNote());

        int checked = 0;

        for (int i = 0; i < 600; ++i)
        {
            const double hz = 20.0 * std::pow (10.0, 3.0 * static_cast<double> (i) / 600.0);
            const double snapped = tuning.nearestScaleHz (hz);
            const double got = std::abs (cents (snapped, hz));

            for (int repeat = -6; repeat <= 6; ++repeat)
                for (int degree = 0; degree < scale.size(); ++degree)
                {
                    const double candidate = root
                        * std::pow (scale.repeat, static_cast<double> (repeat))
                        * scale.ratios[static_cast<std::size_t> (degree)];

                    CHECK (std::abs (cents (candidate, hz)) >= got - 1.0e-9);
                }

            ++checked;
        }

        CHECK (checked == 600);
    }
}

TEZLA_TEST (the_wrap_at_a_repeat_boundary_is_handled)
{
    // The case a naive scan gets wrong: a frequency just under a repeat is
    // nearer the *next* repeat's tonic than this repeat's last degree.
    const auto tuning = tuned (scales::twelveToneEqual());
    const double root = tuning.frequencyFor (tuning.getRootNote());

    // Ten cents under the octave above. The nearest degree is that octave, not
    // the major seventh ninety cents below it.
    const double justUnder = root * std::pow (2.0, 1190.0 / 1200.0);

    CHECK_NEAR (cents (tuning.nearestScaleHz (justUnder), root * 2.0), 0.0, 1.0e-9);

    // And symmetrically, ten cents over.
    const double justOver = root * std::pow (2.0, 1210.0 / 1200.0);

    CHECK_NEAR (cents (tuning.nearestScaleHz (justOver), root * 2.0), 0.0, 1.0e-9);
}

TEZLA_TEST (the_snap_never_moves_a_pitch_further_than_half_a_step)
{
    // The property that makes it usable rather than merely correct: whatever
    // you set, the lock moves it by at most half the scale's smallest step, so
    // it corrects the tuning without changing the sound you dialled in.
    //
    // Swept across four decades and four tunings rather than sampled.
    for (const auto& built : { scales::twelveToneEqual(), scales::justMajor(),
                               scales::bohlenPierce(), scales::partch43() })
    {
        const auto tuning = tuned (built);
        const auto& scale = tuning.getScale();

        if (! scale.isUsable())
            continue;

        // The widest gap between adjacent degrees, including the wrap.
        double widest = 0.0;

        for (int degree = 0; degree < scale.size(); ++degree)
        {
            const double here = scale.ratios[static_cast<std::size_t> (degree)];
            const double next = degree + 1 < scale.size()
                                  ? scale.ratios[static_cast<std::size_t> (degree + 1)]
                                  : scale.repeat;

            widest = std::max (widest, 1200.0 * std::log2 (next / here));
        }

        double worst = 0.0;

        for (int i = 0; i < 4000; ++i)
        {
            const double hz = 20.0 * std::pow (10.0, 3.0 * static_cast<double> (i) / 4000.0);
            const double snapped = tuning.nearestScaleHz (hz);

            CHECK (snapped > 0.0);

            worst = std::max (worst, std::abs (cents (snapped, hz)));
        }

        // Half the widest gap, plus a hair for the arithmetic.
        CHECK (worst <= 0.5 * widest + 1.0e-6);
    }
}

TEZLA_TEST (a_nonsensical_frequency_comes_straight_back)
{
    Tuning tuning;

    CHECK (tuning.nearestScaleHz (0.0) == 0.0);
    CHECK (tuning.nearestScaleHz (-100.0) == -100.0);

    // **A broken scale cannot get in**, which is the more useful fact and was
    // found by the test failing: `setScale` refuses anything `isUsable` turns
    // down and leaves the previous tuning in place, so a Tuning always holds a
    // usable scale and `nearestScaleHz` never meets a degenerate one through
    // the public API. The guards inside it stay as belt and braces on a struct
    // that is public, but the invariant is here rather than in a comment.
    Scale broken;
    broken.name = "broken";
    broken.ratios = { 1.0 };
    broken.repeat = 1.0;   // not a repeat at all

    Tuning refused;

    CHECK (! refused.setScale (broken));
    CHECK (refused.getScale().name == "12-TET");
    CHECK (refused.getScale().isUsable());

    // And it still snaps, to the tuning it kept.
    const double root = refused.frequencyFor (refused.getRootNote());

    CHECK_NEAR (cents (refused.nearestScaleHz (root * std::pow (2.0, 10.0 / 1200.0)), root),
                0.0, 1.0e-9);
}

// ---------------------------------------------------------------------------
// The comb's half of it
// ---------------------------------------------------------------------------

TEZLA_TEST (the_comb_tuning_ratio_is_bit_exact_at_one)
{
    // Off has to be the comb that shipped, not a comb that rounds to it. The
    // ratio is applied unconditionally, so the claim rests on `x * 1.0` being
    // exact -- which it is, and this is the test that says so rather than
    // assuming it.
    Comb reference;
    Comb ratioed;

    for (auto* comb : { &reference, &ratioed })
    {
        comb->prepare (kRate);
        comb->setDelaySeconds (0.004);
        comb->setKeyTrack (0.6);
        comb->setNoteHz (110.0);
        comb->setFeedback (0.7);
        comb->setDamping (0.3);
        comb->setMix (1.0);
        comb->setSpread (0.4);
    }

    ratioed.setTuningRatio (1.0);

    CHECK (reference.currentDelaySamples() == ratioed.currentDelaySamples());

    double left = 0.0;
    double right = 0.0;
    double leftB = 0.0;
    double rightB = 0.0;

    for (int i = 0; i < 12000; ++i)
    {
        const double drive = std::sin (0.07 * static_cast<double> (i));

        left = drive;
        right = -drive;
        leftB = drive;
        rightB = -drive;

        reference.process (left, right);
        ratioed.process (leftB, rightB);

        CHECK (left == leftB);
        CHECK (right == rightB);
    }
}

TEZLA_TEST (the_comb_tuning_ratio_moves_the_resonance_by_exactly_that_ratio)
{
    // Delay and frequency are reciprocals, so a ratio of r on the delay is a
    // ratio of 1/r on the pitch -- which is the arithmetic the engine relies
    // on when it hands over `resonant / snapped`.
    Comb comb;

    comb.prepare (kRate);
    comb.setDelaySeconds (0.004);
    comb.setKeyTrack (0.6);
    comb.setNoteHz (110.0);

    const double base = comb.firstNotchHz();

    for (const double ratio : { 0.5, 0.9438743, 1.0, 1.0594631, 2.0 })
    {
        comb.setTuningRatio (ratio);

        CHECK_NEAR (comb.firstNotchHz() * ratio, base, 1.0e-9);
    }

    // A nonsensical ratio is refused rather than dividing by zero -- this is a
    // control-rate setter fed from an engine that could hand it anything.
    comb.setTuningRatio (0.0);
    CHECK (comb.getTuningRatio() == 1.0);

    comb.setTuningRatio (-3.0);
    CHECK (comb.getTuningRatio() == 1.0);
}
