// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <cmath>

#include "Denormals.hpp"

namespace tezla::dsp {

/// One-pole exponential smoother for parameter values.
///
/// Every continuous parameter goes through one of these. A parameter written
/// straight into a gain or a filter coefficient produces zipper noise on
/// automation and a click on every mouse move; at high drive a saturator turns
/// that click into a broadband crack.
///
/// The time constant is the 1/e time, so a value reaches ~99% of its target in
/// about 5 * tau. 20 ms is a sane default for gains and drive; use 5-10 ms for
/// anything the user will sweep fast, and 50 ms+ for crossover frequencies.
template <typename Float = double>
class SmoothedValue
{
public:
    void prepare (double sampleRate, Float timeConstantSeconds = static_cast<Float>(0.02)) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        setTimeConstant (timeConstantSeconds);
    }

    void setTimeConstant (Float seconds) noexcept
    {
        // A zero or negative time constant means "no smoothing", which is a
        // legitimate request (discrete switches handled by crossfade elsewhere).
        if (seconds <= Float{})
        {
            coefficient_ = Float{1};
            return;
        }

        coefficient_ = Float{1} - std::exp (static_cast<Float>(-1.0 / (static_cast<double>(seconds) * sampleRate_)));
    }

    /// Jump straight to a value with no ramp. Use in prepareToPlay / reset only.
    void setCurrentAndTarget (Float value) noexcept
    {
        current_ = target_ = value;
        smoothing_ = false;
    }

    void setTarget (Float value) noexcept
    {
        target_ = value;
        smoothing_ = true;
    }

    [[nodiscard]] Float getTarget()  const noexcept { return target_; }
    [[nodiscard]] Float getCurrent() const noexcept { return current_; }

    /// True while the smoother is still moving. Lets a caller skip per-sample
    /// work and use the plain value when nothing is being automated.
    ///
    /// Tracked with a flag rather than by comparing current against target:
    /// the comparison would be an exact float equality test, which is both a
    /// warning under -Wfloat-equal and fragile if the snap threshold ever
    /// changes.
    [[nodiscard]] bool isSmoothing() const noexcept { return smoothing_; }

    /// Advance one sample and return the new value.
    Float next() noexcept
    {
        current_ += coefficient_ * (target_ - current_);

        // Exponential approach never exactly arrives; snap when the remaining
        // distance is inaudible so isSmoothing() can terminate.
        if (std::abs (target_ - current_) < static_cast<Float>(1.0e-9))
        {
            current_ = target_;
            smoothing_ = false;
        }

        return current_;
    }

    /// Advance `numSamples` at once, for when only the end value is needed.
    Float skip (int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            next();

        return current_;
    }

private:
    double sampleRate_  { 44100.0 };
    Float  coefficient_ { 1 };
    Float  current_     {};
    Float  target_      {};
    bool   smoothing_   { false };
};

} // namespace tezla::dsp
