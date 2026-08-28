// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <vector>

#include <tezla/dsp/Modulation.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

using Source = Modulation::Source;
using Slot   = Modulation::Slot;

Modulation made()
{
    Modulation modulation;
    modulation.prepare (kRate);
    return modulation;
}

/// One chunk of silence, so the sources move and the level follower does not.
void advanceSilent (Modulation& modulation, int numSamples)
{
    modulation.advance (numSamples, nullptr, 0, false, 0.0);
}
} // namespace

TEZLA_TEST (modulation_is_inactive_until_something_is_assigned)
{
    // The load-bearing property. The caller skips its whole modulation path
    // when this is false, which is what makes an unassigned plugin byte-for-byte
    // what it was before any of this existed.
    auto modulation = made();
    CHECK (! modulation.isActive());

    // A source with no depth is not an assignment.
    modulation.setSlot (0, { Source::lfo1, 3, 0.0 });
    CHECK (! modulation.isActive());

    // Nor is a depth with no source.
    modulation.setSlot (0, { Source::off, 3, 0.8 });
    CHECK (! modulation.isActive());

    modulation.setSlot (0, { Source::lfo1, 3, 0.8 });
    CHECK (modulation.isActive());

    // And taking it away again puts it back.
    modulation.setSlot (0, {});
    CHECK (! modulation.isActive());
}

TEZLA_TEST (modulation_offsets_are_zero_for_everything_unassigned)
{
    auto modulation = made();
    modulation.setSlot (0, { Source::lfo1, 7, 1.0 });

    modulation.lfo (0).setRateHz (5.0);
    advanceSilent (modulation, 256);

    for (int destination = 0; destination < Modulation::kMaxDestinations; ++destination)
        if (destination != 7)
            CHECK (modulation.offsetFor (destination) == 0.0);
}

TEZLA_TEST (modulation_depth_of_exactly_zero_contributes_exactly_zero)
{
    // Not "very little" -- zero, so a slot parked at the centre of its depth
    // control cannot move a destination by one bit. The same property Halo's
    // Width carries at Normal.
    auto modulation = made();

    modulation.setSlot (0, { Source::lfo1, 2, 0.5 });   // something, so it is active
    modulation.setSlot (1, { Source::lfo2, 5, 0.0 });   // and nothing, at depth zero

    modulation.lfo (0).setRateHz (3.0);
    modulation.lfo (1).setRateHz (7.0);

    for (int i = 0; i < 50; ++i)
    {
        advanceSilent (modulation, 64);
        CHECK (modulation.offsetFor (5) == 0.0);
    }
}

TEZLA_TEST (modulation_sums_slots_pointing_at_the_same_destination)
{
    // Two sources on one knob is a legitimate thing to want -- a slow sweep
    // with a fast wobble on top -- and the arithmetic is a sum, not a
    // last-one-wins.
    auto modulation = made();

    modulation.setSlot (0, { Source::lfo1, 4, 0.3 });
    modulation.setSlot (1, { Source::lfo2, 4, 0.2 });

    // Park both LFOs at a known phase by using saws at zero rate with offsets.
    modulation.lfo (0).setWave (Lfo::Wave::sawUp);
    modulation.lfo (0).setRateHz (0.0);
    modulation.lfo (0).setPhaseOffset (1.0);     // wraps to 0 -> -1

    modulation.lfo (1).setWave (Lfo::Wave::sawUp);
    modulation.lfo (1).setRateHz (0.0);
    modulation.lfo (1).setPhaseOffset (0.5);     // 0

    advanceSilent (modulation, 64);

    CHECK_NEAR (modulation.offsetFor (4), 0.3 * -1.0 + 0.2 * 0.0, 1.0e-12);
}

TEZLA_TEST (modulation_negative_depth_inverts)
{
    // The interesting half of the control: a level follower at negative depth
    // is a compressor made of whatever it is pointed at.
    auto modulation = made();

    modulation.lfo (0).setWave (Lfo::Wave::sawUp);
    modulation.lfo (0).setRateHz (0.0);
    modulation.lfo (0).setPhaseOffset (0.75);    // +0.5

    modulation.setSlot (0, { Source::lfo1, 1, 0.4 });
    advanceSilent (modulation, 64);
    const double positive = modulation.offsetFor (1);

    modulation.setSlot (0, { Source::lfo1, 1, -0.4 });
    advanceSilent (modulation, 64);
    const double negative = modulation.offsetFor (1);

    CHECK (positive > 0.0);
    CHECK_NEAR (negative, -positive, 1.0e-12);
}

