// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <cstdio>
#include <type_traits>
#include <vector>

#include <tezla/dsp/ModalResonator.hpp>
#include <tezla/dsp/TensionDrop.hpp>

#include "TensionDrop.hpp"   // Malleus's forwarding header

using tezla::dsp::ModalResonator;
using tezla::dsp::TensionDrop;

// The promotion to shared/tezla-dsp left Malleus a name, not a copy: one
// class, two namespaces. A second implementation would drift.
static_assert (std::is_same_v<tezla::malleus::TensionDrop, tezla::dsp::TensionDrop>);

TEZLA_TEST (the_drop_starts_at_depth_and_lands_at_the_stated_time)
{
    // A 12-semitone drop over 150 ms: the multiplier opens at exactly 2.0
    // (1200 cents is exp2(1.0), which is exact), carries half the depth at
    // 0.1386 of the stated time (e^-5t/T = 1/2), has 8.08 cents left AT the
    // stated time (e^-5 of 1200), and shortly after snaps to exactly 1.0 so
    // the resonator's no-op retune guard takes over.
    TensionDrop drop;
    drop.prepare (48000.0);
    drop.trigger (12.0, 0.15);

    CHECK (drop.multiplier() == 2.0);
    CHECK (drop.isActive());

    const int half = static_cast<int> (0.15 * 48000.0 * std::log (2.0) / 5.0);
    drop.advance (half);

    CHECK_NEAR (drop.remainingCents(), 600.0, 2.0);

    drop.advance (static_cast<int> (0.15 * 48000.0) - half);

    CHECK_NEAR (drop.remainingCents(), 1200.0 * std::exp (-5.0), 0.1);

    drop.advance (static_cast<int> (5.0 * 0.15 * 48000.0));

    CHECK (drop.multiplier() == 1.0);
    CHECK (! drop.isActive());

    // Zero depth is bit-exactly nothing from the first sample.
    TensionDrop still;
    still.prepare (48000.0);
    still.trigger (0.0, 0.1);

    CHECK (still.multiplier() == 1.0);
    CHECK (! still.isActive());
}

TEZLA_TEST (the_drop_ignores_block_size_and_host_rate)
{
    // Multiplicative advance: 7200 samples in one call, in 150 blocks of
    // 48, or in ragged pieces must land within rounding of the same cents
    // -- the host's buffer size cannot bend the glide.
    TensionDrop one;
    TensionDrop many;
    one.prepare (48000.0);
    many.prepare (48000.0);
    one.trigger (7.0, 0.2);
    many.trigger (7.0, 0.2);

    one.advance (7200);

    for (int block = 0; block < 150; ++block)
        many.advance (48);

    CHECK_NEAR (many.remainingCents() / one.remainingCents(), 1.0, 1.0e-9);

    // And the same SECONDS at 192 kHz reads the same cents.
    TensionDrop fast;
    fast.prepare (192000.0);
    fast.trigger (7.0, 0.2);
    fast.advance (7200 * 4);

    CHECK_NEAR (fast.remainingCents() / one.remainingCents(), 1.0, 1.0e-6);
}

TEZLA_TEST (the_drop_glides_a_ringing_bank_onto_its_target)
{
    // The integration the feature exists for: strike a mode at twice its
    // frequency, glide down over 80 ms through the state-preserving
    // retune, and measure where the ring actually lands -- 220.0 Hz within
    // half a Hz, from a 440 Hz strike. Then the reverse-drop: strike at
    // half and rise onto 220 the same way.
    for (const double depth : { 12.0, -12.0 })
    {
        ModalResonator bank;
        bank.prepare (48000.0);
        bank.setModeCount (1);

        TensionDrop drop;
        drop.prepare (48000.0);
        drop.trigger (depth, 0.08);

        const double target = 220.0;

        bank.setMode (0, target * drop.multiplier(), 2.0, 1.0);
        bank.excite (0, 1.0);

        std::vector<double> tail;

        for (int n = 0; n < 48000; ++n)
        {
            if (n % 48 == 0)
            {
                drop.advance (48);
                bank.setMode (0, target * drop.multiplier(), 2.0, 1.0);
            }

            const double y = bank.process();

            if (n >= 24000)
                tail.push_back (y);
        }

        // Zero-crossing frequency over the settled half second.
        int crossings = 0;
        int first = -1;
        int last = -1;

        for (std::size_t n = 1; n < tail.size(); ++n)
            if (tail[n - 1] <= 0.0 && tail[n] > 0.0)
            {
                if (first < 0)
                    first = static_cast<int> (n);

                last = static_cast<int> (n);
                ++crossings;
            }

        const double hz = (crossings - 1) * 48000.0 / (last - first);

        std::printf ("        [drop] depth %+.0f st lands at %.2f Hz\n", depth, hz);

        CHECK_NEAR (hz, target, 0.5);
    }
}

