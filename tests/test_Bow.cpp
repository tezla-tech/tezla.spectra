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

#include <tezla/dsp/ModalResonator.hpp>

#include "Bow.hpp"

using tezla::dsp::ModalResonator;
using tezla::malleus::Bow;

namespace
{
/// A bowable object: harmonic modes on f0, struck-position comb weights,
/// equal gains summing to one.
struct BowRig
{
    ModalResonator bank;
    Bow bow;

    void prepare (double fs, int modes, double f0, double t60, double position)
    {
        bank.prepare (fs);
        bank.setModeCount (modes);

        for (int i = 0; i < modes; ++i)
        {
            bank.setMode (i, f0 * (i + 1), t60, 1.0 / modes);
            bank.setInputWeight (i,
                std::sin ((i + 1) * std::numbers::pi * position));
        }

        bow.prepare (fs);
    }

    [[nodiscard]] double step()
    {
        return bank.process (bow.force (bank.contactVelocity()));
    }
};

struct BowRun
{
    double rms;        // mean-centred, over the tail
    double mean;       // the static deflection
    double peak;
    double excess;
    bool finite;
    std::vector<double> tail;
};

/// Runs a rig from exact rest and measures the sustained tail. The RMS is
/// taken about the tail's mean because the kinetic force statically
/// deflects the object (as a real bow bends a real string): the standing
/// offset is not oscillation and must not count as it.
[[nodiscard]] BowRun runBow (double fs, int modes, double f0, double t60,
                             double position, double pressure, double speed,
                             double seconds, bool keepTail = false)
{
    BowRig rig;
    rig.prepare (fs, modes, f0, t60, position);
    rig.bow.setPressure (pressure);
    rig.bow.setSpeed (speed);

    const int total = static_cast<int> (seconds * fs);
    const int tailStart = total - static_cast<int> (0.3 * fs);

    BowRun r {};
    r.finite = true;

    std::vector<double> tail;

    for (int n = 0; n < total; ++n)
    {
        const double y = rig.step();

        if (! std::isfinite (y))
        {
            r.finite = false;
            break;
        }

        if (std::abs (y) > r.peak)
            r.peak = std::abs (y);

        if (n >= tailStart)
            tail.push_back (y);
    }

    double mean = 0.0;

    for (const double v : tail)
        mean += v;

    mean /= tail.empty() ? 1.0 : static_cast<double> (tail.size());

    double sumSq = 0.0;

    for (const double v : tail)
        sumSq += (v - mean) * (v - mean);

    r.mean = mean;
    r.rms = tail.empty() ? 0.0
                         : std::sqrt (sumSq / static_cast<double> (tail.size()));
    r.excess = rig.bow.getClampExcess();

    if (keepTail)
        r.tail = std::move (tail);

    return r;
}

/// Goertzel-style projection: signal power at one frequency.
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
} // namespace

TEZLA_TEST (the_friction_curve_is_the_rosin_shape)
{
    // mu(0) is exactly zero (tanh(0) = 0): a bow moving WITH the object
    // transmits nothing, which is also the zero-speed silence guarantee.
    CHECK (Bow::friction (0.0) == 0.0);

    // Odd, as friction must be.
    CHECK_NEAR (Bow::friction (-0.3), -Bow::friction (0.3), 1.0e-15);

    // The static peak sits in the stick-slip corner and reads 0.670 --
    // measured from the curve itself and pinned. Everything beyond falls
    // away toward the dynamic coefficient: the negative-resistance region
    // that pumps the modes.
    double best = 0.0;
    double bestAt = 0.0;

    for (double dv = 0.0; dv <= 20.0; dv += 1.0e-3)
    {
        const double mu = Bow::friction (dv);

        CHECK (std::abs (mu) <= Bow::kMuStatic);

        if (mu > best)
        {
            best = mu;
            bestAt = dv;
        }
    }

    std::printf ("        [bow curve] peak mu %.4f at dv %.3f\n", best, bestAt);

    CHECK_NEAR (best, 0.670, 0.001);
    CHECK (bestAt > 0.03 && bestAt < 0.06);

    // Falling flank -- the region that makes it an oscillator.
    CHECK (Bow::friction (0.1) > Bow::friction (0.5));
    CHECK (Bow::friction (0.5) > Bow::friction (5.0));

    // The tail forgets the static coefficient and approaches the dynamic
    // one: mu(50) within 0.002 of muD.
    CHECK_NEAR (Bow::friction (50.0), Bow::kMuDynamic, 0.002);
}

