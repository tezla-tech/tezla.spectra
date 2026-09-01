// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// What an object's overtones ARE -- the mode-ratio tables behind Malleus,
// each derived from the physics rather than copied (CLAUDE.md section 2.1),
// plus the three operations Malleus performs on them: the material morph,
// the inharmonicity stretch, and the Overtone Lock that snaps a partial onto
// the loaded scale.
//
// The tables (ratios of each mode's frequency to the PRIME -- the partial the
// ear takes as the pitch, which is ratio 1.0 in every table):
//
//   String    the harmonic series, n. The stiff string's n*sqrt(1 + B n^2)
//             is the physical anchor of the Stretch control below: one power
//             law serves every material instead of a per-material constant.
//   Bar       free-free transverse beam (Euler-Bernoulli): f_n ~ x_n^2 where
//             cos(x_n) cosh(x_n) = 1. Root-found here by bisection; the
//             classic ratios 1 : 2.756 : 5.404 : 8.933 fall out and the test
//             pins them.
//   Membrane  ideal circular membrane: the zeros of the Bessel functions
//             J_m, all (m, k) modes collected and sorted. J_m comes from
//             `besselJ` below rather than std::cyl_bessel_j, which libc++
//             does not implement; design time only, and pinned against
//             2.405 / 3.832 / 5.136 / 5.520.
//   Plate     simply supported rectangular plate, aspect sqrt(2):
//             f ~ m^2/2 + n^2, exact closed form, sorted and deduplicated.
//   Bell      the canonical minor-third church bell: hum 1/2, prime 1,
//             tierce 6/5, quint 3/2, nominal 2, superquint 3, octave
//             nominal 4. Founders shaped this profile empirically -- there is
//             no closed form -- so those seven are taken from the standard
//             organology literature (see DSP-REFERENCES). Above them the
//             table continues as a mildly stretched harmonic series on the
//             nominal; that continuation is a VOICING CHOICE and is stated
//             as one, not claimed as physics. Note the hum sits BELOW the
//             prime: a struck bell carries its own sub-octave, which on this
//             rig is a feature.
//
// Everything transcendental happens once, at first use, into static tables;
// per-voice work at control rate is multiplies and one log per locked mode.

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "Exact.hpp"
#include "Tuning.hpp"

namespace tezla::dsp {

/// Bessel function of the first kind, integer order, by the trapezoidal rule
/// on Bessel's own integral:
///
///     J_m(x) = (1 / 2pi) * integral over [0, 2pi] of cos(m*t - x*sin t) dt
///
/// The integrand is periodic and analytic, so the trapezoidal rule converges
/// geometrically rather than at any polynomial order -- the equally spaced
/// average *is* the spectrally accurate quadrature here, which is why 256
/// points is enough and why the loop looks too simple to be right.
///
/// **Why not `std::cyl_bessel_j`**, which this used to call: it is C++17, and
/// **libc++ does not implement it**. Apple clang refuses the call outright, so
/// Malleus had never once compiled on macOS -- libstdc++ and MSVC both provide
/// it, so neither Linux nor Windows had any reason to notice. It surfaced the
/// first time a macOS runner reached this header.
///
/// Verified against `std::cyl_bessel_j` on libstdc++ over the entire range
/// this file asks for -- m = 0..20, x = 0..60 in steps of 0.01 -- with a worst
/// absolute difference of **8.861e-15**, at m = 16, x = 59.80. Design time
/// only: nothing calls this from an audio callback.
[[nodiscard]] inline double besselJ (int m, double x) noexcept
{
    constexpr int kSteps = 256;

    const double twoPi = 2.0 * std::numbers::pi;
    double sum = 0.0;

    for (int i = 0; i < kSteps; ++i)
    {
        const double theta = twoPi * static_cast<double> (i) / static_cast<double> (kSteps);
        sum += std::cos (static_cast<double> (m) * theta - x * std::sin (theta));
    }

    return sum / static_cast<double> (kSteps);
}

enum class ModeMaterial
{
    String = 0,
    Bar,
    Membrane,
    Plate,
    Bell,
    count
};

class ModeShapes
{
public:
    static constexpr int kMaxModes = 64;

    using Table = std::array<double, kMaxModes>;