TEZLA_TEST (the_curve_bends_the_landing_and_is_the_exponential_at_zero)
{
    // Curve 0 is the original glide by branch: the same sequence of
    // remaining cents as a drop triggered without a curve at all.
    TensionDrop plain, zero;
    plain.prepare (96000.0);
    zero.prepare (96000.0);
    plain.trigger (24.0, 0.1);
    zero.trigger (24.0, 0.1, 0.0);

    for (int block = 0; block < 400; ++block)
    {
        plain.advance (32);
        zero.advance (32);
        CHECK (plain.remainingCents() == zero.remainingCents());
    }

    // Curve -1 is a straight line in cents that lands at exactly the stated
    // time: half the depth half way, nothing at all at the end.
    TensionDrop line;
    line.prepare (96000.0);
    line.trigger (24.0, 0.1, -1.0);
    line.advance (4800);
    CHECK_NEAR (line.remainingCents(), 1200.0, 1.0e-9);
    line.advance (4800);
    CHECK (line.multiplier() == 1.0);
    CHECK (! line.isActive());

    // Curve +1 holds near the start and then falls: at half time it still
    // carries e^(-5 * 0.5^4) = 73 % of the depth, where the exponential
    // carries 8 %; and it lands.
    TensionDrop snap;
    snap.prepare (96000.0);
    snap.trigger (24.0, 0.1, 1.0);
    snap.advance (4800);
    CHECK_NEAR (snap.remainingCents(), 2400.0 * std::exp (-5.0 * std::pow (0.5, 4.0)), 1.0e-9);

    TensionDrop exponential;
    exponential.prepare (96000.0);
    exponential.trigger (24.0, 0.1);
    exponential.advance (4800);
    CHECK_NEAR (exponential.remainingCents(), 2400.0 * std::exp (-2.5), 0.01);

    snap.advance (96000);
    CHECK (! snap.isActive());

    std::printf ("        [curve] half way through a 24 st drop: line %.0f cents, exponential %.0f, snap %.0f\n",
                 1200.0, 2400.0 * std::exp (-2.5), 2400.0 * std::exp (-5.0 * std::pow (0.5, 4.0)));

    // Every curve is 1.0 of the depth at the start, for a hit that reads the
    // multiplier before its first control tick.
    for (const double curve : { -1.0, -0.5, 0.5, 1.0 })
    {
        TensionDrop d;
        d.prepare (48000.0);
        d.trigger (12.0, 0.05, curve);
        CHECK (d.multiplier() == 2.0);
    }
}

TEZLA_TEST (a_curved_drop_ignores_block_size_too)
{
    // The curved landings count elapsed samples -- an exact integer sum --
    // so 32-sample blocks and one 4096-sample block land on the same bits.
    for (const double curve : { -1.0, -0.3, 0.6, 1.0 })
    {
        TensionDrop small, large;
        small.prepare (192000.0);
        large.prepare (192000.0);
        small.trigger (-7.0, 0.03, curve);
        large.trigger (-7.0, 0.03, curve);

        for (int block = 0; block < 128; ++block)
            small.advance (32);

        large.advance (4096);

        CHECK (small.remainingCents() == large.remainingCents());
        CHECK (small.multiplier() == large.multiplier());
    }
}
