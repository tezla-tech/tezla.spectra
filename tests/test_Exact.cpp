// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <limits>

#include <tezla/dsp/Exact.hpp>

using namespace tezla::dsp;

// isExactlyZero is one line, and it is now the guard on every bit-exact neutral
// setting in the suite -- Emberdrive's and Halo's modulation offsets, Halo's
// stereo width, and whether the modulation path runs at all. A later edit that
// "tidied" it into a tolerance would loosen all of those at once and nothing
// else would notice, because a tolerance still returns true for zero.

TEZLA_TEST (exactly_zero_is_true_for_both_zeroes)
{
    CHECK (isExactlyZero (0.0));
    CHECK (isExactlyZero (-0.0));
    CHECK (isExactlyZero (0.0f));
    CHECK (isExactlyZero (-0.0f));
}

TEZLA_TEST (exactly_zero_is_false_for_anything_that_is_not_zero)
{
    // The whole point. A tolerance would swallow these, and a modulation slot
    // set to a very small depth would silently stop modulating -- with the
    // threshold at which it started again decided by whoever picked the
    // tolerance rather than by the user.
    CHECK (! isExactlyZero (std::numeric_limits<double>::denorm_min()));
    CHECK (! isExactlyZero (-std::numeric_limits<double>::denorm_min()));
    CHECK (! isExactlyZero (1.0e-300));
    CHECK (! isExactlyZero (1.0e-12));
    CHECK (! isExactlyZero (1.0e-9));
    CHECK (! isExactlyZero (std::numeric_limits<float>::denorm_min()));
    CHECK (! isExactlyZero (1.0e-30f));
}

TEZLA_TEST (exactly_zero_treats_nan_the_way_the_comparison_it_replaced_did)
{
    // `x == 0.0` is false for NaN, and so is this. Worth pinning because the
    // obvious way to write an exact-zero test without tripping -Wfloat-equal --
    // `!(x < 0) && !(x > 0)` -- is true for NaN, which would send a NaN down
    // the neutral path and hide it instead of letting it show.
    const double nan = std::numeric_limits<double>::quiet_NaN();

    CHECK (! isExactlyZero (nan));
    CHECK (! isExactlyZero (std::numeric_limits<double>::infinity()));
    CHECK (! isExactlyZero (-std::numeric_limits<double>::infinity()));
}

TEZLA_TEST (exactly_zero_works_at_compile_time)
{
    // constexpr, so it costs nothing where the value is known and can be used
    // in a static_assert.
    static_assert (isExactlyZero (0.0));
    static_assert (! isExactlyZero (1.0e-300));
    CHECK (true);
}

TEZLA_TEST (exactly_zero_is_the_comparison_it_replaced)
{
    // The substitution proof. Nine call sites were `x == 0.0` before this
    // existed, three of them in an audio path, and the claim being made is that
    // nothing about their behaviour changed. So assert it directly, against the
    // raw comparison, across every class of value a double can take.
    //
    // tests/ does not build with -Wfloat-equal, which is why the right-hand
    // side can be written out here at all.
    const double cases[] {
        0.0, -0.0,
        std::numeric_limits<double>::denorm_min(),
        -std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::epsilon(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
        1.0, -1.0, 0.5, -0.5, 1.0e-300, -1.0e-300, 1.0e300,
    };

    bool identical = true;

    for (const double value : cases)
        if (isExactlyZero (value) != (value == 0.0))
            identical = false;

    // And a sweep of ordinary values, both signs, either side of zero.
    for (int i = -2000; i <= 2000; ++i)
    {
        const double value = static_cast<double> (i) * 1.0e-4;

        if (isExactlyZero (value) != (value == 0.0))
            identical = false;
    }

    CHECK (identical);
}

TEZLA_TEST (is_exactly_is_the_no_op_guard_and_nothing_looser)
{
    // CLAUDE.md section 7: any setter that clears state must refuse a no-op,
    // and the guard goes in the setter. That guard is an equality test, and it
    // has to be exact -- a tolerance would let a small move be silently
    // ignored and put the threshold at which the control starts working
    // somewhere arbitrary.
    CHECK (isExactly (1.0, 1.0));
    CHECK (isExactly (0.0, -0.0));          // as `==` says: the two zeroes agree
    CHECK (! isExactly (1.0, 1.0 + 1.0e-15));

    // NaN is equal to nothing, itself included, which is exactly what `==` does
    // and what a guard wants: a NaN target must not be mistaken for a no-op.
    const double nan = std::numeric_limits<double>::quiet_NaN();

    CHECK (! isExactly (nan, nan));
    CHECK (! isExactly (nan, 0.0));

    // Not a tolerance in disguise. Two values a millionth apart are different
    // values, and a setter must act on the difference.
    CHECK (! isExactly (1000.0, 1000.000001));
    CHECK (! isExactly (0.0, 1.0e-300));
}
