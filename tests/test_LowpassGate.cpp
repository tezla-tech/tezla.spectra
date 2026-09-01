// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tezla/dsp/LowpassGate.hpp>

using tezla::dsp::LowpassGate;

namespace
{
/// Runs a pinged, input-less gate and returns the conductance trajectory.
[[nodiscard]] std::vector<double> pingTrajectory (double fs, double seconds)
{
    LowpassGate gate;
    gate.prepare (fs);
    gate.ping (1.0);

    std::vector<double> g;
    const int total = static_cast<int> (seconds * fs);

    for (int n = 0; n < total; ++n)
    {
        (void) gate.process (0.0);
        g.push_back (gate.conductance());
    }

    return g;
}

/// First sample index where the trajectory falls to `level`, or -1.
[[nodiscard]] int crossingIndex (const std::vector<double>& g, double level)
{
    for (std::size_t n = 1; n < g.size(); ++n)
        if (g[n - 1] >= level && g[n] < level)
            return static_cast<int> (n);

    return -1;
}
} // namespace

TEZLA_TEST (the_ping_decay_is_measurably_not_an_exponential)
{
    // The vactrol signature: a bright cell darkens fast, a dark cell drags.
    // A pure exponential has ONE half-life; this cell's lengthens as it
    // fades. Measured on the conductance itself at 48 kHz:
    //
    //   0.5  -> 0.25  in ~28 ms   (bright: the quadratic term dominates)
    //   0.05 -> 0.025 in ~86 ms   (dark: the linear tail)
    //
    // -- the late half-life is ~3.1x the early one, pinned. Break the
    // quadratic term and both read the same, which is the red state.
    const auto g = pingTrajectory (48000.0, 2.5);

    const int at50 = crossingIndex (g, 0.5);
    const int at25 = crossingIndex (g, 0.25);
    const int at5 = crossingIndex (g, 0.05);
    const int at2p5 = crossingIndex (g, 0.025);

    CHECK (at50 > 0 && at25 > at50 && at5 > at25 && at2p5 > at5);

    const double earlyHalfLife = (at25 - at50) / 48000.0;
    const double lateHalfLife = (at2p5 - at5) / 48000.0;

    std::printf ("        [lpg decay] half-life 0.5->0.25 %.1f ms, 0.05->0.025 %.1f ms (x%.2f)\n",
                 1000.0 * earlyHalfLife, 1000.0 * lateHalfLife,
                 lateHalfLife / earlyHalfLife);

    CHECK (lateHalfLife > 2.0 * earlyHalfLife);
    CHECK_NEAR (1000.0 * earlyHalfLife, 28.0, 3.0);
    CHECK_NEAR (1000.0 * lateHalfLife, 86.0, 6.0);

    // And from its peak on, the dark decay is monotone -- no bumps, no
    // ringing. The peak itself reads ~0.85, not 1.0: the flash is already
    // fading while the cell chases it, which is the attack transient a
    // real vactrol ping has.
    std::size_t peakIndex = 0;

    for (std::size_t n = 1; n < g.size(); ++n)
        if (g[n] > g[peakIndex])
            peakIndex = n;

    CHECK (g[peakIndex] > 0.7);
    CHECK (g[peakIndex] < 0.999);

    bool monotone = true;

    for (std::size_t n = peakIndex + 1; n < g.size(); ++n)
        monotone = monotone && g[n] <= g[n - 1];

    CHECK (monotone);
}

TEZLA_TEST (a_closed_gate_is_exact_silence_not_quiet)
{
    // Once the cell darkens below the snap floor the conductance is
    // EXACTLY zero, and so is the output, whatever keeps playing into it.
    LowpassGate gate;
    gate.prepare (48000.0);
    gate.ping (1.0);

    int closedAt = -1;

    for (int n = 0; n < 48000 * 3; ++n)
    {
        const double input = std::sin (2.0 * std::numbers::pi * 220.0 * n / 48000.0);
        (void) gate.process (input);

        if (gate.conductance() == 0.0)
        {
            closedAt = n;
            break;
        }
    }

    std::printf ("        [lpg close] fully dark %.2f s after the strike\n",
                 closedAt / 48000.0);

    CHECK (closedAt > 0);

    // From here on: bit-exact zeros against a live input, forever.
    for (int n = 0; n < 4000; ++n)
    {
        const double input = std::sin (2.0 * std::numbers::pi * 220.0 * n / 48000.0);
        CHECK (gate.process (input) == 0.0);
    }

    // And a gate never pinged is a gate never open.
    LowpassGate untouched;
    untouched.prepare (48000.0);

    for (int n = 0; n < 2000; ++n)
        CHECK (untouched.process (1.0) == 0.0);

    // Silence in, silence out -- a pinged gate with nothing to pass makes
    // nothing, exactly.
    LowpassGate quiet;
    quiet.prepare (48000.0);
    quiet.ping (1.0);

    for (int n = 0; n < 2000; ++n)
        CHECK (quiet.process (0.0) == 0.0);
}

