// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Attack and release smoothing for the dynamics stage.
//
// The gain reduction is smoothed, not the level. Smoothing the level and then
// computing gain lets the knee do strange things during a transient, because
// the detector and the curve disagree about what the signal is doing.
// Smoothing the gain keeps the static curve honest and puts the time constants
// where the user expects to hear them.
//
// Times are 1/e time constants, matching SmoothedValue, so a stated 10 ms
// attack reaches about 63% of its target in 10 ms and essentially all of it in
// 50 ms. That is the convention this repository uses everywhere; tooltips say
// so rather than leaving the user to guess.

#include <algorithm>
#include <cmath>

#include "Denormals.hpp"

namespace tezla::dsp {

class EnvelopeFollower
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        setAttackMs (attackMs_);
        setReleaseMs (releaseMs_);
        reset();
    }

    void reset() noexcept
    {
        fastState_ = 0.0;
        slowState_ = 0.0;
    }

    void setAttackMs (double milliseconds) noexcept
    {
        attackMs_     = std::max (milliseconds, 0.0);
        attackCoeff_  = coefficientFor (attackMs_);
    }

    void setReleaseMs (double milliseconds) noexcept
    {
        releaseMs_    = std::max (milliseconds, 0.0);
        releaseCoeff_ = coefficientFor (releaseMs_);

        // The slow path releases six times slower than the fast one, and -- the
        // part that is easy to get wrong -- it also *attacks* slowly, over the
        // release time. If it attacked at the fast rate it would charge fully
        // on a single 5 ms transient and then hold the signal down for half a
        // second, which is not program-dependent release, just a slow release
        // wearing a disguise.
        slowAttackCoeff_  = coefficientFor (releaseMs_);
        slowReleaseCoeff_ = coefficientFor (releaseMs_ * kProgramDependentRatio);
    }

    /// Program-dependent release: a second, slower release runs alongside the
    /// first, and whichever is holding more reduction wins. Short peaks recover
    /// at the set release time; sustained material recovers slowly, which is
    /// what stops a bass line pumping while still letting snares breathe.
    void setProgramDependent (bool shouldBeProgramDependent) noexcept
    {
        programDependent_ = shouldBeProgramDependent;
    }

    /// Feeds the static curve's output through the time constants.
    /// `targetGainReductionDb` is <= 0; the return value is <= 0.
    [[nodiscard]] double process (double targetGainReductionDb) noexcept
    {
        fastState_ = smoothTowards (fastState_, targetGainReductionDb, attackCoeff_, releaseCoeff_);

        if (! programDependent_)
            return fastState_;

        slowState_ = smoothTowards (slowState_, targetGainReductionDb, slowAttackCoeff_, slowReleaseCoeff_);

        // More negative means more reduction, so the slower one wins whenever
        // it has not yet let go.
        return std::min (fastState_, slowState_);
    }

    [[nodiscard]] double getCurrentGainReductionDb() const noexcept
    {
        return programDependent_ ? std::min (fastState_, slowState_) : fastState_;
    }

private:
    static constexpr double kProgramDependentRatio = 6.0;

    [[nodiscard]] double coefficientFor (double milliseconds) const noexcept
    {
        if (milliseconds <= 0.0)
            return 0.0;   // instantaneous

        return std::exp (-1.0 / (milliseconds * 0.001 * sampleRate_));
    }

    [[nodiscard]] static double smoothTowards (double state, double target,
                                               double attackCoeff, double releaseCoeff) noexcept
    {
        // Going further into reduction is the attack; coming back out is the
        // release. Which one applies depends on the direction, not on the sign.
        const double coefficient = target < state ? attackCoeff : releaseCoeff;
        return snapToZero (target + coefficient * (state - target));
    }

    double sampleRate_   { 44100.0 };
    double attackMs_     { 5.0 };
    double releaseMs_    { 200.0 };
    double attackCoeff_  { 0.0 };
    double releaseCoeff_ { 0.0 };
    double slowAttackCoeff_  { 0.0 };
    double slowReleaseCoeff_ { 0.0 };
    double fastState_    { 0.0 };
    double slowState_    { 0.0 };
    bool   programDependent_ { false };
};

} // namespace tezla::dsp
