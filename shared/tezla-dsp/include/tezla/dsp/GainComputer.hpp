// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The static curve of the dynamics stage: how much gain reduction to apply for
// a given input level, before any attack or release smoothing.
//
// The quadratic soft-knee gain computer, in the standard form (Giannoulis,
// Massberg & Reiss, JAES 2012 -- see docs/DSP-REFERENCES.md; derived and
// measured here rather than transcribed). Three controls:
//
//   Ceiling  the level the curve bends toward. With an infinite Ratio it is
//            a ceiling in the literal sense -- the output does not pass it --
//            and with a finite Ratio the same number is what a compressor
//            calls its threshold. setThresholdDb is the same setter under
//            that name, because a call site reads wrong under the other one.
//   Knee     how far below the Ceiling the curve starts bending. 0 is a hard
//            corner exactly at the Ceiling; 24 dB starts easing 24 dB below
//            it, which is the gentle, glue-ish end.
//   Ratio    1 (nothing at all) to infinity (a limiter). The default is
//            INFINITY, and that default is load-bearing.
//
// **Infinite ratio is bit-exact against the limiter this class used to be.**
// The whole ratio generalisation is one multiply by `slope_ = 1 - 1/ratio`,
// and 1/infinity is exactly 0 in IEEE arithmetic, so the slope is exactly 1.0
// and multiplying by it changes no bit of any result. That matters because
// Capstone is built on this class and had shipped: a "compatible" curve that
// moved its output by an ulp would have changed every project using it. The
// test asserts the old formula's output bit-for-bit over a swept range rather
// than approximately.
//
// Ratio 1:1 is the other exact case: slope 0, so every branch returns a
// signed zero and the stage is bit-exact bypass (CLAUDE.md section 7 -- a
// neutral setting must be identity, not nearly identity).

#include <algorithm>
#include <cmath>
#include <limits>

namespace tezla::dsp {

class GainComputer
{
public:
    void setCeilingDb (double ceilingDb) noexcept { ceilingDb_ = ceilingDb; }

    /// The same number as setCeilingDb, under the name the compressor case
    /// uses. Both are correct in their own context and neither reads right in
    /// the other's, which is the whole reason for the alias.
    void setThresholdDb (double thresholdDb) noexcept { ceilingDb_ = thresholdDb; }

    /// How far below the ceiling the knee begins, in dB. 0 = hard corner.
    void setKneeDb (double kneeDb) noexcept { kneeDb_ = std::max (kneeDb, 0.0); }

    /// Compression ratio, 1 (no reduction at all) to infinity (a limiter).
    ///
    /// Stored as its reciprocal, because that is what the curve uses and
    /// because 1/infinity is exactly 0 -- which is what makes the infinite
    /// case bit-exact rather than merely close. Values below 1 are clamped:
    /// a ratio under 1 is an upward expander, which is a different curve and
    /// not what any caller of this class wants by accident.
    void setRatio (double ratio) noexcept
    {
        ratio_ = std::max (ratio, 1.0);
        slope_ = 1.0 - 1.0 / ratio_;
    }

    [[nodiscard]] double getCeilingDb()   const noexcept { return ceilingDb_; }
    [[nodiscard]] double getThresholdDb() const noexcept { return ceilingDb_; }
    [[nodiscard]] double getKneeDb()      const noexcept { return kneeDb_; }
    [[nodiscard]] double getRatio()       const noexcept { return ratio_; }

    /// Gain reduction in dB for an input at `levelDb`. Always <= 0.
    ///
    /// Written with the knee *width* equal to twice the Knee control, so the
    /// curve starts bending exactly `knee` dB below the ceiling and reaches
    /// its final slope exactly at it -- which is what the control claims to
    /// do, and what makes Ceiling mean the level it says.
    ///
    /// `slope_` is the only thing the ratio contributes: 1 - 1/ratio, which
    /// is exactly 1.0 for a limiter (so this is bit-for-bit the limiter it
    /// used to be) and exactly 0.0 at 1:1 (so that is bit-exact bypass).
    [[nodiscard]] double computeGainReductionDb (double levelDb) const noexcept
    {
        const double kneeWidth = 2.0 * kneeDb_;
        const double overshoot = levelDb - ceilingDb_;

        if (kneeWidth <= 0.0)
            return overshoot <= 0.0 ? 0.0 : -overshoot * slope_;

        if (overshoot <= -kneeDb_)
            return 0.0;

        if (overshoot >= kneeDb_)
            return -overshoot * slope_;

        const double intoKnee = overshoot + kneeDb_;
        return -(intoKnee * intoKnee) / (2.0 * kneeWidth) * slope_;
    }

private:
    double ceilingDb_ { -0.3 };
    double kneeDb_    { 6.0 };
    double ratio_     { std::numeric_limits<double>::infinity() };

    /// 1 - 1/ratio. Exactly 1.0 by default, which is what keeps every
    /// existing caller -- Capstone above all -- bit-for-bit unchanged.
    double slope_     { 1.0 };
};

} // namespace tezla::dsp
