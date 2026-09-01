// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <tezla/dsp/Exact.hpp>

#include <MembranaEngine.hpp>

using namespace tezla;
using namespace tezla::membrana;

// The stages are tested individually elsewhere. What only this file can ask
// is whether the CHAIN holds: that four neutral stages in a row are the
// identity to the bit, that the dynamics decisions are genuinely linked,
// that autoLevel keeps 1 kHz still while the distance moves, that the gain
// riding leaves no measurable sidebands, and that none of it depends on how
// the host cuts its buffers.

namespace
{
    constexpr double kRate = 48000.0;
}

TEZLA_TEST (membrana_all_neutral_is_bit_exact_identity)
{
    // The defaults are the neutral setting: 1 m on axis (character 35% is
    // NOT zero -- the reference position is what nulls the sphere), grille
    // 0, presence 0, detail 0, output 0 dB. 40001 values through both
    // channels, including +/-0.0 and the denormal-hostile region around
    // zero, back bit for bit.
    MembranaEngine engine;
    engine.prepare (kRate);
    engine.setSettings (MembranaEngine::Settings {});

    CHECK (engine.isIdentity());
    CHECK (MembranaEngine::latencySamples() == 0);

    for (int i = -20000; i <= 20000; ++i)
    {
        double left = static_cast<double> (i) / 19997.0;   // not a round grid
        double right = -left * 0.37;

        const double originalLeft = left;
        const double originalRight = right;

        engine.process (&left, &right, 1);

        CHECK (dsp::isExactly (left, originalLeft));
        CHECK (dsp::isExactly (right, originalRight));
    }
}

TEZLA_TEST (membrana_stage_toggles_flip_identity)
{
    // Each stage leaves neutrality on its own; each returns with its
    // switch off.
    MembranaEngine::Settings s;
    CHECK (MembranaEngine::isIdentity (s));

    s.mic.distanceCm = 8.0;
    CHECK (! MembranaEngine::isIdentity (s));
    s.mic.on = false;
    CHECK (MembranaEngine::isIdentity (s));
    s = {};

    s.presence.amountDb = 3.0;
    CHECK (! MembranaEngine::isIdentity (s));
    s.presence.on = false;
    CHECK (MembranaEngine::isIdentity (s));
    s = {};

    s.detail.amountDb = 3.0;
    CHECK (! MembranaEngine::isIdentity (s));
    s.detail.on = false;
    CHECK (MembranaEngine::isIdentity (s));
    s = {};

    s.outputDb = 1.0;
    CHECK (! MembranaEngine::isIdentity (s));

    // The omni corner the shared predicate has to get right: an omni with
    // no character and no grille is neutral at ANY distance under
    // autoLevel -- there is no gradient to boost and no shadow to shape.
    s = {};
    s.mic.pattern01 = 0.0;
    s.mic.character01 = 0.0;
    s.mic.distanceCm = 5.0;
    CHECK (MembranaEngine::isIdentity (s));

    // But with autoLevel off, 5 cm means +24 dB of physical level: loud,
    // not neutral.
    s.mic.autoLevel = false;
    CHECK (! MembranaEngine::isIdentity (s));
}

TEZLA_TEST (membrana_silence_in_silence_out_fully_engaged)
{
    MembranaEngine engine;
    MembranaEngine::Settings s;
    s.mic.distanceCm = 8.0;
    s.mic.character01 = 1.0;
    s.mic.grille01 = 0.5;
    s.presence.amountDb = 9.0;
    s.presence.track01 = 1.0;
    s.detail.amountDb = 12.0;
    engine.prepare (kRate);
    engine.setSettings (s);

    std::vector<double> left (4800, 0.0), right (4800, 0.0);

    for (int block = 0; block < 20; ++block)
    {
        engine.process (left.data(), right.data(), 4800);

        for (double v : left)
            CHECK (v == 0.0);

        for (double v : right)
            CHECK (v == 0.0);
    }
}