TEZLA_TEST (modulation_level_follower_reads_the_input)
{
    // No trigger, no MIDI: loud audio in, high control value out.
    auto modulation = made();
    modulation.setSlot (0, { Source::level, 0, 1.0 });
    modulation.levelFollower().setAttackMs (1.0);
    modulation.levelFollower().setSensitivityDb (0.0);

    std::vector<double> loud (4096, 0.8);
    const double* pointers[1] { loud.data() };

    modulation.advance (4096, pointers, 1, false, 0.0);
    const double onLoud = modulation.offsetFor (0);

    std::vector<double> quiet (4096, 0.0);
    const double* quietPointers[1] { quiet.data() };

    modulation.levelFollower().setReleaseMs (1.0);
    modulation.advance (4096, quietPointers, 1, false, 0.0);
    const double onQuiet = modulation.offsetFor (0);

    CHECK (onLoud > 0.7);
    CHECK (onQuiet == 0.0);
}

TEZLA_TEST (modulation_synced_lfos_follow_the_transport_and_free_run_without_one)
{
    auto modulation = made();
    modulation.setSlot (0, { Source::lfo1, 0, 1.0 });
    modulation.lfo (0).setWave (Lfo::Wave::sawUp);
    modulation.lfo (0).setRateHz (1.0);
    modulation.setLfoSync (0, true, 1.0);

    // With a transport, the same beat gives the same offset however long the
    // session has been running.
    modulation.advance (64, nullptr, 0, true, 2.25);
    const double atQuarter = modulation.offsetFor (0);

    modulation.advance (64, nullptr, 0, true, 900.25);
    CHECK_NEAR (modulation.offsetFor (0), atQuarter, 1.0e-9);

    // Without one -- a standalone, or a stopped transport -- it has to keep
    // moving rather than freeze, or the plugin looks broken outside a DAW.
    const double before = modulation.offsetFor (0);
    for (int i = 0; i < 20; ++i)
        advanceSilent (modulation, 1024);

    CHECK (modulation.offsetFor (0) != before);
}

TEZLA_TEST (modulation_does_nothing_at_all_while_inactive)
{
    // advance() returns immediately when nothing is assigned, so the sources do
    // not run and the offsets stay zero. This is the cheap half of the fast
    // path the caller depends on.
    auto modulation = made();
    modulation.lfo (0).setRateHz (10.0);

    for (int i = 0; i < 100; ++i)
    {
        advanceSilent (modulation, 512);

        for (int destination = 0; destination < 8; ++destination)
            CHECK (modulation.offsetFor (destination) == 0.0);
    }
}

TEZLA_TEST (modulation_reports_which_destinations_are_modulated)
{
    // What a knob's ring asks before it draws anything.
    auto modulation = made();
    CHECK (! modulation.isModulated (3));

    modulation.setSlot (2, { Source::lfo3, 3, -0.6 });
    CHECK (modulation.isModulated (3));
    CHECK (! modulation.isModulated (4));

    modulation.setSlot (2, { Source::lfo3, 3, 0.0 });
    CHECK (! modulation.isModulated (3));
}

TEZLA_TEST (modulation_clamps_a_slot_into_range_rather_than_reading_out_of_bounds)
{
    // Slots come from parameters, and a destination index is a choice index --
    // a stale project or a renumbered list could hand over anything.
    auto modulation = made();

    modulation.setSlot (0, { Source::lfo1, 9999, 4.0 });
    CHECK (modulation.getSlot (0).destination < Modulation::kMaxDestinations);
    CHECK (modulation.getSlot (0).depth == 1.0);

    modulation.setSlot (1, { Source::lfo1, -5, -4.0 });
    CHECK (modulation.getSlot (1).destination >= 0);
    CHECK (modulation.getSlot (1).depth == -1.0);

    // And an out-of-range slot index is ignored rather than written anywhere.
    modulation.setSlot (999, { Source::lfo1, 0, 1.0 });
    modulation.setSlot (-1, { Source::lfo1, 0, 1.0 });

    CHECK (modulation.offsetFor (-1) == 0.0);
    CHECK (modulation.offsetFor (Modulation::kMaxDestinations) == 0.0);
}

TEZLA_TEST (modulation_does_not_depend_on_the_host_block_size)
{
    // Everything downstream inherits this: if the sources drifted with the
    // buffer size, so would every parameter they touch.
    const auto run = [] (int chunk, int chunks)
    {
        auto modulation = made();
        modulation.setSlot (0, { Source::lfo1, 0, 0.7 });
        modulation.lfo (0).setWave (Lfo::Wave::triangle);
        modulation.lfo (0).setRateHz (2.3);
        modulation.lfo (0).setSmooth (0.4);

        for (int i = 0; i < chunks; ++i)
            advanceSilent (modulation, chunk);

        return modulation.offsetFor (0);
    };

    CHECK_NEAR (run (32, 512), run (512, 32), 1.0e-9);
}
