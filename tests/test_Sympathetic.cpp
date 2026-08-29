// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tezla/dsp/Tuning.hpp>

#include "Exciters.hpp"
#include "SympatheticBank.hpp"

using tezla::dsp::Tuning;
using tezla::malleus::NoiseBurst;
using tezla::malleus::SympatheticBank;

namespace
{
/// Power of `x` at one frequency (Goertzel-style projection).
[[nodiscard]] double powerAt (const std::vector<double>& x, double hz, double fs)
{
    double re = 0.0;
    double im = 0.0;

    for (std::size_t n = 0; n < x.size(); ++n)
    {
        const double phase = 2.0 * std::numbers::pi * hz
                           * static_cast<double> (n) / fs;
        re += x[n] * std::cos (phase);
        im += x[n] * std::sin (phase);
    }

    return (re * re + im * im) / static_cast<double> (x.size() * x.size());
}

/// Excites a bank with a seeded noise burst and returns the ring-out tail.
[[nodiscard]] std::vector<double> ringOut (SympatheticBank& strings,
                                           double burstAmount, double seconds)
{
    NoiseBurst burst;
    burst.prepare (48000.0);
    burst.setSeed (2026);
    burst.trigger (0.9, burstAmount);

    std::vector<double> tail;
    const int total = static_cast<int> (seconds * 48000.0);

    for (int n = 0; n < total; ++n)
    {
        const double y = strings.process (burst.next());

        if (n >= total / 2)
            tail.push_back (y);
    }

    return tail;
}
} // namespace

TEZLA_TEST (the_strings_answer_on_the_scale_degrees)
{
    // Seven strings tuned off dsp::Tuning (12-TET default), fed a noise
    // burst through the coupling -- pure broadband excitation, no pitch of
    // its own. What rings back must be the DEGREES: projected power on
    // each string's fundamental against probes 1.5 semitones off every
    // string. Brightness 0 keeps the upper partials silent so the probes
    // are provably in empty space.
    Tuning tuning;

    const int notes[] { 48, 52, 55, 59, 62, 65, 69 };
    double frequencies[7];

    for (int s = 0; s < 7; ++s)
        frequencies[s] = tuning.frequencyFor (notes[s]);

    SympatheticBank strings;
    strings.prepare (48000.0);
    strings.setStrings (frequencies, 7, 6.0, 0.0);
    strings.setCoupling (1.0);
    strings.setDrone (0.0);

    const auto tail = ringOut (strings, 1.0, 2.0);

    double onDegree = 1.0e300;
    double offDegree = 0.0;

    for (int s = 0; s < 7; ++s)
    {
        onDegree = std::min (onDegree, powerAt (tail, frequencies[s], 48000.0));
        offDegree = std::max (offDegree,
                              powerAt (tail, frequencies[s] * std::pow (2.0, 1.5 / 12.0),
                                       48000.0));
    }

    std::printf ("        [taraf] weakest degree %.4g, loudest off-degree %.4g (x%.0f)\n",
                 onDegree, offDegree, onDegree / offDegree);

    // Every degree rings; nothing between them does.
    CHECK (onDegree > 30.0 * offDegree);

    // Brightness opens the octave partial. Measured on ONE string at
    // 200 Hz so the 400 Hz probe sits in provably empty space -- with
    // seven strings the probe reads the neighbours' spectral skirts
    // instead of the partial, which is how this check first failed.
    const auto octavePower = [] (double brightness)
    {
        const double oneString[] { 200.0 };

        SympatheticBank solo;
        solo.prepare (48000.0);
        solo.setStrings (oneString, 1, 6.0, brightness);
        solo.setCoupling (1.0);
        solo.setDrone (0.0);

        const auto ring = ringOut (solo, 1.0, 2.0);
        return powerAt (ring, 400.0, 48000.0);
    };

    const double partialDark = octavePower (0.0);
    const double partialBright = octavePower (0.8);

    std::printf ("        [taraf] octave partial power bright %.4g vs dark %.4g\n",
                 partialBright, partialDark);

    CHECK (partialBright > 30.0 * partialDark);
}

