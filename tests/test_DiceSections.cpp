// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <array>
#include <string_view>

#include <DiceSections.hpp>

using namespace tezla::sonitus;

namespace
{
// **Every parameter id Sonitus publishes**, in publication order, taken from
// `tezla-render params` rather than typed. Framework-free tests cannot ask the
// plugin for its own list, so this is the copy -- and the copy is what makes
// the completeness check below mean anything: a new parameter that nobody
// classifies is caught when this list is refreshed, and the roller treats an
// unclassified parameter as locked in the meantime, so the failure mode is
// "the dice ignores it" rather than "the dice moves the wrong thing".
constexpr std::string_view kEveryParameter[] {
    "shapeA", "octaveA", "semitonesA", "centsA", "widthA", "morphA", "unisonA", "detuneA",
    "spreadA", "driftA", "levelA", "shapeB", "octaveB", "semitonesB", "centsB", "widthB",
    "morphB", "unisonB", "detuneB", "spreadB", "driftB", "levelB", "syncB", "pmIndex",
    "pmReverse", "feedbackA", "feedbackB", "subShape", "subOctave", "subLevel", "ringAmount",
    "foldAmount", "kargyraa", "kargyraaRasp", "kargyraaDivisor", "filterMode", "cutoff",
    "resonance", "filterDrive", "filterTrack", "filterFm", "filterVel", "ampAttack",
    "ampDecay", "ampSustain", "ampRelease", "ampHold", "ampAttackT", "ampDecayT",
    "ampReleaseT", "ampSnap", "ampVelocity", "env1Attack", "env1Decay", "env1Sustain",
    "env1Release", "env1Hold", "env1AttackT", "env1DecayT", "env1ReleaseT", "env1Snap",
    "env2Attack", "env2Decay", "env2Sustain", "env2Release", "env2Hold", "env2AttackT",
    "env2DecayT", "env2ReleaseT", "env2Snap", "adv1Enable", "adv1Loop", "adv1Snap",
    "adv1Points", "adv1Sustain", "adv1LoopStart", "adv1T1", "adv1L1", "adv1C1", "adv1T2",
    "adv1L2", "adv1C2", "adv1T3", "adv1L3", "adv1C3", "adv1T4", "adv1L4", "adv1C4", "adv1T5",
    "adv1L5", "adv1C5", "adv1T6", "adv1L6", "adv1C6", "adv1T7", "adv1L7", "adv1C7", "adv1T8",
    "adv1L8", "adv1C8", "adv1T9", "adv1L9", "adv1C9", "adv1T10", "adv1L10", "adv1C10",
    "adv1T11", "adv1L11", "adv1C11", "adv1T12", "adv1L12", "adv1C12", "adv1T13", "adv1L13",
    "adv1C13", "adv1T14", "adv1L14", "adv1C14", "adv1T15", "adv1L15", "adv1C15", "adv1T16",
    "adv1L16", "adv1C16", "adv2Enable", "adv2Loop", "adv2Snap", "adv2Points", "adv2Sustain",
    "adv2LoopStart", "adv2T1", "adv2L1", "adv2C1", "adv2T2", "adv2L2", "adv2C2", "adv2T3",
    "adv2L3", "adv2C3", "adv2T4", "adv2L4", "adv2C4", "adv2T5", "adv2L5", "adv2C5", "adv2T6",
    "adv2L6", "adv2C6", "adv2T7", "adv2L7", "adv2C7", "adv2T8", "adv2L8", "adv2C8", "adv2T9",
    "adv2L9", "adv2C9", "adv2T10", "adv2L10", "adv2C10", "adv2T11", "adv2L11", "adv2C11",
    "adv2T12", "adv2L12", "adv2C12", "adv2T13", "adv2L13", "adv2C13", "adv2T14", "adv2L14",
    "adv2C14", "adv2T15", "adv2L15", "adv2C15", "adv2T16", "adv2L16", "adv2C16", "adv3Enable",
    "adv3Loop", "adv3Snap", "adv3Points", "adv3Sustain", "adv3LoopStart", "adv3T1", "adv3L1",
    "adv3C1", "adv3T2", "adv3L2", "adv3C2", "adv3T3", "adv3L3", "adv3C3", "adv3T4", "adv3L4",
    "adv3C4", "adv3T5", "adv3L5", "adv3C5", "adv3T6", "adv3L6", "adv3C6", "adv3T7", "adv3L7",
    "adv3C7", "adv3T8", "adv3L8", "adv3C8", "adv3T9", "adv3L9", "adv3C9", "adv3T10", "adv3L10",
    "adv3C10", "adv3T11", "adv3L11", "adv3C11", "adv3T12", "adv3L12", "adv3C12", "adv3T13",
    "adv3L13", "adv3C13", "adv3T14", "adv3L14", "adv3C14", "adv3T15", "adv3L15", "adv3C15",
    "adv3T16", "adv3L16", "adv3C16", "keyMode", "polyphony", "glide", "bendRange", "lfo1Wave",
    "lfo1Rate", "lfo1Sync", "lfo1Div", "lfo1Smooth", "lfo1Retrig", "lfo1Key", "lfo1Att",
    "lfo2Wave", "lfo2Rate", "lfo2Sync", "lfo2Div", "lfo2Smooth", "lfo2Retrig", "lfo2Key",
    "lfo2Att", "seqRate", "seqLength", "seqGlide", "seqToLfoRate", "seq1", "seq2", "seq3",
    "seq4", "seq5", "seq6", "seq7", "seq8", "seq9", "seq10", "seq11", "seq12", "seq13",
    "seq14", "seq15", "seq16", "modSource1", "modDest1", "modDepth1", "modSource2", "modDest2",
    "modDepth2", "modSource3", "modDest3", "modDepth3", "modSource4", "modDest4", "modDepth4",
    "modSource5", "modDest5", "modDepth5", "modSource6", "modDest6", "modDepth6",
    "gmodSource1", "gmodDest1", "gmodDepth1", "gmodSource2", "gmodDest2", "gmodDepth2",
    "gmodSource3", "gmodDest3", "gmodDepth3", "splitHz", "subMono", "subSplit", "order",
    "tubeDrive", "combMode", "combTime", "combTrack", "combFeed", "combDamp", "combSpread",
    "combMix", "combInvert", "phaseFreq", "phaseStages", "formantMorph", "formantSharp",
    "formantMix", "formantLock", "formantHarmonic", "formantNotch", "formantNotchDepth",
    "tilt", "output", "oversampling"
};

constexpr int kCount = static_cast<int> (std::size (kEveryParameter));
} // namespace

