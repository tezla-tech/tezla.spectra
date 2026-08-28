// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/Decibels.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 192000.0;

double sine (int index, double hz)
{
    return 0.6 * std::sin (2.0 * std::numbers::pi * hz * static_cast<double> (index) / kRate);
}
} // namespace

// What this filter is for -- offset removed, sub bass kept -- is already covered
// by dc_blocker_removes_offset_but_keeps_sub_bass and
// dc_blocker_corner_is_sample_rate_independent. These two are about changing the
// corner while audio is running, which nothing checked.

TEZLA_TEST (dc_blocker_retune_does_not_step_the_output)
{
    // The bug this exists for, and it was in shipped code: Emberdrive's expert
    // DC corner is a continuous, automatable control, and every change ran the
    // blocker through prepare() -- which resets. A first-order highpass whose
    // memory has been zeroed puts out `x` rather than `x - x[n-1] + R*y[n-1]`,
    // so the output steps by the whole previous sample. Once per knob move that
    // is a tick; under modulation it is a tick every 32 samples.
    //
    // Measured on the plugin before the fix, the chunk boundaries were four
    // times as rough as the signal between them. Afterwards, 1.00.
    const auto stepAtChange = [] (bool useRetune)
    {
        DcBlocker<double> blocker;
        blocker.prepare (kRate, 10.0);

        // Well past any settling, and away from a zero crossing so a lost state
        // has something to show.
        constexpr int kChangeAt = 60000;

        double previous = 0.0;
        double before = 0.0;

        for (int i = 0; i < kChangeAt; ++i)
        {
            const double y = blocker.process (sine (i, 90.0) + 0.2);
            before = std::abs (y - previous);
            previous = y;
        }

        if (useRetune)
            blocker.retune (kRate, 30.0);
        else
            blocker.prepare (kRate, 30.0);

        const double after = std::abs (blocker.process (sine (kChangeAt, 90.0) + 0.2) - previous);

        // How much bigger the step across the change is than the step either
        // side of it. One means the change is invisible.
        return after / std::max (before, 1.0e-12);
    };

    // prepare() is the old path, and it is what makes this test mean something:
    // without it, a broken retune would pass by being equally broken.
    CHECK (stepAtChange (false) > 50.0);
    CHECK (stepAtChange (true) < 2.0);
}

TEZLA_TEST (dc_blocker_retune_actually_moves_the_corner)
{
    // Preserving the state would be easy to get right by doing nothing at all,
    // so this asserts the coefficient really changed: 5 Hz passes a 20 Hz tone
    // nearly whole, 40 Hz takes several dB off it.
    const auto levelAt = [] (double cornerHz, double toneHz)
    {
        DcBlocker<double> blocker;
        blocker.prepare (kRate, 1.0);
        blocker.retune (kRate, cornerHz);

        double sumSquares = 0.0;
        int counted = 0;

        for (int i = 0; i < 400000; ++i)
        {
            const double y = blocker.process (sine (i, toneHz));

            if (i > 200000)
            {
                sumSquares += y * y;
                ++counted;
            }
        }

        // RMS, never peak: three samples per cycle reads 0.866 of the amplitude
        // and looks exactly like a filter 1.2 dB down. See CLAUDE.md section 10.
        return gainToDb (std::sqrt (sumSquares / counted) / (0.6 / std::numbers::sqrt2), -200.0);
    };

    const double gentle = levelAt (5.0, 20.0);
    const double steep  = levelAt (40.0, 20.0);

    CHECK (gentle > -0.5);
    CHECK (steep < -4.0);
    CHECK (gentle - steep > 3.0);
}
