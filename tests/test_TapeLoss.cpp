// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

#include <TapeLoss.hpp>

using namespace tezla::ferrite;

namespace
{
[[nodiscard]] TapeLoss makeLoss (double rate, double ips)
{
    TapeLoss loss;
    loss.prepare (rate);
    loss.setSpeedIps (ips);

    // Drain the crossfade the speed change started, so measurements see one
    // settled filter.
    for (int i = 0; i < 4096; ++i)
        (void) loss.process (0.0);

    return loss;
}

/// Steady-state gain of the WHOLE stage (FIR + bump) at one frequency, by
/// sine RMS in over RMS out.
[[nodiscard]] double renderedGainAt (TapeLoss& loss, double hz, double rate)
{
    const int settle = 4096;
    const int window = 8192;

    double sumIn = 0.0, sumOut = 0.0;

    for (int i = 0; i < settle + window; ++i)
    {
        const double input = std::sin (2.0 * 3.141592653589793 * hz * i / rate);
        const double output = loss.process (input);

        if (i >= settle)
        {
            sumIn += input * input;
            sumOut += output * output;
        }
    }

    return std::sqrt (sumOut / sumIn);
}
} // namespace

// ---------------------------------------------------------------------------
// The magnitude is the analytic curve
// ---------------------------------------------------------------------------

TEZLA_TEST (the_designed_filter_matches_the_analytic_loss_curve)
{
    // The whole licence for the minimum-phase construction: at four sample
    // rates and three speeds, the designed FIR's magnitude must sit on the
    // closed-form loss product wherever that product is louder than -40 dB.
    //
    // Measured worst error across every rate x speed x frequency: 0.23 dB.
    constexpr double spacing = 5.0, thickness = 35.0, gap = 2.5;

    double worstDb = 0.0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const double ips : { 7.5, 15.0, 30.0 })
        {
            auto loss = makeLoss (rate, ips);

            for (double hz = 100.0; hz < 0.42 * rate && hz <= 22000.0; hz *= 1.35)
            {
                const double analytic = TapeLoss::analyticMagnitude (
                    hz, ips, spacing, thickness, gap);

                if (analytic < 0.01)   // -40 dB: below this the match is moot
                    continue;

                const double designed = loss.designedMagnitudeAt (hz);
                const double errorDb = std::abs (
                    20.0 * std::log10 (designed / analytic));

                worstDb = std::max (worstDb, errorDb);
            }
        }

    CHECK (worstDb < 0.5);
}

TEZLA_TEST (halving_the_speed_moves_the_loss_down_an_octave)
{
    // The defining property of wavelength losses: the response at frequency
    // f and speed v equals the response at f/2 and v/2. Checked on the
    // analytic curve (arithmetic) AND on the designed filters (behaviour).
    const double atFifteen = TapeLoss::analyticMagnitude (8000.0, 15.0, 5.0, 35.0, 2.5);
    const double atSevenFive = TapeLoss::analyticMagnitude (4000.0, 7.5, 5.0, 35.0, 2.5);
    CHECK (std::abs (atFifteen - atSevenFive) < 1.0e-12);

    auto fast = makeLoss (96000.0, 15.0);
    auto slow = makeLoss (96000.0, 7.5);

    const double fastDesigned = fast.designedMagnitudeAt (8000.0);
    const double slowDesigned = slow.designedMagnitudeAt (4000.0);

    CHECK (std::abs (20.0 * std::log10 (fastDesigned / slowDesigned)) < 0.5);
}

// ---------------------------------------------------------------------------
// Minimum phase, demonstrated rather than declared
// ---------------------------------------------------------------------------

TEZLA_TEST (the_impulse_is_front_loaded_not_symmetric)
{
    // A minimum-phase impulse concentrates its energy at the start; the
    // reference's linear-phase construction centres it at tap N/2, and an
    // unfolded (zero-phase) cepstrum splits it half-and-half between the
    // buffer's two ends. Measured here: 91.5% of the energy sits in the
    // first eight of 128 taps; the zero-phase break-check lands near 52%.
    auto loss = makeLoss (48000.0, 15.0);

    CHECK (loss.frontLoadedEnergy (8) > 0.85);
    CHECK (loss.latencySamples() == 0);
}

// ---------------------------------------------------------------------------
// The head bump
// ---------------------------------------------------------------------------

TEZLA_TEST (the_head_bump_sits_at_the_contact_wavelength_and_scales)
{
    // f = v / 9 mm: the classic per-speed figures fall straight out.
    CHECK (std::abs (TapeLoss::bumpFrequencyFor (15.0) - 42.3) < 0.5);
    CHECK (std::abs (TapeLoss::bumpFrequencyFor (30.0) - 84.7) < 1.0);
    CHECK (std::abs (TapeLoss::bumpFrequencyFor (7.5) - 21.2) < 0.5);

    // Behaviour: with the bump at amount 1, the rendered gain at the bump
    // frequency stands ~2.5 dB above the same filter at amount 0.
    auto with = makeLoss (96000.0, 15.0);

    auto without = makeLoss (96000.0, 15.0);
    without.setBumpAmount (0.0);
    for (int i = 0; i < 4096; ++i)
        (void) without.process (0.0);

    const double bumpHz = TapeLoss::bumpFrequencyFor (15.0);
    const double lift = 20.0 * std::log10 (renderedGainAt (with, bumpHz, 96000.0)
                                             / renderedGainAt (without, bumpHz, 96000.0));

    CHECK (std::abs (lift - 2.5) < 0.4);
}