TEZLA_TEST (the_clamp_is_identity_below_the_knee_and_bounded_above)
{
    // Below the knee the backstop is bit-exact identity -- the healthy bow
    // never feels it (the curve's own bound tops out at half the knee).
    for (double f = -Bow::kClampKnee; f <= Bow::kClampKnee; f += 0.01)
        CHECK (Bow::clampForce (f) == f);

    // Above it, monotone, C1 at the knee, and bounded by the limit however
    // hard a hypothetical broken curve pushes.
    const double slope = (Bow::clampForce (Bow::kClampKnee + 1.0e-6)
                          - Bow::clampForce (Bow::kClampKnee)) / 1.0e-6;

    CHECK_NEAR (slope, 1.0, 0.01);

    double previous = Bow::clampForce (Bow::kClampKnee);

    for (double f = Bow::kClampKnee; f <= 10.0; f += 0.01)
    {
        const double clamped = Bow::clampForce (f);

        CHECK (clamped >= previous);
        CHECK (clamped <= Bow::kClampLimit);
        previous = clamped;
    }

    CHECK (Bow::clampForce (1.0e6) <= Bow::kClampLimit);
    CHECK (Bow::clampForce (1.0e6) > 0.9 * Bow::kClampLimit);
    CHECK_NEAR (Bow::clampForce (-1.0e6), -Bow::clampForce (1.0e6), 1.0e-12);
}

TEZLA_TEST (the_bow_self_oscillates_above_a_pressure_onset)
{
    // The onset map, measured on 8 string modes at 110 Hz (T60 2 s, struck
    // comb at 0.29, speed 0.5, 48 kHz; tail RMS about its mean):
    //
    //   P 0.01  rms 0.00025   -- below onset: effectively silent
    //   P 0.05  rms 0.13      -- singing
    //   P 0.20  rms 0.60      -- full voice
    //
    // pinned as inequalities with a 2400x span, so the onset can neither
    // vanish (oscillating at any touch) nor run away (needing full
    // pressure to speak). The clamp stays exactly dormant throughout.
    const BowRun quiet = runBow (48000.0, 8, 110.0, 2.0, 0.29, 0.01, 0.5, 1.5);
    const BowRun singing = runBow (48000.0, 8, 110.0, 2.0, 0.29, 0.05, 0.5, 1.5);
    const BowRun full = runBow (48000.0, 8, 110.0, 2.0, 0.29, 0.2, 0.5, 1.5);

    std::printf ("        [bow onset] rms P.01 %.4g, P.05 %.4g, P.20 %.4g (mean %.3g)\n",
                 quiet.rms, singing.rms, full.rms, full.mean);

    CHECK (quiet.finite && singing.finite && full.finite);
    CHECK (quiet.rms < 1.0e-3);
    CHECK (singing.rms > 0.08);
    CHECK (full.rms > 0.4);
    CHECK (full.rms > 1000.0 * quiet.rms);

    CHECK (quiet.excess == 0.0);
    CHECK (singing.excess == 0.0);
    CHECK (full.excess == 0.0);
}

TEZLA_TEST (the_bow_onset_also_gates_on_speed)
{
    // Same rig at pressure 0.6: a crawling bow (speed 0.1) cannot sustain
    // (rms 6e-4), a moving one (0.4) sings at rms 0.8 -- the second axis
    // of the onset map the plan asks for.
    const BowRun slow = runBow (48000.0, 8, 110.0, 2.0, 0.29, 0.6, 0.1, 1.5);
    const BowRun moving = runBow (48000.0, 8, 110.0, 2.0, 0.29, 0.6, 0.4, 1.5);

    std::printf ("        [bow speed] rms S.1 %.4g, S.4 %.4g\n",
                 slow.rms, moving.rms);

    CHECK (slow.finite && moving.finite);
    CHECK (slow.rms < 5.0e-3);
    CHECK (moving.rms > 0.4);
    CHECK (moving.rms > 50.0 * slow.rms);
}