    /// The pure material's ratio table, ascending, prime at exactly 1.0.
    /// Built on first use (thread-safe magic static), then a plain read.
    [[nodiscard]] static const Table& table (ModeMaterial material)
    {
        static const Table string = buildString();
        static const Table bar = buildBar();
        static const Table membrane = buildMembrane();
        static const Table plate = buildPlate();
        static const Table bell = buildBell();

        switch (material)
        {
            case ModeMaterial::String:   return string;
            case ModeMaterial::Bar:      return bar;
            case ModeMaterial::Membrane: return membrane;
            case ModeMaterial::Plate:    return plate;
            case ModeMaterial::Bell:     return bell;
            case ModeMaterial::count:    break;
        }

        return string;
    }

    /// One mode's ratio at a point on the material morph, with stretch.
    ///
    /// `materialPosition` runs 0..4 across String, Bar, Membrane, Plate,
    /// Bell in that order; integer positions ARE the pure tables, to the
    /// bit, and the space between interpolates in log-frequency (ratios are
    /// multiplicative objects; interpolating them linearly would bend every
    /// intermediate object sharp).
    ///
    /// `stretch` is the inharmonicity power law: ratio^(1 + stretch), so 0
    /// is the physical table, positive stretches the overtones apart
    /// (the stiff-string direction, taken far past any real string), and
    /// negative squeezes them in. The prime, at ratio 1, is a fixed point
    /// for every stretch -- the pitch never moves, only the colour.
    [[nodiscard]] static double ratioAt (double materialPosition, int mode,
                                         double stretch)
    {
        if (mode < 0 || mode >= kMaxModes)
            return 1.0;

        constexpr double top = static_cast<double> (ModeMaterial::count) - 1.0;
        const double position = materialPosition < 0.0 ? 0.0
                              : materialPosition > top ? top
                              : materialPosition;

        const auto lower = static_cast<int> (position);
        const double frac = position - lower;

        double ratio;

        if (frac <= 0.0)
        {
            ratio = table (static_cast<ModeMaterial> (lower))[static_cast<std::size_t> (mode)];
        }
        else
        {
            const double a = table (static_cast<ModeMaterial> (lower))[static_cast<std::size_t> (mode)];
            const double b = table (static_cast<ModeMaterial> (lower + 1))[static_cast<std::size_t> (mode)];

            ratio = std::exp ((1.0 - frac) * std::log (a) + frac * std::log (b));
        }

        if (isExactlyZero (stretch))
            return ratio;

        return std::pow (ratio, 1.0 + stretch);
    }

    /// The Overtone Lock: snaps `frequencyHz` onto the scale lattice rooted
    /// at the voice's own fundamental, by `amount` (0 leaves it untouched,
    /// exactly; 1 lands it on the nearest degree; between blends in cents).
    ///
    /// The lattice is the scale's degrees replicated at its repeat interval
    /// in both directions -- so a bell's hum at half the fundamental snaps
    /// into the repeat below, and Bohlen-Pierce's 3/1 tritave works exactly
    /// like an octave would. This is what makes a gong agree with the
    /// gamelan: the object's partials land where the scale's notes are.
    [[nodiscard]] static double lockToScale (double frequencyHz,
                                             double fundamentalHz,
                                             const Scale& scale,
                                             double amount)
    {
        if (amount <= 0.0 || frequencyHz <= 0.0 || fundamentalHz <= 0.0
            || ! (scale.repeat > 1.0) || scale.ratios.empty())
            return frequencyHz;

        const double x = frequencyHz / fundamentalHz;
        const double logRepeat = std::log (scale.repeat);

        // Which repeat the partial falls in, and where within it.
        const double k = std::floor (std::log (x) / logRepeat);
        const double base = x / std::pow (scale.repeat, k);   // in [1, repeat)

        // Nearest degree in log distance -- the top of the repeat (the next
        // tonic) is a candidate too, or a partial just under the repeat
        // would be dragged flat across half the top interval.
        const double logBase = std::log (base);

        double bestLog = 0.0;   // degree 0, ratio 1
        double bestDistance = std::abs (logBase);

        for (const double ratio : scale.ratios)
        {
            const double candidate = std::log (ratio);
            const double distance = std::abs (logBase - candidate);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestLog = candidate;
            }
        }

        if (std::abs (logBase - logRepeat) < bestDistance)
            bestLog = logRepeat;

        const double snapped = fundamentalHz * std::exp (bestLog + k * logRepeat);

        if (amount >= 1.0)
            return snapped;

        // Blend in log-frequency: "70% locked" means 70% of the cents moved.
        return std::exp ((1.0 - amount) * std::log (frequencyHz)
                         + amount * std::log (snapped));
    }

private:
    [[nodiscard]] static Table buildString()
    {
        Table t {};

        for (int n = 0; n < kMaxModes; ++n)
            t[static_cast<std::size_t> (n)] = static_cast<double> (n + 1);

        return t;
    }