// ---------------------------------------------------------------------------
// Switching
// ---------------------------------------------------------------------------

TEZLA_TEST (a_speed_switch_is_click_free_and_actually_switches)
{
    // A steady tone through a mid-stream speed change: the largest
    // sample-to-sample step during the switch must stay in family with the
    // steady signal's own steps (a hard swap of 64 taps of state would
    // spike), and afterwards the response must genuinely be the new
    // speed's.
    constexpr double rate = 48000.0;
    constexpr double hz = 180.0;

    auto loss = makeLoss (rate, 15.0);

    double steadyStep = 0.0, switchStep = 0.0;
    double previous = 0.0;

    for (int i = 0; i < 4096; ++i)
    {
        const double out = loss.process (
            0.5 * std::sin (2.0 * 3.141592653589793 * hz * i / rate));

        if (i > 0)
            steadyStep = std::max (steadyStep, std::abs (out - previous));

        previous = out;
    }

    loss.setSpeedIps (7.5);

    for (int i = 4096; i < 12288; ++i)
    {
        const double out = loss.process (
            0.5 * std::sin (2.0 * 3.141592653589793 * hz * i / rate));

        switchStep = std::max (switchStep, std::abs (out - previous));
        previous = out;
    }

    CHECK (switchStep < 1.5 * steadyStep);

    // And the filter now really is the 7.5 ips design.
    auto fresh = makeLoss (rate, 7.5);
    const double switched = loss.designedMagnitudeAt (8000.0);
    const double direct = fresh.designedMagnitudeAt (8000.0);

    CHECK (std::abs (20.0 * std::log10 (switched / direct)) < 1.0e-9);
}

TEZLA_TEST (mid_fade_retargets_queue_and_the_last_one_wins)
{
    // Hammer the expert geometry every 256 samples while audio runs: no
    // click ever, and once the dust settles the filter is the design the
    // LAST call asked for -- a queued retarget that dropped the newest
    // values would leave a stale response.
    constexpr double rate = 48000.0;

    auto loss = makeLoss (rate, 15.0);

    double steadyStep = 0.0, hammerStep = 0.0;
    double previous = 0.0;

    for (int i = 0; i < 2048; ++i)
    {
        const double out = loss.process (
            0.5 * std::sin (2.0 * 3.141592653589793 * 180.0 * i / rate));

        if (i > 0)
            steadyStep = std::max (steadyStep, std::abs (out - previous));

        previous = out;
    }

    for (int i = 0; i < 24000; ++i)
    {
        if (i % 256 == 0)
            loss.setGeometry (5.0 + 0.001 * i, 35.0, 2.5);

        const double out = loss.process (
            0.5 * std::sin (2.0 * 3.141592653589793 * 180.0 * (2048 + i) / rate));

        hammerStep = std::max (hammerStep, std::abs (out - previous));
        previous = out;
    }

    const double finalSpacing = 5.0 + 0.001 * 23808;   // the last value set

    // Let every queued fade drain.
    for (int i = 0; i < 4 * 2048 + 64; ++i)
        (void) loss.process (0.0);

    CHECK (hammerStep < 1.5 * steadyStep);

    TapeLoss fresh;
    fresh.prepare (rate);
    fresh.setSpeedIps (15.0);
    fresh.setGeometry (finalSpacing, 35.0, 2.5);
    for (int i = 0; i < 8192; ++i)
        (void) fresh.process (0.0);

    const double settled = loss.designedMagnitudeAt (12000.0);
    const double direct = fresh.designedMagnitudeAt (12000.0);

    CHECK (std::abs (20.0 * std::log10 (settled / direct)) < 1.0e-9);
}

// ---------------------------------------------------------------------------
// House basics
// ---------------------------------------------------------------------------

TEZLA_TEST (loss_silence_in_is_exact_silence_out)
{
    auto loss = makeLoss (48000.0, 15.0);

    bool allZero = true;

    for (int i = 0; i < 4096; ++i)
        allZero = allZero && loss.process (0.0) == 0.0;

    CHECK (allZero);
}

TEZLA_TEST (loss_no_op_setters_do_not_disturb_the_stream)
{
    auto quiet = makeLoss (48000.0, 15.0);
    auto pushed = makeLoss (48000.0, 15.0);

    double worst = 0.0;

    for (int i = 0; i < 8192; ++i)
    {
        const double input = 0.7 * std::sin (2.0 * 3.141592653589793 * 300.0 * i / 48000.0);

        const double a = quiet.process (input);
        pushed.setSpeedIps (15.0);
        pushed.setGeometry (5.0, 35.0, 2.5);
        pushed.setBumpAmount (1.0);
        const double b = pushed.process (input);

        worst = std::max (worst, std::abs (a - b));
    }

    CHECK (worst == 0.0);
}