TEZLA_TEST (the_bow_locks_to_the_objects_modes)
{
    // What self-oscillation must mean on a modal object: the sustained
    // spectrum concentrates ON the modes, not at some relaxation frequency
    // of the loop's own. Projected onto each mode frequency and onto the
    // midpoints between them, the strongest mode line dominates the
    // strongest between-modes probe -- measured at x2715, pinned at x100.
    const BowRun r = runBow (48000.0, 8, 110.0, 2.0, 0.29, 0.05, 0.5, 1.5, true);

    CHECK (r.finite);
    CHECK (r.tail.size() > 10000);

    double onMode = 0.0;
    double offMode = 0.0;

    for (int k = 1; k <= 8; ++k)
    {
        onMode = std::max (onMode, powerAt (r.tail, 110.0 * k, 48000.0));
        offMode = std::max (offMode, powerAt (r.tail, 110.0 * (k + 0.5), 48000.0));
    }

    std::printf ("        [bow lock] on-mode %.4g, between-modes %.4g (x%.1f)\n",
                 onMode, offMode, onMode / offMode);

    CHECK (onMode > 100.0 * offMode);
}

TEZLA_TEST (the_bow_speaks_the_same_at_every_rate)
{
    // Section 6 for a self-oscillator: the limit cycle's strength must not
    // follow the host rate. Measured sustained RMS at 48/96/192 kHz sits
    // within a few percent (the injection is momentum per second and the
    // hair corners are in Hz); pinned at 10%.
    const BowRun a = runBow (48000.0, 8, 110.0, 2.0, 0.29, 0.6, 0.5, 1.5);
    const BowRun b = runBow (96000.0, 8, 110.0, 2.0, 0.29, 0.6, 0.5, 1.5);
    const BowRun c = runBow (192000.0, 8, 110.0, 2.0, 0.29, 0.6, 0.5, 1.5);

    std::printf ("        [bow rates] rms 48k %.4g, 96k %.4g, 192k %.4g\n",
                 a.rms, b.rms, c.rms);

    CHECK (a.finite && b.finite && c.finite);
    CHECK (a.rms > 0.1);
    CHECK_NEAR (b.rms / a.rms, 1.0, 0.1);
    CHECK_NEAR (c.rms / a.rms, 1.0, 0.1);
}

TEZLA_TEST (the_whole_bow_parameter_plane_stays_bounded)
{
    // The section 7 sweep: the full pressure x speed plane, not samples of
    // it -- on the musical rig AND on a deliberately absurd worst case
    // (64 modes, every weight and gain at 1, T60 10 s). Everything finite,
    // every peak under the pinned ceiling, and the clamp exactly dormant
    // everywhere: the curve's own bound does the limiting, the backstop
    // never has to.
    double worstPeak = 0.0;

    for (int pi = 0; pi <= 10; ++pi)
        for (int si = 0; si <= 10; ++si)
        {
            const BowRun r = runBow (48000.0, 8, 110.0, 2.0, 0.29,
                                     pi * 0.1, si * 0.1, 0.35);

            CHECK (r.finite);
            CHECK (r.peak < 20.0);
            CHECK (r.excess == 0.0);
            worstPeak = std::max (worstPeak, r.peak);
        }

    std::printf ("        [bow sweep] musical-rig worst peak %.3g\n", worstPeak);

    double absurdPeak = 0.0;

    for (int pi = 0; pi <= 5; ++pi)
        for (int si = 0; si <= 5; ++si)
        {
            BowRig rig;
            rig.prepare (48000.0, 64, 55.0, 10.0, 0.29);

            for (int i = 0; i < 64; ++i)
            {
                rig.bank.setMode (i, 55.0 * (i + 1), 10.0, 1.0);
                rig.bank.setInputWeight (i, 1.0);
            }

            rig.bow.setPressure (pi * 0.2);
            rig.bow.setSpeed (si * 0.2);

            for (int n = 0; n < 24000; ++n)
            {
                const double y = rig.step();

                CHECK (std::isfinite (y));
                absurdPeak = std::max (absurdPeak, std::abs (y));
            }

            CHECK (rig.bow.getClampExcess() == 0.0);
        }

    std::printf ("        [bow sweep] absurd-rig worst peak %.4g\n", absurdPeak);

    CHECK (absurdPeak < 500.0);
}

TEZLA_TEST (a_bow_at_zero_pressure_or_zero_speed_is_bit_exact_silence)
{
    // From exact rest, zero pressure OR zero speed keeps every term of the
    // loop exactly zero: tanh(0) = 0 for the still bow, pressure * x = 0
    // for the lifted one. Not small -- zero.
    for (const bool still : { true, false })
    {
        BowRig rig;
        rig.prepare (48000.0, 8, 110.0, 2.0, 0.29);
        rig.bow.setPressure (still ? 0.7 : 0.0);
        rig.bow.setSpeed (still ? 0.0 : 0.7);

        for (int n = 0; n < 5000; ++n)
            CHECK (rig.step() == 0.0);

        CHECK (rig.bank.energy() == 0.0);
    }
}
