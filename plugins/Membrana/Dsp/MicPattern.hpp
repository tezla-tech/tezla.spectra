// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The first-order microphone, exactly.
//
// A point source radiates p(r) = (A/r) e^(-jkr) with k = w/c. A pressure
// (omni) capsule reads p; a gradient capsule reads dp/dr, proportional to
// (1/r + jk) p. Gradient mics are equalised flat for the far field (kr >> 1),
// so what remains audible of the near field is the factor (1 + 1/(jkr)) --
// the proximity effect. Nothing about it is a "bass boost" bolted on: it is
// the 1/r term of the spherical wave refusing to vanish when r is small.
//
// A first-order mic mixes the two capsules with pattern parameter a
// (a = 1 omni, 0.5 cardioid, 0 figure-8). At off-axis angle theta the
// equalised response to our point source, relative to the flat far field, is
//
//     H_mic(s) = D + G * w_p / s          (one real first-order section)
//       D   = a + (1-a) cos(theta)        pattern level at theta
//       G   = (1-a) cos(theta)            gradient weight at theta
//       w_p = c / r                       the proximity corner, in rad/s
//
// Everything couples correctly by construction, which is the reason to model
// the mechanism instead of drawing curves: on axis every pattern has D = 1
// and proximity weight (1-a); a cardioid at 90 degrees is -6.02 dB with
// exactly zero proximity; an omni never has any, at any distance. Those
// zeros are structural -- (1-a) is exactly 0.0 when a == 1.0, and the boost
// formula collapses to 0 dB in double precision at 90 degrees -- and the
// tests assert them as exact, not near.
//
// The magnitude of the proximity term, which is what the tests pin:
//
//     boost_dB(f) = 10 log10( 1 + ( G c / (2 pi f r D) )^2 )
//
// Cardioid at 5 cm on axis: +3.01 dB at 546 Hz, +14.89 dB at 100 Hz. A
// figure-8 doubles G, so its corner sits one octave up at the same distance.
// Doubling r moves the corner down exactly one octave -- r is linear in the
// denominator.
//
// The pole at s = 0 is the ideal gradient blowing up at DC. A real
// diaphragm's own resonance bounds it; here that is the plugin's lowLimitHz
// second-order highpass, applied where this target is realised (CapsuleEq),
// never inside this closed form.
//
// This header is the math core only: pure functions of (a, cos theta, r, f),
// evaluated at design time. The realisation as filters -- normalised against
// the reference condition r = 1 m on axis so that the default settings are
// bit-exact identity -- is CapsuleEq's job.
//
// Valid range: the plugin exposes axisDeg 0..90, so cosTheta >= 0 and
// D >= a >= 0. The formulas remain honest behind the mic (a rear null has
// level 0 and a proximity ratio that genuinely diverges), but nothing here
// clamps for it; the parameter range is the guard.

#include <cmath>
#include <numbers>

#include <tezla/dsp/Exact.hpp>

namespace tezla::membrana {

struct MicPattern
{
    /// Dry air at ~20 C, in m/s. One constant shared by every distance-to-
    /// frequency conversion in the plugin, so the sphere model and the
    /// proximity model can never disagree about what a centimetre costs.
    static constexpr double kSpeedOfSound = 343.0;

    /// D: the pattern's level at incidence angle theta, as a linear gain.
    /// a = 1 omni, 0.5 cardioid, 0 figure-8.
    static constexpr double level (double a, double cosTheta) noexcept
    {
        return a + (1.0 - a) * cosTheta;
    }

    /// G: the weight of the gradient (velocity) component at theta. The
    /// share of the output that carries proximity. Exactly 0.0 for an omni.
    static constexpr double gradientWeight (double a, double cosTheta) noexcept
    {
        return (1.0 - a) * cosTheta;
    }

    /// The proximity corner: the frequency at which the boost passes
    /// +3.01 dB. G c / (2 pi r D). Cardioid, 5 cm, on axis: 545.9 Hz.
    static double cornerHz (double a, double cosTheta, double rMetres) noexcept
    {
        const double g = gradientWeight (a, cosTheta);
        const double d = level (a, cosTheta);
        return (g * kSpeedOfSound) / (2.0 * std::numbers::pi * rMetres * d);
    }

    /// The proximity boost at frequency f, in dB, relative to the mic's own
    /// far-field (flat) response at the same angle. The pattern level D is
    /// NOT included -- level() carries that separately, so a test can pin
    /// "cardioid at 90 degrees: level -6.02 dB, boost exactly 0 dB" as two
    /// distinct facts.
    ///
    /// The zero-gradient case returns by predicate, not arithmetic: an omni
    /// (G exactly 0.0 because 1-a is) must read 0.000 dB at every frequency
    /// and every distance, bit-exactly.
    static double boostDb (double a, double cosTheta, double rMetres, double fHz) noexcept
    {
        const double g = gradientWeight (a, cosTheta);

        if (tezla::dsp::isExactlyZero (g))
            return 0.0;

        const double d     = level (a, cosTheta);
        const double ratio = (g * kSpeedOfSound)
                             / (2.0 * std::numbers::pi * fHz * rMetres * d);
        return 10.0 * std::log10 (1.0 + ratio * ratio);
    }
};

} // namespace tezla::membrana