TEZLA_TEST (membrana_autolevel_holds_1khz_across_the_distance_sweep)
{
    // The auto-trim rule (CLAUDE.md section 7): Distance must read as tone,
    // not loudness. Full character, each distance prepared and measured:
    // worst deviation measured 0.0004 dB; the plan's budget was +/-0.3.
    for (double cm : { 2.0, 5.0, 10.0, 20.0, 50.0, 80.0, 100.0 })
    {
        MembranaEngine engine;
        MembranaEngine::Settings s;
        s.mic.distanceCm = cm;
        s.mic.character01 = 1.0;
        engine.prepare (kRate);
        engine.setSettings (s);

        const double dPhase = 2.0 * std::numbers::pi * 1000.0 / kRate;
        double phase = 0.0, energyIn = 0.0, energyOut = 0.0;
        std::vector<double> left (512), right (512), input (512);
        int done = 0;
        const int settle = 24000, measure = 24000;

        while (done < settle + measure)
        {
            for (int i = 0; i < 512; ++i)
            {
                input[static_cast<std::size_t> (i)] = 0.1 * std::sin (phase);
                left[static_cast<std::size_t> (i)] =
                    right[static_cast<std::size_t> (i)] =
                        input[static_cast<std::size_t> (i)];
                phase += dPhase;
            }

            engine.process (left.data(), right.data(), 512);

            for (int i = 0; i < 512; ++i)
                if (done + i >= settle)
                {
                    energyIn += input[static_cast<std::size_t> (i)]
                                * input[static_cast<std::size_t> (i)];
                    energyOut += left[static_cast<std::size_t> (i)]
                                 * left[static_cast<std::size_t> (i)];
                }

            done += 512;
        }

        CHECK_NEAR (10.0 * std::log10 (energyOut / energyIn), 0.0, 0.05);
    }
}

TEZLA_TEST (membrana_linked_decisions_swap_with_the_channels)
{
    // Feed (A, B), then (B, A): if every dynamics decision is one decision
    // for both channels, the outputs swap bit for bit. An unlinked ride
    // would treat each channel by its own level and the swap would not
    // commute.
    MembranaEngine::Settings s;
    s.presence.amountDb = 6.0;
    s.detail.amountDb = 8.0;
    s.mic.distanceCm = 8.0;

    MembranaEngine forward, swapped;
    forward.prepare (kRate);
    forward.setSettings (s);
    swapped.prepare (kRate);
    swapped.setSettings (s);

    constexpr int kCount = 4096;
    std::vector<double> l1 (kCount), r1 (kCount), l2 (kCount), r2 (kCount);

    for (int i = 0; i < kCount; ++i)
    {
        const auto n = static_cast<std::size_t> (i);
        const double a = 0.05 * std::sin (2.0 * std::numbers::pi * 5000.0 * i / kRate);
        const double b = 0.02 * std::sin (2.0 * std::numbers::pi * 300.0 * i / kRate);
        l1[n] = a; r1[n] = b;
        l2[n] = b; r2[n] = a;
    }

    forward.process (l1.data(), r1.data(), kCount);
    swapped.process (l2.data(), r2.data(), kCount);

    for (int i = 0; i < kCount; ++i)
    {
        const auto n = static_cast<std::size_t> (i);
        CHECK (std::bit_cast<std::uint64_t> (l1[n]) == std::bit_cast<std::uint64_t> (r2[n]));
        CHECK (std::bit_cast<std::uint64_t> (r1[n]) == std::bit_cast<std::uint64_t> (l2[n]));
    }
}

TEZLA_TEST (membrana_one_channels_level_drives_the_other_channels_lift)
{
    // The swap test above proves symmetry, but per-channel UNLINKED
    // detectors are also swap-symmetric -- what only linking gives is
    // cross-modulation: the loud channel's level decides the quiet
    // channel's gain. Left carries a mid-window 6 kHz tone; right carries
    // the same tone 26 dB quieter -- far below the detail floor on its
    // own. Linked, the right channel is lifted by the LEFT channel's
    // level; unlinked it would get nothing. Measured: right gains
    // +1.81 dB -- the same figure the -38 dB tone earns in the mono
    // DetailLift test, arriving on the channel that did nothing to earn
    // it, which is the linking working.
    MembranaEngine engine;
    MembranaEngine::Settings s;
    s.detail.amountDb = 6.0;
    engine.prepare (kRate);
    engine.setSettings (s);

    const double loud = std::pow (10.0, -38.0 / 20.0);
    const double quiet = std::pow (10.0, -64.0 / 20.0);
    const double dPhase = 2.0 * std::numbers::pi * 6000.0 / kRate;

    std::vector<double> left (512), right (512), inRight (512);
    double phase = 0.0, energyIn = 0.0, energyOut = 0.0;
    const int settle = 72000, measure = 24000;
    int done = 0;

    while (done < settle + measure)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto n = static_cast<std::size_t> (i);
            const double tone = std::sin (phase);
            left[n] = loud * tone;
            right[n] = inRight[n] = quiet * tone;
            phase += dPhase;
        }

        engine.process (left.data(), right.data(), 512);

        for (int i = 0; i < 512; ++i)
            if (done + i >= settle)
            {
                const auto n = static_cast<std::size_t> (i);
                energyIn += inRight[n] * inRight[n];
                energyOut += right[n] * right[n];
            }

        done += 512;
    }

    const double rightGainDb = 10.0 * std::log10 (energyOut / energyIn);
    CHECK (rightGainDb > 1.0);
    CHECK_NEAR (rightGainDb, 1.81, 0.5);
}