    /// Roots of cos(x) cosh(x) = 1 by bisection. The roots sit near
    /// (2n + 1) pi / 2; bracket each and bisect. cosh stays finite well past
    /// the 64th root (x ~ 200, cosh ~ 1e87, double max 1.8e308).
    [[nodiscard]] static Table buildBar()
    {
        const auto f = [] (double x) { return std::cos (x) * std::cosh (x) - 1.0; };

        Table t {};
        double firstSquared = 0.0;

        for (int n = 0; n < kMaxModes; ++n)
        {
            const double centre = (2.0 * n + 3.0) * std::numbers::pi / 2.0;
            double low = centre - std::numbers::pi / 2.0;
            double high = centre + std::numbers::pi / 2.0;

            // The bracket must straddle a sign change; nudge until it does.
            while (f (low) * f (high) > 0.0 && high - low > 1.0e-6)
                low += (high - low) * 0.1;

            for (int iteration = 0; iteration < 80; ++iteration)
            {
                const double mid = 0.5 * (low + high);
                (f (low) * f (mid) <= 0.0 ? high : low) = mid;
            }

            const double root = 0.5 * (low + high);

            if (n == 0)
                firstSquared = root * root;

            t[static_cast<std::size_t> (n)] = root * root / firstSquared;
        }

        return t;
    }

    /// All (m, k) Bessel-zero modes of the ideal circular membrane, sorted.
    /// Zeros of J_m found by scanning for sign changes and bisecting --
    /// `besselJ` above does the evaluation, because std::cyl_bessel_j does not
    /// exist on libc++. Design time only.
    [[nodiscard]] static Table buildMembrane()
    {
        std::vector<double> zeros;

        for (int m = 0; m <= 20; ++m)
        {
            const auto jm = [m] (double x)
            {
                return besselJ (m, x);
            };

            double previousX = m + 0.1;   // J_m's first zero lies above m
            double previousValue = jm (previousX);

            for (double x = previousX + 0.05; x < 60.0; x += 0.05)
            {
                const double value = jm (x);

                if (previousValue * value < 0.0)
                {
                    double low = previousX, high = x;

                    for (int iteration = 0; iteration < 60; ++iteration)
                    {
                        const double mid = 0.5 * (low + high);
                        (jm (low) * jm (mid) <= 0.0 ? high : low) = mid;
                    }

                    zeros.push_back (0.5 * (low + high));
                }

                previousX = x;
                previousValue = value;
            }
        }

        std::sort (zeros.begin(), zeros.end());

        Table t {};

        for (int n = 0; n < kMaxModes; ++n)
            t[static_cast<std::size_t> (n)] = zeros[static_cast<std::size_t> (n)]
                                                / zeros.front();

        return t;
    }

    /// Simply supported rectangular plate, aspect sqrt(2): f ~ m^2/2 + n^2.
    /// Exact closed form; duplicates within a cent are merged (a degenerate
    /// pair is one mode struck twice, not two lines).
    [[nodiscard]] static Table buildPlate()
    {
        std::vector<double> values;

        for (int m = 1; m <= 16; ++m)
            for (int n = 1; n <= 16; ++n)
                values.push_back (0.5 * m * m + static_cast<double> (n * n));

        std::sort (values.begin(), values.end());

        std::vector<double> distinct;

        for (const double value : values)
            if (distinct.empty() || value > distinct.back() * 1.0006)
                distinct.push_back (value);

        Table t {};

        for (int n = 0; n < kMaxModes; ++n)
            t[static_cast<std::size_t> (n)] = distinct[static_cast<std::size_t> (n)]
                                                / distinct.front();

        return t;
    }

    /// The canonical seven, then the stated voicing continuation: partials
    /// above the octave nominal continue as a 2% stretched harmonic series
    /// on the nominal, which keeps a big bell's upper shimmer inharmonic
    /// the way its lower profile already is.
    [[nodiscard]] static Table buildBell()
    {
        Table t {};

        const double canonical[] { 0.5, 1.0, 1.2, 1.5, 2.0, 3.0, 4.0 };

        for (int n = 0; n < 7; ++n)
            t[static_cast<std::size_t> (n)] = canonical[n];

        for (int n = 7; n < kMaxModes; ++n)
            t[static_cast<std::size_t> (n)] =
                2.0 * std::pow (static_cast<double> (n - 4), 1.02);

        return t;
    }
};

} // namespace tezla::dsp
