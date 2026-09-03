// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A very slow bounded wander that mostly sits still and occasionally lurches.
//
// ---------------------------------------------------------------------------
// Why this is not the two walks we already have
// ---------------------------------------------------------------------------
//
// There are two random walks in this repository already -- `UnisonBank`'s
// per-copy drift and Sonitus's per-voice card temperature -- and both are
// **uncorrelated on purpose**. That is what makes a stack thick and eight
// voices sound like eight cards, and it is the opposite of what this is for.
//
// This one is meant to be shared: one walk applied common-mode to everything at
// once, so the whole instrument goes wrong together the way a tape machine or a
// failing power supply does. That is a different sound from a thicker one, and
// it cannot be got from an LFO, because an LFO repeats and the ear locks onto
// the repeat inside a bar.
//
// The two existing walks are deliberately **not** refactored into this. They
// are pinned bit-exact by tests -- a note-on must leave the drift exactly where
// it was -- and folding three things into one class to save thirty lines is how
// that guarantee gets broken by someone tidying up a year from now. This is a
// third walk with a third shape, not a duplicate.
//
// ---------------------------------------------------------------------------
// The lurch, and why it is in the targets and not in the coefficients
// ---------------------------------------------------------------------------
//
// The obvious way to make a wander lurch is to let it fall faster than it
// recovers -- an envelope follower with two time constants. **That biases the
// mean.** A walk that drops quickly and crawls back spends more of its life
// below zero, so the whole instrument ends up permanently flat, and the amount
// it is flat by depends on the random stream. A tuning offset you cannot
// predict is not a feature.
//
// So the one-pole stays symmetric, exactly like the other two, and the shape
// comes from the *target distribution*:
//
//     target = bipolar() * |bipolar()|
//
// Two independent draws. The product is concentrated near zero with occasional
// excursions to the rails -- mostly still, sometimes a lurch -- and it is
// **zero-mean by construction**, since E[a * |b|] = E[a] * E[|b|] = 0 for
// independent a and b. `tests/test_SlowWalk.cpp` runs ten simulated minutes and
// asserts the mean against that; a deliberately skewed target is the
// break-check.
//
// **The bound is structural, not clamped.** Targets lie in [-1, 1], the walk
// starts at 0, and a one-pole with a coefficient in [0, 1] can only ever move
// to a point between where it is and where it is going. So the walk is in
// [-1, 1] for all time by convexity, and whatever depth control scales it is
// therefore its own guarantee of range.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Exact.hpp"
#include "SmallRandom.hpp"

namespace tezla::dsp {

class SlowWalk
{
public:
    /// How many time constants the walk is given to reach each new target.
    ///
    /// Three gets it 95% of the way, which is what makes the period control
    /// mean what its label says: "a new place to be every N seconds, and about
    /// that long to get there". Much less and the walk never reaches the rails
    /// and the depth control quietly does half of what it claims; much more and
    /// it snaps between targets instead of wandering.
    static constexpr double kApproachesPerPeriod = 3.0;

    static constexpr double kMinimumPeriodSeconds = 0.5;
    static constexpr double kMaximumPeriodSeconds = 600.0;

    /// `stepSamples` is the caller's control interval: this advances once per
    /// chunk, never per sample. Counted in samples rather than in blocks, so
    /// the trajectory cannot depend on the host's buffer size -- CLAUDE.md
    /// section 7.
    void prepare (double sampleRate, int stepSamples) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        stepSamples_ = std::max (stepSamples, 1);

        updateCoefficient();
        reset();
    }

    /// The stream the targets are drawn from. Set it per instance so two
    /// instances do not lurch in step.
    void setSeed (std::uint64_t seed) noexcept { seed_ = seed | 1ull; }

    /// Back to the middle, with the stream re-seeded. For `prepare` and a graph
    /// rebuild -- **not** for a note-on, which must leave a shared instability
    /// exactly where it was, for the same reason a key does not reset the
    /// temperature of a transistor.
    void reset() noexcept
    {
        random_.seed (seed_);
        value_ = 0.0;
        target_ = 0.0;
        countdown_ = 0;
    }

    /// How long between new targets, in seconds.
    void setPeriodSeconds (double seconds) noexcept
    {
        const double wanted = std::clamp (seconds, kMinimumPeriodSeconds, kMaximumPeriodSeconds);

        // Guarded: a caller pushes this every control chunk and below is a
        // division. Same value in, nothing recomputed -- and nothing reset
        // either, which matters more: a period change must bend the walk, not
        // restart it.
        if (isExactly (wanted, periodSeconds_))
            return;

        periodSeconds_ = wanted;
        updateCoefficient();
    }

    [[nodiscard]] double getPeriodSeconds() const noexcept { return periodSeconds_; }

    /// One control chunk of wandering.
    void advance() noexcept
    {
        if (countdown_ <= 0)
        {
            // Two draws, and the product of the second's magnitude with the
            // first's sign and size. See the header: this is where the lurch
            // lives, and it is zero-mean.
            const double sign = random_.bipolar();
            const double size = random_.bipolar();

            target_ = sign * std::abs (size);
            countdown_ = static_cast<int> (sampleRate_ * periodSeconds_);
        }

        countdown_ -= stepSamples_;
        value_ += coefficient_ * (target_ - value_);
    }

    /// Where the walk is, in [-1, 1]. Bounded by construction -- see the header.
    [[nodiscard]] double value() const noexcept { return value_; }

    /// Where it is heading. For the test that pins the target distribution.
    [[nodiscard]] double target() const noexcept { return target_; }

private:
    void updateCoefficient() noexcept
    {
        // dt / tau, with tau the period divided by the approaches it is given.
        // The same form as the two existing walks, which write it as
        // 2*pi*f_c*dt with f_c = 1/(2*pi*tau).
        const double tau = periodSeconds_ / kApproachesPerPeriod;

        coefficient_ = std::clamp (static_cast<double> (stepSamples_) / (tau * sampleRate_),
                                   0.0, 1.0);
    }

    double sampleRate_ { 48000.0 };
    int    stepSamples_ { 32 };

    double periodSeconds_ { 20.0 };
    double coefficient_ { 0.0 };

    double value_ { 0.0 };
    double target_ { 0.0 };
    int    countdown_ { 0 };

    std::uint64_t seed_ { 0x2545f4914f6cdd1dull };
    SmallRandom random_;
};

} // namespace tezla::dsp