TEZLA_TEST (membrana_presence_stage_is_wired_into_the_chain)
{
    // A static presence shelf (track 0) of 6 dB at a 1 kHz corner must
    // arrive at the output: a 16 kHz tone through the whole engine reads
    // +5.99 dB (the tracker's own tests pin the asymptote; this pins the
    // wiring).
    MembranaEngine engine;
    MembranaEngine::Settings s;
    s.presence.amountDb = 6.0;
    s.presence.track01 = 0.0;
    s.presence.frequencyHz = 1000.0;
    engine.prepare (kRate);
    engine.setSettings (s);

    const double dPhase = 2.0 * std::numbers::pi * 16000.0 / kRate;
    double phase = 0.0, energyIn = 0.0, energyOut = 0.0;
    std::vector<double> left (512), right (512), input (512);
    const int settle = 144000, measure = 24000;
    int done = 0;

    while (done < settle + measure)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto n = static_cast<std::size_t> (i);
            input[n] = 0.1 * std::sin (phase);
            left[n] = right[n] = input[n];
            phase += dPhase;
        }

        engine.process (left.data(), right.data(), 512);

        for (int i = 0; i < 512; ++i)
            if (done + i >= settle)
            {
                const auto n = static_cast<std::size_t> (i);
                energyIn += input[n] * input[n];
                energyOut += left[n] * left[n];
            }

        done += 512;
    }

    CHECK_NEAR (10.0 * std::log10 (energyOut / energyIn), 6.0, 0.1);
}

TEZLA_TEST (membrana_gain_riding_leaves_no_measurable_sidebands)
{
    // Section 7's sweep, measured rather than assumed. The path is linear
    // except the smoothed gain rides, so the worst case is a steady tone
    // sitting mid-knee at MAXIMUM dynamics settings, where any detector
    // ripple would modulate the gain and print sidebands. Project the
    // settled output onto its fundamental and measure everything else:
    // -149.8 dBFS RMS measured -- the log-domain smoothing doing exactly
    // what the read paper's Fig. 9 promises. Asserted at -130 to leave
    // 20 dB for platform variance; the -60 dBFS budget sits 90 dB away.
    MembranaEngine engine;
    MembranaEngine::Settings s;
    s.presence.amountDb = 9.0;
    s.presence.track01 = 1.0;
    s.presence.thresholdDb = -28.0;
    s.detail.amountDb = 12.0;
    engine.prepare (kRate);
    engine.setSettings (s);

    const double amplitude = std::pow (10.0, -34.0 / 20.0);
    const double dPhase = 2.0 * std::numbers::pi * 6000.0 / kRate;
    const int settle = 144000;
    const int measure = 1 << 16;

    std::vector<double> left (512), right (512);
    std::vector<double> out;
    out.reserve (static_cast<std::size_t> (measure));

    double phase = 0.0;
    int done = 0;

    while (done < settle + measure)
    {
        for (int i = 0; i < 512; ++i)
        {
            left[static_cast<std::size_t> (i)] =
                right[static_cast<std::size_t> (i)] = amplitude * std::sin (phase);
            phase += dPhase;
        }

        engine.process (left.data(), right.data(), 512);

        for (int i = 0; i < 512; ++i)
            if (done + i >= settle && out.size() < static_cast<std::size_t> (measure))
                out.push_back (left[static_cast<std::size_t> (i)]);

        done += 512;
    }

    double sinSum = 0.0, cosSum = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        sinSum += out[static_cast<std::size_t> (i)] * std::sin (dPhase * i);
        cosSum += out[static_cast<std::size_t> (i)] * std::cos (dPhase * i);
    }

    sinSum *= 2.0 / measure;
    cosSum *= 2.0 / measure;

    double residual = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        const double fundamental = sinSum * std::sin (dPhase * i)
                                   + cosSum * std::cos (dPhase * i);
        const double difference = out[static_cast<std::size_t> (i)] - fundamental;
        residual += difference * difference;
    }

    CHECK (10.0 * std::log10 (residual / measure) < -130.0);
}

