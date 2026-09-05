// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Which section of the panel a parameter belongs to.
//
// Framework-free on purpose: it is pure string work, so a test can hold it to
// the whole id list without a plugin instance, and the classification cannot
// drift from what the panel does because there is only one of it.
//
// ---------------------------------------------------------------------------
// The one rule that matters, and the reason Stryda has this at all
// ---------------------------------------------------------------------------
//
// An id that matches nothing returns `unknown`. On Sonitus that state was
// invisible for three phases -- twenty-one parameters were silently unrolled
// because the header documented a `tezla-render dice` gate that had never been
// written. Stryda's gate exists from the first parameter, exits non-zero on
// any unknown, and the suite asserts zero as well. **Run it after adding
// parameters** (CLAUDE.md section 11).

#include <cstddef>
#include <initializer_list>
#include <string_view>

namespace tezla::stryda
{

/// The sections, in tab order.
enum class DiceSection
{
    operators,    ///< the six strips: ratio, character, level, envelopes, mode
    matrix,       ///< the 36 cells, the noise row, key scaling, velocity
    voice,        ///< filter, sub lane, unison
    sequencer,    ///< the ratio pattern and its transport
    mangle,       ///< split, vowel lane, the chain
    modulation,   ///< ADV envelopes, LFOs, macros, slots
    global,       ///< oversampling, polyphony, master, tuning, the cap
    unknown       ///< matched nothing -- never rolled; see the header comment
};

inline constexpr int numDiceSections = 7;

[[nodiscard]] constexpr const char* diceSectionName (DiceSection section) noexcept
{
    switch (section)
    {
        case DiceSection::operators:  return "OPERATORS";
        case DiceSection::matrix:     return "MATRIX";
        case DiceSection::voice:      return "VOICE";
        case DiceSection::sequencer:  return "SEQ";
        case DiceSection::mangle:     return "MANGLE";
        case DiceSection::modulation: return "MOD";
        case DiceSection::global:     return "GLOBAL";
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

/// `o<n>` followed by a name, which is the operator id rule.
[[nodiscard]] constexpr bool isOperatorId (std::string_view id) noexcept
{
    return id.size() > 2 && id[0] == 'o' && id[1] >= '1' && id[1] <= '6';
}

/// `m<to><from>`, both digits: a matrix cell.
[[nodiscard]] constexpr bool isCellId (std::string_view id) noexcept
{
    return id.size() == 3 && id[0] == 'm'
             && id[1] >= '1' && id[1] <= '6'
             && id[2] >= '1' && id[2] <= '6';
}

/// `n<to>`: the noise row.
[[nodiscard]] constexpr bool isNoiseId (std::string_view id) noexcept
{
    return id.size() == 2 && id[0] == 'n' && id[1] >= '1' && id[1] <= '6';
}

/// `s<n>` or `v<n>`: a ratio step or a vowel step, one or two digits.
[[nodiscard]] constexpr bool isStepId (std::string_view id, char letter) noexcept
{
    if (id.size() < 2 || id.size() > 3 || id[0] != letter)
        return false;

    for (std::size_t i = 1; i < id.size(); ++i)
        if (id[i] < '0' || id[i] > '9')
            return false;

    return true;
}

/// `e<n>...` and `l<n>...`: an ADV envelope or an LFO.
[[nodiscard]] constexpr bool isEnvelopeId (std::string_view id) noexcept
{
    return id.size() > 2 && id[0] == 'e' && (id[1] == '1' || id[1] == '2');
}

[[nodiscard]] constexpr bool isLfoId (std::string_view id) noexcept
{
    return id.size() > 2 && id[0] == 'l' && (id[1] == '1' || id[1] == '2');
}
} // namespace detail

/// Which section an id belongs to, or `unknown`.
[[nodiscard]] constexpr DiceSection diceSectionFor (std::string_view id) noexcept
{
    using namespace detail;

    // The per-operator key scaling and velocity live on the MATRIX page's
    // scaling plate rather than on the strip, so they are classified by where
    // the player finds them rather than by their id's prefix.
    if (isOperatorId (id))
    {
        const auto tail = id.substr (2);

        if (isOneOf (tail, { "KeyBreak", "KeyLeft", "KeyRight", "VelLevel", "VelIndex" }))
            return DiceSection::matrix;

        // Feedback is the matrix's diagonal and is drawn there.
        if (tail == "Feedback")
            return DiceSection::matrix;

        return DiceSection::operators;
    }

    if (isCellId (id) || isNoiseId (id))
        return DiceSection::matrix;

    if (startsWith (id, "filter") || startsWith (id, "sub") || startsWith (id, "unison"))
        return DiceSection::voice;

    if (startsWith (id, "seq") || isStepId (id, 's'))
        return DiceSection::sequencer;

    if (startsWith (id, "vowel") || isStepId (id, 'v')
        || startsWith (id, "crush") || startsWith (id, "comb")
        || startsWith (id, "phaser") || startsWith (id, "comp")
        || startsWith (id, "mangle")
        || isOneOf (id, { "split", "downsample" }))
        return DiceSection::mangle;

    if (isEnvelopeId (id) || isLfoId (id)
        || startsWith (id, "mac") || startsWith (id, "mod"))
        return DiceSection::modulation;

    if (isOneOf (id, { "oversampling", "renderOversampling", "polyphony",
                       "master", "indexCap" }))
        return DiceSection::global;

    return DiceSection::unknown;
}

} // namespace tezla::stryda

/// The alias every plugin's `DiceSections.hpp` provides, so `tezla-render
/// dice` can audit any of them without naming a plugin. One main.cpp serves
/// the whole suite; hard-coding one plugin's namespace is what stopped it.
namespace tezla::dice
{
using stryda::DiceSection;
using stryda::diceSectionFor;
using stryda::diceSectionName;
} // namespace tezla::dice
