// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <tezla/dsp/SlowWalk.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kStep = 32;

/// Steps the walk for `seconds` and returns every value it took.
std::vector<double> run (SlowWalk& walk, double seconds)
{
    const auto steps = static_cast<std::size_t> (seconds * kRate / kStep);

    std::vector<double> out;
    out.reserve (steps);

    for (std::size_t i = 0; i < steps; ++i)
    {
        walk.advance();
        out.push_back (walk.value());
    }

    return out;
}

SlowWalk made (double period, std::uint64_t seed = 0x9e3779b97f4a7c15ull)
{
    SlowWalk walk;
    walk.setSeed (seed);
    walk.prepare (kRate, kStep);
    walk.setPeriodSeconds (period);
    return walk;
}

double meanOf (const std::vector<double>& v)
{
    double sum = 0.0;

    for (const double x : v)
        sum += x;

    return sum / static_cast<double> (v.size());
}

double stdDevOf (const std::vector<double>& v)
{
    const double mean = meanOf (v);
    double sum = 0.0;

    for (const double x : v)
        sum += (x - mean) * (x - mean);

    return std::sqrt (sum / static_cast<double> (v.size()));
}
} // namespace

TEZLA_TEST (the_slow_walk_is_zero_mean)
{
    // The reason the lurch lives in the target distribution rather than in
    // asymmetric coefficients. A walk that falls faster than it recovers spends
    // more of its life below zero, so the instrument it is driving would sit
    // permanently flat by an amount nobody can predict. `bipolar() * |bipolar()|`
    // is lopsided in *shape* -- mostly small, occasionally at the rail -- and
    // symmetric in sign, so E[target] = E[a] * E[|b|] = 0.
    //
    // Ten simulated minutes, at a period short enough that it is hundreds of
    // targets rather than a handful.
    auto walk = made (2.0);
    const auto values = run (walk, 600.0);

    const double mean = meanOf (values);
    const double sd = stdDevOf (values);

    // The walk is heavily autocorrelated -- consecutive samples are the same
    // wander -- so the count of *independent* observations is the number of
    // targets drawn, not the number of steps. That is what the standard error
    // has to be built from, or this test passes on a badly biased walk.
    const double targets = 600.0 / walk.getPeriodSeconds();
    const double standardError = sd / std::sqrt (targets);

    std::printf ("    slow walk over 600 s: mean %+.6f, sd %.4f, %d targets, 3 s.e. %.4f\n",
                 mean, sd, static_cast<int> (targets), 3.0 * standardError);

    CHECK (std::abs (mean) < 3.0 * standardError);
}

TEZLA_TEST (the_slow_walk_is_bounded_by_construction)
{
    // Targets lie in [-1, 1], the walk starts at 0, and a one-pole with a
    // coefficient in [0, 1] can only move to a point between where it is and
    // where it is going. So this is convexity rather than a clamp, and whatever
    // depth control scales the walk is its own guarantee of range.
    for (const double period : { SlowWalk::kMinimumPeriodSeconds, 1.0, 20.0, 120.0 })
    {
        auto walk = made (period);
        const auto values = run (walk, std::max (60.0, period * 4.0));

        double widest = 0.0;

        for (const double x : values)
            widest = std::max (widest, std::abs (x));

        std::printf ("    slow walk period %6.1f s: |value| reaches %.4f\n", period, widest);

        CHECK (widest <= 1.0);
    }
}

TEZLA_TEST (the_slow_walk_actually_reaches_out_towards_the_rails)
{
    // The other half of the bound, and the one a bug would break silently: a
    // walk that never gets far from zero is bounded too, and useless. Three
    // time constants per target is what buys 95% of the way there, so over
    // enough targets the walk should visit most of its range.
    auto walk = made (2.0);
    const auto values = run (walk, 400.0);

    double widest = 0.0;

    for (const double x : values)
        widest = std::max (widest, std::abs (x));

    std::printf ("    slow walk excursion over 200 targets: %.4f of full range\n", widest);

    CHECK (widest > 0.75);
}

TEZLA_TEST (the_slow_walk_takes_the_same_path_at_every_sample_rate)
{
    // It is a wander in *time*, so a session at 192 kHz must hear the same
    // machine as one at 44.1 kHz. The targets are drawn on a countdown measured
    // in seconds and the one-pole's coefficient is dt/tau, so the continuous
    // trajectory is the same and only its sampling differs.
    constexpr double kSeconds = 90.0;

    std::vector<double> reference;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        SlowWalk walk;
        walk.setSeed (0x1234567890abcdefull);
        walk.prepare (rate, kStep);
        walk.setPeriodSeconds (5.0);

        // Sample the walk once a second, so the four runs are comparable
        // whatever their step count.
        const int stepsPerSecond = static_cast<int> (rate / kStep);

        std::vector<double> perSecond;

        for (int second = 0; second < static_cast<int> (kSeconds); ++second)
        {
            for (int i = 0; i < stepsPerSecond; ++i)
                walk.advance();

            perSecond.push_back (walk.value());
        }

        if (reference.empty())
        {
            reference = perSecond;
            continue;
        }

        double worst = 0.0;

        for (std::size_t i = 0; i < reference.size(); ++i)
            worst = std::max (worst, std::abs (perSecond[i] - reference[i]));

        std::printf ("    slow walk at %6.0f Hz vs 44100: worst difference %.3e\n", rate, worst);

        CHECK (worst < 5.0e-3);
    }
}

TEZLA_TEST (a_period_change_bends_the_walk_rather_than_restarting_it)
{
    // The setter is guarded and the guard must not reset anything: a knob under
    // an automation lane pushes this every control chunk, and a walk that
    // jumped back to zero on every push would be a click generator rather than
    // a slow instability. CLAUDE.md section 7's fourth bite.
    auto walk = made (10.0);
    run (walk, 30.0);

    const double before = walk.value();

    // The same value again changes nothing at all.
    walk.setPeriodSeconds (10.0);
    CHECK (walk.value() == before);

    // A different value leaves the walk where it stands and only changes how
    // fast it moves from here.
    walk.setPeriodSeconds (40.0);
    CHECK (walk.value() == before);

    walk.advance();
    CHECK (std::abs (walk.value() - before) < 0.01);
}

TEZLA_TEST (a_slow_walk_reset_returns_it_to_the_middle_and_replays)
{
    // `reset` is for prepare and a graph rebuild, and it has to be exactly
    // repeatable or nothing here is testable.
    auto first = made (3.0, 0xfeedfacecafebeefull);
    const auto one = run (first, 60.0);

    first.reset();
    CHECK (first.value() == 0.0);

    const auto two = run (first, 60.0);

    CHECK (one.size() == two.size());

    for (std::size_t i = 0; i < one.size(); ++i)
        CHECK (one[i] == two[i]);

    // And two instances with different seeds must not lurch in step, or a
    // shared instability applied to two things would be one thing.
    auto other = made (3.0, 0x0123456789abcdefull);
    const auto three = run (other, 60.0);

    double biggest = 0.0;

    for (std::size_t i = 0; i < one.size(); ++i)
        biggest = std::max (biggest, std::abs (one[i] - three[i]));

    std::printf ("    two seeds diverge by up to %.4f\n", biggest);

    CHECK (biggest > 0.2);
}