TEZLA_TEST (retriggering_mid_decay_is_click_free)
{
    // Re-striking a half-closed gate must not step the output: the cell's
    // attack pole ramps the gain over ~a millisecond. Yardstick: the
    // steady open gate's own largest sample-to-sample step on the same
    // 1 kHz sine. The retrigger's worst step measures BELOW it (x0.71 --
    // the half-closed gate is quieter than the open one throughout the
    // ramp), pinned under x1.5; make the attack instant and it spikes
    // well past that, which is the red state.
    LowpassGate gate;
    gate.prepare (48000.0);

    const auto sine = [] (int n)
    {
        return std::sin (2.0 * std::numbers::pi * 1000.0 * n / 48000.0);
    };

    // Steady reference: held fully open, past its attack.
    gate.setHold (1.0);

    double steadyStep = 0.0;
    double previous = 0.0;

    for (int n = 0; n < 9600; ++n)
    {
        const double y = gate.process (sine (n));

        if (n > 4800)
            steadyStep = std::max (steadyStep, std::abs (y - previous));

        previous = y;
    }

    // Now the scenario: strike, decay 150 ms, strike again -- 7211 samples
    // in, NOT 7200: that would be exactly 150 cycles of the 1 kHz probe,
    // putting the re-strike on a zero crossing where any step hides (the
    // same slip the resonator retune test made and documented first).
    LowpassGate struck;
    struck.prepare (48000.0);
    struck.ping (1.0);

    double worstStep = 0.0;
    double before = 0.0;

    for (int n = 0; n < 14400; ++n)
    {
        if (n == 7211)
        {
            CHECK (struck.conductance() < 0.4);   // genuinely mid-decay
            struck.ping (1.0);
        }

        const double y = struck.process (sine (n));

        if (n >= 7211 && n < 7211 + 480)
            worstStep = std::max (worstStep, std::abs (y - before));

        before = y;
    }

    std::printf ("        [lpg retrigger] worst step %.4f vs steady %.4f (x%.2f)\n",
                 worstStep, steadyStep, worstStep / steadyStep);

    CHECK (worstStep < 1.5 * steadyStep);
}

TEZLA_TEST (the_gate_darkens_as_it_quietens)
{
    // The vactrol couples cutoff to gain: the same conductance that turns
    // the level down pulls the corner down. At hold 0.35 a 6 kHz tone
    // must come through far quieter than the conductance alone explains
    // -- a pure VCA would pass 0.35 of it; the coupled filter passes
    // ~0.05 (measured, pinned): the closing gate darkens.
    const auto levelAt = [] (double hold)
    {
        LowpassGate gate;
        gate.prepare (48000.0);
        gate.setHold (hold);

        // Let the cell settle onto the held light first.
        for (int n = 0; n < 24000; ++n)
            (void) gate.process (0.0);

        double sumSq = 0.0;

        for (int n = 0; n < 24000; ++n)
        {
            const double y = gate.process (
                std::sin (2.0 * std::numbers::pi * 6000.0 * n / 48000.0));

            if (n >= 4800)
                sumSq += y * y;
        }

        return std::sqrt (sumSq / (24000 - 4800));
    };

    const double open = levelAt (1.0);
    const double half = levelAt (0.35);

    std::printf ("        [lpg couple] 6 kHz through hold 1.0 %.4f, hold 0.35 %.4f (ratio %.3f)\n",
                 open, half, half / open);

    CHECK (open > 0.5);                  // open gate passes the top
    CHECK (half / open < 0.15);          // far below the 0.35 a VCA would pass
    CHECK (half / open > 0.01);          // but audibly there, not gone
}

TEZLA_TEST (the_vactrol_trajectory_is_the_same_at_every_rate)
{
    // All time constants are per-second and the decay integrates per
    // sample, so the conductance at a given TIME must match across host
    // rates (CLAUDE.md section 6). Compared at 50/100/200/400 ms between
    // 48 and 192 kHz.
    const auto g48 = pingTrajectory (48000.0, 0.5);
    const auto g192 = pingTrajectory (192000.0, 0.5);

    for (const double t : { 0.05, 0.1, 0.2, 0.4 })
    {
        const double a = g48[static_cast<std::size_t> (t * 48000.0)];
        const double b = g192[static_cast<std::size_t> (t * 192000.0)];

        CHECK_NEAR (b / a, 1.0, 0.01);
    }
}

TEZLA_TEST (the_gate_costs_what_a_voice_can_afford)
{
    // One exp and one pow per sample was called harmless in the header;
    // measured here so the claim stays true.
    LowpassGate gate;
    gate.prepare (48000.0);
    gate.setHold (0.6);

    double sink = 0.0;
    const auto start = std::chrono::steady_clock::now();

    for (int n = 0; n < 48000; ++n)
        sink += gate.process (std::sin (0.13 * n));

    const double seconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    std::printf ("        [lpg cpu] %.2f%% of one core (sink %g)\n",
                 100.0 * seconds, sink);

    CHECK_CPU_BUDGET (seconds, 0.02, "lowpass gate");   // 2% of a core
}