TEZLA_TEST (every_parameter_belongs_to_exactly_one_dice_section)
{
    CHECK (kCount == 324);

    std::array<int, 8> tally {};
    int unknown = 0;

    for (const auto id : kEveryParameter)
    {
        const auto section = diceSectionFor (id);

        ++tally[static_cast<std::size_t> (section)];

        if (section == DiceSection::unknown)
            ++unknown;
    }

    CHECK (unknown == 0);

    // The measured split, pinned. These are counts rather than a shape, and
    // they are here so that moving a parameter between sections has to be a
    // deliberate edit of this line rather than something that slides past.
    //
    // ENV is 190 of the 324 because the three ADV envelopes are 48 parameters
    // each. That is worth seeing rather than being surprised by: a roll with
    // everything but ENV locked still moves more than half the instrument.
    CHECK (tally[static_cast<std::size_t> (DiceSection::osc)] == 37);
    CHECK (tally[static_cast<std::size_t> (DiceSection::filter)] == 7);
    CHECK (tally[static_cast<std::size_t> (DiceSection::envelope)] == 190);
    CHECK (tally[static_cast<std::size_t> (DiceSection::modulation)] == 63);
    CHECK (tally[static_cast<std::size_t> (DiceSection::mangle)] == 21);
    CHECK (tally[static_cast<std::size_t> (DiceSection::play)] == 4);
    CHECK (tally[static_cast<std::size_t> (DiceSection::output)] == 2);

    int total = 0;
    for (int i = 0; i < 7; ++i)
        total += tally[static_cast<std::size_t> (i)];

    CHECK (total == kCount);
}