TEZLA_TEST (the_drone_sustains_what_the_strings_would_lose)
{
    // Activity, not silence (the zombie-voice lesson inverted): with drone
    // off, 3-second strings have measurably decayed 2 s after the burst;
    // with drone up, the loop keeps feeding them and the energy HOLDS.
    // Both stay bounded -- the tanh governs the level.
    const auto energyRatio = [] (double drone)
    {
        Tuning tuning;
        double frequencies[4];

        for (int s = 0; s < 4; ++s)
            frequencies[s] = tuning.frequencyFor (50 + 5 * s);

        SympatheticBank strings;
        strings.prepare (48000.0);
        strings.setStrings (frequencies, 4, 3.0, 0.5);
        strings.setCoupling (1.0);
        strings.setDrone (drone);

        NoiseBurst burst;
        burst.prepare (48000.0);
        burst.setSeed (7);
        burst.trigger (0.9, 2.0);

        double early = 0.0;
        double late = 0.0;

        for (int n = 0; n < 96000; ++n)
        {
            (void) strings.process (burst.next());

            if (n == 14400)
                early = strings.energy();
        }

        late = strings.energy();

        return late / early;
    };

    const double free = energyRatio (0.0);
    const double held = energyRatio (1.0);

    std::printf ("        [drone] energy at 2 s / 0.3 s: free %.4f, drone %.3f\n",
                 free, held);

    CHECK (free < 0.3);    // undriven strings die at their T60
    CHECK (held > 0.5);    // the drone keeps them singing
}

TEZLA_TEST (the_drone_plane_stays_bounded_with_the_clip_governing)
{
    // The section 7 sweep for this loop: the whole drone x coupling plane,
    // twelve maximum-length strings (T60 20 s), a hot object burst in --
    // everything finite, every peak under the pinned ceiling. The tanh
    // inside the loop is what turns "loop gain over one" into a level, and
    // removing it is the break-check's red state.
    Tuning tuning;
    double frequencies[12];

    for (int s = 0; s < 12; ++s)
        frequencies[s] = tuning.frequencyFor (48 + 3 * s);

    double worst = 0.0;

    for (int di = 0; di <= 10; ++di)
        for (int ci = 0; ci <= 10; ++ci)
        {
            SympatheticBank strings;
            strings.prepare (48000.0);
            strings.setStrings (frequencies, 12, 20.0, 0.7);
            strings.setCoupling (ci * 0.1);
            strings.setDrone (di * 0.1);

            NoiseBurst burst;
            burst.prepare (48000.0);
            burst.setSeed (99);
            burst.trigger (0.9, 5.0);

            for (int n = 0; n < 28800; ++n)
            {
                const double y = strings.process (burst.next());

                CHECK (std::isfinite (y));
                worst = std::max (worst, std::abs (y));
            }
        }

    std::printf ("        [drone sweep] worst peak %.3g across the plane\n", worst);

    CHECK (worst < 10.0);
}

TEZLA_TEST (an_unexcited_drone_cannot_self_start)
{
    // tanh(0) is exactly 0: full drone, full coupling, and a silent object
    // stay at bit-exact zero forever -- the loop has no noise floor to
    // bootstrap from (section 7's self-start rule).
    Tuning tuning;
    double frequencies[6];

    for (int s = 0; s < 6; ++s)
        frequencies[s] = tuning.frequencyFor (50 + 4 * s);

    SympatheticBank strings;
    strings.prepare (48000.0);
    strings.setStrings (frequencies, 6, 20.0, 1.0);
    strings.setCoupling (1.0);
    strings.setDrone (1.0);

    for (int n = 0; n < 5000; ++n)
        CHECK (strings.process (0.0) == 0.0);

    CHECK (strings.energy() == 0.0);

    // And reset is a true reset: ring it, wipe it, silent again.
    (void) strings.process (1.0);

    for (int n = 0; n < 100; ++n)
        (void) strings.process (0.0);

    CHECK (strings.energy() > 0.0);

    strings.reset();

    CHECK (strings.energy() == 0.0);

    for (int n = 0; n < 1000; ++n)
        CHECK (strings.process (0.0) == 0.0);
}
