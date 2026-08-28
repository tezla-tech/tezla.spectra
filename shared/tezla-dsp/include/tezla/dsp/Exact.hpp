// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Exactly zero, said deliberately.
//
// -Wfloat-equal is right nearly everywhere. Comparing two computed floats for
// equality is almost always a bug waiting for a rounding difference, and a
// compiler that says so is doing its job.
//
// It is wrong in one place, and that place matters here: CLAUDE.md section 7
// requires that "any stage permanently in the signal path needs a bit-exact
// bypass at its neutral setting, not merely a transparent one". The way that
// gets built is a branch that skips the stage when its control sits at exactly
// zero -- not near zero, exactly. A tolerance there would be the bug: it would
// make a very small setting silently do nothing, and the boundary at which it
// started doing something would be arbitrary.
//
// So the comparison stays, and it gets a name. One place where the exception is
// declared and justified, rather than nine separate `== 0.0` comparisons each
// looking to a reader like an oversight the compiler caught.
//
// There is a second such place, and CLAUDE.md section 7 makes it a rule rather
// than a habit: **any setter that clears state must refuse a no-op, and the
// guard goes in the setter.** That guard is an equality test between the value
// asked for and the value already held -- and it has to be exact for the same
// reason as above. A tolerance would let a small move be silently ignored, and
// the threshold at which the control started working would be arbitrary.
//
// Neither is for asking whether a measurement came out at zero, or whether two
// measurements agree. Those questions want a tolerance, and the tolerance is
// part of the claim being made -- see the tolerances in tests/ for what that
// looks like.

namespace tezla::dsp {

/// True only for +0.0 and -0.0. NaN is false, as it is for `== 0`.
template <typename Float>
[[nodiscard]] constexpr bool isExactlyZero (Float value) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

    return value == static_cast<Float> (0);

#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
#endif
}

/// True only when the two are the same value, bit for bit. NaN is never equal
/// to anything, including itself, exactly as `==` says.
///
/// For the no-op guards CLAUDE.md section 7 requires in any setter that clears
/// state, and for nothing else. Two computed quantities that ought to agree are
/// a job for a tolerance.
template <typename Float>
[[nodiscard]] constexpr bool isExactly (Float a, Float b) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

    return a == b;

#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
#endif
}

} // namespace tezla::dsp