TEZLA_TEST (the_dice_sections_put_the_awkward_ids_where_the_panel_does)
{
    // The cases where reading the id is not obviously the same as reading the
    // panel, each one a decision rather than a fall-through.

    // An FM pair is an oscillator arrangement, not a modulation route.
    CHECK (diceSectionFor ("pmIndex") == DiceSection::osc);
    CHECK (diceSectionFor ("pmReverse") == DiceSection::osc);
    CHECK (diceSectionFor ("feedbackA") == DiceSection::osc);

    // On the MANGLE page, but they are the sub oscillator's routing.
    CHECK (diceSectionFor ("subMono") == DiceSection::osc);
    CHECK (diceSectionFor ("subSplit") == DiceSection::osc);

    // Velocity into the amplitude envelope is part of the envelope.
    CHECK (diceSectionFor ("ampVelocity") == DiceSection::envelope);

    // ...but velocity into the filter is part of the filter.
    CHECK (diceSectionFor ("filterVel") == DiceSection::filter);

    // The sixteenth ADV point, which is the one a hand-written table would
    // have missed.
    CHECK (diceSectionFor ("adv3C16") == DiceSection::envelope);

    // Both matrices, not just the voice one.
    CHECK (diceSectionFor ("gmodDepth3") == DiceSection::modulation);

    // An output tone control that lives on the MANGLE page stays with the
    // page, so the lock is where the player can see it.
    CHECK (diceSectionFor ("tilt") == DiceSection::mangle);

    // The two in the header, which are the two that can hurt.
    CHECK (diceSectionFor ("output") == DiceSection::output);
    CHECK (diceSectionFor ("oversampling") == DiceSection::output);

    // And anything unrecognised is unknown, which the roller reads as locked.
    CHECK (diceSectionFor ("somethingAddedTomorrow") == DiceSection::unknown);
    CHECK (diceSectionFor ("") == DiceSection::unknown);
}

// ---------------------------------------------------------------------------
// How far a roll moves a control
// ---------------------------------------------------------------------------

TEZLA_TEST (the_dice_amount_is_a_fraction_of_the_way_there_not_a_cap)
{
    // Full strength is the roll that shipped, **bit for bit**: the target and
    // nothing else. Asserted over a grid rather than at one pair, because the
    // failure it guards against -- trusting `c + 1 * (t - c)` to reduce to `t`
    // -- is a last-place error that only shows at some values.
    int exact = 0;

    for (int c = 0; c <= 20; ++c)
        for (int t = 0; t <= 20; ++t)
        {
            const float current = static_cast<float> (c) / 20.0f;
            const float target = static_cast<float> (t) / 20.0f;

            CHECK (diceValueFor (current, target, 1.0f) == target);
            CHECK (diceValueFor (current, target, 0.0f) == current);

            ++exact;
        }

    CHECK (exact == 441);

    // And the shape in between: half way is half way, and it stays inside the
    // range for any inputs that are.
    CHECK_NEAR (diceValueFor (0.2f, 0.8f, 0.5f), 0.5f, 1.0e-6);
    CHECK_NEAR (diceValueFor (0.8f, 0.2f, 0.5f), 0.5f, 1.0e-6);
    CHECK_NEAR (diceValueFor (0.9f, 0.1f, 0.25f), 0.7f, 1.0e-6);

    for (int c = 0; c <= 20; ++c)
        for (int t = 0; t <= 20; ++t)
            for (int a = 0; a <= 20; ++a)
            {
                const float value = diceValueFor (static_cast<float> (c) / 20.0f,
                                                  static_cast<float> (t) / 20.0f,
                                                  static_cast<float> (a) / 20.0f);

                CHECK (value >= 0.0f && value <= 1.0f);
            }

    // The bias the "+/- amount, clamped" shape would have had, stated as the
    // property this one does not: a control sitting at the bottom of its range
    // can still reach the top at full strength, and reaches exactly as far
    // proportionally at half strength as one sitting at the top.
    CHECK (diceValueFor (0.0f, 1.0f, 1.0f) == 1.0f);
    CHECK (diceValueFor (1.0f, 0.0f, 1.0f) == 0.0f);
    CHECK_NEAR (diceValueFor (0.0f, 1.0f, 0.5f), 0.5f, 1.0e-6);
    CHECK_NEAR (diceValueFor (1.0f, 0.0f, 0.5f), 0.5f, 1.0e-6);
}