TEZLA_TEST (membrana_character_is_rate_independent_through_the_engine)
{
    // The capsule's own tests pin the design; this pins the WIRING: the
    // same physical settings through the whole engine at four rates land
    // the same gains at fixed physical frequencies.
    const double freqs[] = { 1000.0, 7000.0, 12000.0 };
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    double gains[4][3];

    for (int ri = 0; ri < 4; ++ri)
    {
        for (int fi = 0; fi < 3; ++fi)
        {
            MembranaEngine engine;
            MembranaEngine::Settings s;
            s.mic.distanceCm = 3.0;
            s.mic.character01 = 1.0;
            engine.prepare (rates[ri]);
            engine.setSettings (s);

            const double dPhase = 2.0 * std::numbers::pi * freqs[fi] / rates[ri];
            double phase = 0.0, energyIn = 0.0, energyOut = 0.0;
            std::vector<double> left (512), right (512), input (512);
            const int settle = static_cast<int> (rates[ri] / 2.0);
            const int measure = settle;
            int done = 0;

            while (done < settle + measure)
            {
                for (int i = 0; i < 512; ++i)
                {
                    input[static_cast<std::size_t> (i)] = 0.05 * std::sin (phase);
                    left[static_cast<std::size_t> (i)] =
                        right[static_cast<std::size_t> (i)] =
                            input[static_cast<std::size_t> (i)];
                    phase += dPhase;
                }

                engine.process (left.data(), right.data(), 512);

                for (int i = 0; i < 512; ++i)
                    if (done + i >= settle)
                    {
                        energyIn += input[static_cast<std::size_t> (i)]
                                    * input[static_cast<std::size_t> (i)];
                        energyOut += left[static_cast<std::size_t> (i)]
                                     * left[static_cast<std::size_t> (i)];
                    }

                done += 512;
            }

            gains[ri][fi] = 10.0 * std::log10 (energyOut / energyIn);
        }
    }

    for (int fi = 0; fi < 3; ++fi)
        for (int ri = 1; ri < 4; ++ri)
            CHECK_NEAR (gains[ri][fi], gains[0][fi], 0.2);
}

TEZLA_TEST (membrana_output_is_bit_identical_across_host_block_sizes)
{
    // The engine cuts its sample loop at its own kControlChunk boundary and
    // carries the remainder across process() calls, so the host's block
    // size cannot bend anything -- 64-sample and 480-sample hosting of the
    // same stream are bit-identical.
    MembranaEngine::Settings s;
    s.mic.distanceCm = 8.0;
    s.presence.amountDb = 6.0;
    s.detail.amountDb = 8.0;

    MembranaEngine a, b;
    a.prepare (kRate);
    a.setSettings (s);
    b.prepare (kRate);
    b.setSettings (s);

    constexpr int kTotal = 48000;   // divisible by 64 and 480
    std::vector<double> inL (kTotal), inR (kTotal);

    for (int i = 0; i < kTotal; ++i)
    {
        const auto n = static_cast<std::size_t> (i);
        inL[n] = 0.1 * std::sin (2.0 * std::numbers::pi * 900.0 * i / kRate)
                 * (i < kTotal / 2 ? 1.0 : 0.03);
        inR[n] = 0.7 * inL[n];
    }

    std::vector<double> outAL (inL), outAR (inR), outBL (inL), outBR (inR);

    for (int n = 0; n < kTotal; n += 64)
        a.process (outAL.data() + n, outAR.data() + n, 64);

    for (int n = 0; n < kTotal; n += 480)
        b.process (outBL.data() + n, outBR.data() + n, 480);

    for (int i = 0; i < kTotal; ++i)
    {
        const auto n = static_cast<std::size_t> (i);
        CHECK (std::bit_cast<std::uint64_t> (outAL[n]) == std::bit_cast<std::uint64_t> (outBL[n]));
        CHECK (std::bit_cast<std::uint64_t> (outAR[n]) == std::bit_cast<std::uint64_t> (outBR[n]));
    }
}
