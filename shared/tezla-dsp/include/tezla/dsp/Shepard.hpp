// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The endless rise: N copies an octave apart, sliding under a fixed window.
//
// ---------------------------------------------------------------------------
// What the illusion is
// ---------------------------------------------------------------------------
//
// A Shepard tone is a chord of octaves whose loudness is a function of *pitch*
// rather than of which copy it is. Slide every copy upward together and each
// one walks up through that fixed loudness curve: it fades in at the bottom,
// swells through the middle, fades out at the top -- and the instant it
// vanishes it reappears an N-octave drop lower, where the curve is silent
// again. Nothing ever stops rising and nothing ever gets higher.
//
// It is the sound of dread in film scoring, and this instrument is four lines
// of arithmetic away from it because `UnisonBank` already runs N oscillators
// with a per-copy pitch and a per-copy gain. So this header is those four
// lines and the argument for them; the bank is told the answer and never
// learns what it means.
//
// ---------------------------------------------------------------------------
// The construction, and the two things that have to be true
// ---------------------------------------------------------------------------
//
// With a shared phase p and N copies, copy k sits at
//
//     u_k     = frac (p + k / N)                 -- its place on the ramp
//     cents_k = 1200 * (N * u_k - N / 2)         -- so copies are an octave apart
//     gain_k  = 0.5 * (1 - cos (2*pi * u_k))     -- the window
//
// **The seam has to be silent.** Copy k's frequency jumps by N octaves the
// instant u_k wraps through zero -- and `0.5 * (1 - cos 0)` is exactly 0.0
// there, so the jump happens at silence. Note what is *not* reset: the
// oscillator's own phase. Only its increment changes, at zero amplitude, so
// there is no waveform discontinuity anywhere in the mechanism rather than one
// small enough to get away with. That is why the raised cosine and not, say, a
// Gaussian, which is never quite zero and would tick forever.
//
// **The level has to be flat**, or the rise is a rise with a tremolo in it.
// The window's summed *power* is what matters, since the copies are at
// different frequencies and add incoherently, and it is exactly constant for
// three copies or more:
//
//     sum gain_k^2 = 0.375 * N       for N >= 3
//
// The identity is that sum(cos t_k) = 0 for N >= 2 and sum(cos 2 t_k) = 0 for
// N >= 3, both being sums of roots of unity. Measured over 2048 phase steps
// before it was written down:
//
//     N | sum gain^2      | ripple
//    ---|-----------------|----------
//     1 | 0 .. 1          | 1.000e+00
//     2 | 0.5 .. 1.0      | 5.000e-01
//     3 | 1.125           | 1.776e-15
//     4 | 1.500           | 1.110e-15
//     5 | 1.875           | 2.220e-15
//     6 | 2.250           | 3.109e-15
//     7 | 2.625           | 3.553e-15
//
// So **a Shepard stack wants at least three copies** -- at two it beats at the
// glissando rate and the illusion becomes an effect. Seven is what it is for.
// `tests/test_Shepard.cpp` pins those figures, and N = 2 is the break-check
// that proves the assertion has teeth.
//
// The level follows for free: `UnisonBank` divides by the root of the summed
// power, so a windowed stack is put back exactly where a flat one sits. That is
// a factor of 1/sqrt(0.375) = 1.633, or **4.26 dB**, which the bank applies
// without a special case because the general expression already says it.

#include <cmath>

namespace tezla::dsp {

/// The loudness curve a Shepard stack slides through, over one full turn.
///
/// Exactly 0 at both ends, which is the whole design -- see the header.
[[nodiscard]] inline double shepardWindow (double u) noexcept
{
    return 0.5 * (1.0 - std::cos (6.283185307179586 * u));
}

/// The summed power of `count` copies of the window, evenly spaced.
///
/// `0.375 * count` for count >= 3, and it is the bank's normalisation. Given a
/// name because it is a claim, and a claim should be assertable.
[[nodiscard]] inline double shepardWindowPower (int count) noexcept
{
    return count >= 3 ? 0.375 * static_cast<double> (count) : 0.0;
}

/// Fills `cents[0..count)` and `gains[0..count)` for glissando phase `phase`.
///
/// `phase` advances by one per full turn of the window and may be any real
/// number; it is wrapped here. One turn moves every copy by **one octave**, so
/// a caller's rate control reads directly in octaves per second and the
/// illusion repeats once per octave.
///
/// Either pointer may be null if only the other is wanted.
inline void shepardRanks (double phase, int count, double* cents, double* gains) noexcept
{
    if (count <= 0)
        return;

    const double span = static_cast<double> (count);

    for (int k = 0; k < count; ++k)
    {
        const double raw = phase + static_cast<double> (k) / span;

        // Wrapped rather than fmod'd, so a negative phase -- a falling
        // glissando that has run for a while -- lands in [0, 1) like any other.
        double u = raw - std::floor (raw);

        // std::floor is exact and the subtraction is exact for the magnitudes
        // this sees, but a phase large enough to lose its fraction would round
        // to exactly 1.0 rather than 0.0. Clamping there costs nothing and
        // keeps the window's "exactly zero at the seam" claim true forever.
        if (! (u < 1.0))
            u = 0.0;

        if (cents != nullptr)
            cents[k] = 1200.0 * (span * u - span * 0.5);

        if (gains != nullptr)
            gains[k] = shepardWindow (u);
    }
}

} // namespace tezla::dsp
