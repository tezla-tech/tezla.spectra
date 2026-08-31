// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Which part of the instrument a parameter belongs to, for DICEROLL's locks.
//
// ---------------------------------------------------------------------------
// Why this is a function of the id string
// ---------------------------------------------------------------------------
//
// A lock has to answer "is this parameter in the section the player locked",
// for all 324 of them, and the alternative to reading the id is a hand-written
// table of 324 entries that somebody has to remember to extend. The sixteen
// ADV points added last week would have needed forty-eight new rows in it, and
// forgetting them would have silently made an "envelopes locked" roll change
// the envelopes. The prefixes are already the naming scheme; this reads them.
//
// **Framework-free on purpose.** It is pure string work, so the tests can hold
// it to the whole id list without a plugin instance, and the classification
// cannot drift from what the panel does because there is only one of it.
//
// ---------------------------------------------------------------------------
// The one rule that matters
// ---------------------------------------------------------------------------
//
// An id that matches nothing returns `unknown`, and the roller treats unknown
// as **locked**. A new parameter is therefore left alone by the dice until
// somebody classifies it -- which is the safe direction to fail, and loud
// enough to notice, because `tezla-render dice` lists every unknown and the
// count is expected to be zero.

#include <cstddef>
#include <initializer_list>
#include <string_view>

namespace tezla::sonitus
{

/// The seven groups the DICEROLL page offers, in panel order.
enum class DiceSection
{
    osc,          ///< both oscillators, the sub, ring, fold, kargyraa, PM
    filter,       ///< the state-variable filter and its tracking
    envelope,     ///< the three AHDSRs and the three ADV envelopes
    modulation,   ///< LFOs, the sequencer, both matrices
    mangle,       ///< comb, phaser, formant, tube, the split
    play,         ///< keyboard mode, polyphony, glide, bend range
    output,       ///< the header pair: output level and oversampling
    unknown       ///< matched nothing -- never rolled; see the header comment
};

inline constexpr int numDiceSections = 7;

/// Panel names, indexed by the enum. `unknown` has none by design: it is not a
/// section the player can lock, it is a bug that has not been found yet.
[[nodiscard]] constexpr const char* diceSectionName (DiceSection section) noexcept
{
    switch (section)
    {
        case DiceSection::osc:        return "OSC";
        case DiceSection::filter:     return "FILTER";
        case DiceSection::envelope:   return "ENV";
        case DiceSection::modulation: return "MOD";
        case DiceSection::mangle:     return "MANGLE";
        case DiceSection::play:       return "PLAY";
        case DiceSection::output:     return "OUTPUT";
        case DiceSection::unknown:    break;
    }

    return "?";
}

namespace detail
{
[[nodiscard]] constexpr bool startsWith (std::string_view id, std::string_view prefix) noexcept
{
    return id.size() >= prefix.size() && id.substr (0, prefix.size()) == prefix;
}

[[nodiscard]] constexpr bool isOneOf (std::string_view id,
                                      std::initializer_list<std::string_view> names) noexcept
{
    for (const auto name : names)
        if (id == name)
            return true;

    return false;
}
} // namespace detail

/// Which section a parameter id belongs to.
///
/// Prefix first, then the handful of ids that are their own thing. Order
/// matters in one place and it is called out where it happens.
[[nodiscard]] constexpr DiceSection diceSectionFor (std::string_view id) noexcept
{
    using namespace detail;

    // ---- OSC ---------------------------------------------------------------
    //
    // Everything that makes the raw tone: both oscillators with their unison
    // banks, the sub, and the four shapers that sit in the voice before the
    // filter. `pmIndex`/`pmReverse`/`feedbackA`/`feedbackB` are here because
    // an FM pair is an oscillator arrangement, not a modulation route -- the
    // MOD section is the matrix, not everything that modulates.
    if (startsWith (id, "shape") || startsWith (id, "octave") || startsWith (id, "semitones")
        || startsWith (id, "cents") || startsWith (id, "width") || startsWith (id, "morph")
        || startsWith (id, "unison") || startsWith (id, "detune") || startsWith (id, "spread")
        || startsWith (id, "drift") || startsWith (id, "level") || startsWith (id, "sub")
        || startsWith (id, "kargyraa") || startsWith (id, "feedback")
        || startsWith (id, "pm"))
        return DiceSection::osc;

    if (isOneOf (id, { "syncB", "ringAmount", "foldAmount" }))
        return DiceSection::osc;

    // ---- FILTER ------------------------------------------------------------
    if (startsWith (id, "filter") || isOneOf (id, { "cutoff", "resonance" }))
        return DiceSection::filter;

    // ---- ENV ---------------------------------------------------------------
    //
    // `amp*` catches ampVelocity too, which belongs here: it is how hard the
    // amplitude envelope is played, not a separate thing.
    if (startsWith (id, "amp") || startsWith (id, "env1") || startsWith (id, "env2")
        || startsWith (id, "adv1") || startsWith (id, "adv2") || startsWith (id, "adv3"))
        return DiceSection::envelope;

    // ---- MOD ---------------------------------------------------------------
    if (startsWith (id, "lfo") || startsWith (id, "seq") || startsWith (id, "mod")
        || startsWith (id, "gmod"))
        return DiceSection::modulation;

    // ---- MANGLE ------------------------------------------------------------
    //
    // **`subMono` and `subSplit` are read before this**, by the OSC branch's
    // "sub" prefix -- and that is the one place the order carries meaning.
    // They live on the MANGLE page but they are the sub oscillator's routing,
    // so locking OSC should hold them. Said here because a reader arriving at
    // the MANGLE list would otherwise expect to find them in it.
    if (startsWith (id, "comb") || startsWith (id, "phase") || startsWith (id, "formant")
        || isOneOf (id, { "splitHz", "order", "tubeDrive", "tilt" }))
        return DiceSection::mangle;

    // ---- PLAY --------------------------------------------------------------
    if (isOneOf (id, { "keyMode", "polyphony", "glide", "bendRange" }))
        return DiceSection::play;

    // ---- OUTPUT ------------------------------------------------------------
    //
    // Two, and they are the two in the header rather than on any page: the
    // master level and the oversampling factor. Small enough to look like an
    // afterthought and the section most worth having, because it is the one
    // that can hurt -- a roll here lands the output at +12 dB. It is locked by
    // default for that reason.
    //
    // `tilt` is not here despite being an output tone control: it lives on the
    // MANGLE page, and a lock the player cannot find by looking at the panel
    // is a lock they will not trust.
    if (isOneOf (id, { "output", "oversampling" }))
        return DiceSection::output;

    return DiceSection::unknown;
}

/// Where a rolled control lands: `current` dragged `amount` of the way towards
/// a random `target`, all three on the normalised 0..1 range.
///
/// **Amount 1 returns the target exactly**, by an explicit branch rather than
/// by trusting `current + 1 * (target - current)` to reduce -- it does not, in
/// float, for most values, and "the full-strength roll is exactly the roll
/// that shipped" is a property worth being able to assert bit for bit.
///
/// The alternative shape -- uniform within +/- amount of current, clamped --
/// was rejected because at full strength it piles values against 0 and 1
/// instead of spreading them: a control at 0.5 could reach anywhere, one at
/// 0.02 could only crowd the bottom. Dragging towards a uniform target has no
/// such bias at any amount.
[[nodiscard]] constexpr float diceValueFor (float current, float target, float amount) noexcept
{
    if (amount >= 1.0f)
        return target;

    if (amount <= 0.0f)
        return current;

    return current + amount * (target - current);
}

} // namespace tezla::sonitus
