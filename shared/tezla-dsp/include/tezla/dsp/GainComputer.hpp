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
// This is a limiter with a soft knee rather than a compressor with a ratio
// control, which is the shape the reference units in this class use. The two
// controls are Ceiling -- the level the output is not allowed past -- and Knee,
// which says how far below the ceiling the curve starts bending. Knee at 0 is
// a hard corner exactly at the ceiling; knee at 24 dB starts easing the signal
// down from 24 dB below it, which is the gentle, glue-ish end.

#include <algorithm>
#include <cmath>

namespace tezla::dsp {

class GainComputer
{
public:
    void setCeilingDb (double ceilingDb) noexcept { ceilingDb_ = ceilingDb; }

    /// How far below the ceiling the knee begins, in dB. 0 = hard corner.
    void setKneeDb (double kneeDb) noexcept { kneeDb_ = std::max (kneeDb, 0.0); }

    [[nodiscard]] double getCeilingDb() const noexcept { return ceilingDb_; }
    [[nodiscard]] double getKneeDb()    const noexcept { return kneeDb_; }

    /// Gain reduction in dB for an input at `levelDb`. Always <= 0.
    ///
    /// The quadratic knee is the standard Reiss/Giannoulis form specialised to
    /// an infinite ratio. Written with the knee *width* equal to twice the Knee
    /// control, so that the curve starts bending exactly `knee` dB below the
    /// ceiling and flattens exactly at it -- which is what the control claims
    /// to do, and what makes Ceiling mean the level it says.
    [[nodiscard]] double computeGainReductionDb (double levelDb) const noexcept
    {
        const double kneeWidth = 2.0 * kneeDb_;
        const double overshoot = levelDb - ceilingDb_;

        if (kneeWidth <= 0.0)
            return overshoot <= 0.0 ? 0.0 : -overshoot;

        if (overshoot <= -kneeDb_)
            return 0.0;

        if (overshoot >= kneeDb_)
            return -overshoot;

        const double intoKnee = overshoot + kneeDb_;
        return -(intoKnee * intoKnee) / (2.0 * kneeWidth);
    }

private:
    double ceilingDb_ { -0.3 };
    double kneeDb_    { 6.0 };
};

} // namespace tezla::dsp
