// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Where the unison stack's copies go -- eight ways, of which the first is the
// one that shipped.
//
// ---------------------------------------------------------------------------
// The observation
// ---------------------------------------------------------------------------
//
// `UnisonBank` places N oscillators symmetrically, in cents, shaped by its
// spread exponent. That is a reese. It is also one array short of being three
// other instruments, and `UnisonBank::setRankOffsets` is that array: a pitch
// offset in cents and a gain per copy, on top of the detune. This header
// decides what to put in it.
//
// ---------------------------------------------------------------------------
// Ranks are symmetric, and rank 0 always exists
// ---------------------------------------------------------------------------
//
//     rank(i) = i - (count - 1) / 2      (integer division)
//
// giving -1,0,1 for three copies and -1,0,1,2 for four. **Exactly one copy sits
// on the played pitch**, so turning Unison up adds notes *around* the note
// rather than moving it: Stack never detunes the instrument.
//
// Detune mode is deliberately not like this -- `UnisonBank::position(0)` is the
// bottom of the stack, so voice 0 sits half the detune flat, which is why hard
// sync uses a dedicated master accumulator at the nominal pitch rather than one
// of the bank's oscillators. None of that changes here.
//
// ---------------------------------------------------------------------------
// Six of the modes are a table; two are not
// ---------------------------------------------------------------------------
//
// The five fixed modes are one generating interval each, `offset = rank * g`,
// and they ignore the tuning entirely -- a tritone is 600 cents whatever scale
// is loaded, which is the point of having them beside Scale.
//
// **Scale** is the one only this instrument can do: copy at rank r plays the
// *key* `note + r * step`, through the loaded tuning. `Tuning::frequencyFor`
// already handles keyboard maps, non-octave repeats and concert pitch, so one
// call gives correct behaviour in all 44 built-ins and any `.scl` file -- and
// what a "step" means is then whatever the tuning says a key means. A semitone
// in twelve-tone equal temperament; a scale degree under a keyboard map; a
// Bohlen-Pierce degree in a scale that repeats at 3/1. A rank whose key falls
// off the keyboard gets gain **exactly 0**, not a clamped note: two copies on
// one pitch is a quiet lie, a silent copy is not.
//
// **Shepard** is in `dsp/Shepard.hpp`, with the theorem that makes it work.
// One thing about it belongs here, though, because it is a units question and
// getting it wrong would be inaudible until two oscillators disagreed:
//
// > `shepardRanks` takes a phase in **turns**, and one turn moves a copy the
// > whole N-octave span. The engine's accumulator is in **octaves travelled**,
// > because that is what the rate control reads and what the ear hears -- so
// > the conversion is `phase = octaves / count`. A bank of seven copies turns
// > once per seven octaves and one of three turns once per three, and both
// > glide at the same audible speed. Dividing by the wrong thing would make
// > oscillator A rise faster than B purely because it had more copies.

#include <algorithm>
#include <array>
#include <cmath>

#include <tezla/dsp/Shepard.hpp>
#include <tezla/dsp/Tuning.hpp>
#include <tezla/dsp/UnisonBank.hpp>

namespace tezla::sonitus {

/// How the unison stack is placed. **Append-only** -- a choice parameter stores
/// an index and `choices::stack` is indexed straight into this. CLAUDE.md §8.
enum class StackMode
{
    detune = 0,   ///< cents, symmetric: the reese, and the only bit-exact one
    octaves,      ///< 1200 -- organ registration, the doubled pulse
    fifths,       ///< 700  -- quintal, hollow and wide
    tritones,     ///< 600  -- symmetric, rootless, unresolvable
    cluster,      ///< 100  -- the tone cluster
    diminished,   ///< 300  -- no root by construction
    scale,        ///< degrees of the loaded tuning, `step` keys apart
    shepard,      ///< the endless glissando

    count
};

/// The maximum copies any stack can have -- the bank's own ceiling.
inline constexpr int kMaxStackCopies = dsp::UnisonBank::kMaxVoices;

/// Widest and narrowest step Scale mode offers, in keys.
inline constexpr int kMinimumStackStep = 1;
inline constexpr int kMaximumStackStep = 7;

/// Where the Shepard accumulator wraps, in octaves travelled.
///
/// 420 is divisible by every copy count from 1 to 7, so `octaves / count` lands
/// on a whole number of turns at the wrap for every one of them and the wrap is
/// bit-exact rather than merely small. At the fastest rate that is about two
/// minutes, which keeps the accumulator's precision where it started.
inline constexpr double kShepardWrapOctaves = 420.0;

/// The generating interval in cents, or 0 for the modes that are not a table.
[[nodiscard]] constexpr double stackIntervalCents (StackMode mode) noexcept
{
    switch (mode)
    {
        case StackMode::octaves:    return 1200.0;
        case StackMode::fifths:     return  700.0;
        case StackMode::tritones:   return  600.0;
        case StackMode::cluster:    return  100.0;
        case StackMode::diminished: return  300.0;

        case StackMode::detune:
        case StackMode::scale:
        case StackMode::shepard:
        case StackMode::count:
        default:                    return 0.0;
    }
}

/// True for the modes whose offsets are a fixed interval times the rank.
[[nodiscard]] constexpr bool isFixedIntervalStack (StackMode mode) noexcept
{
    return mode == StackMode::octaves || mode == StackMode::fifths
        || mode == StackMode::tritones || mode == StackMode::cluster
        || mode == StackMode::diminished;
}

/// Copy `index`'s rank in a stack of `count`. Symmetric, and 0 for exactly one
/// copy at every count -- see the header.
[[nodiscard]] constexpr int stackRank (int index, int count) noexcept
{
    return index - (std::max (count, 1) - 1) / 2;
}

/// Fills a fixed-interval mode's offsets. Every copy sounds, so every gain is 1.
inline void fixedIntervalRanks (StackMode mode, int count,
                                double* cents, double* gains) noexcept
{
    const double interval = stackIntervalCents (mode);

    for (int i = 0; i < count; ++i)
    {
        cents[i] = interval * static_cast<double> (stackRank (i, count));
        gains[i] = 1.0;
    }
}

/// Fills Scale mode's offsets: copy at rank r plays the key `note + r * step`.
///
/// A key off the end of the keyboard, or one a keyboard map leaves unmapped,
/// gets gain 0 -- `Tuning::frequencyFor` returns 0 Hz for both and there is no
/// honest pitch to give that copy.
inline void scaleRanks (const dsp::Tuning& tuning, int note, int step, int count,
                        double* cents, double* gains) noexcept
{
    const double rootHz = tuning.frequencyFor (note);

    for (int i = 0; i < count; ++i)
    {
        cents[i] = 0.0;
        gains[i] = 1.0;

        const int rank = stackRank (i, count);

        if (rank == 0 || rootHz <= 0.0)
            continue;

        const double hz = tuning.frequencyFor (note + rank * std::max (step, 1));

        if (hz <= 0.0)
        {
            gains[i] = 0.0;
            continue;
        }

        cents[i] = 1200.0 * std::log2 (hz / rootHz);
    }
}

/// Everything above, dispatched. **Detune is the caller's business** -- it
/// wants the bank's own arithmetic and no arrays at all, so it is a no-op here.
///
/// `shepardOctaves` is the engine's accumulator in octaves travelled; see the
/// header for why it is divided by the copy count here and not there.
inline void stackRanks (StackMode mode, int count, int step, double shepardOctaves,
                        const dsp::Tuning& tuning, int note,
                        double* cents, double* gains) noexcept
{
    const int copies = std::clamp (count, 1, kMaxStackCopies);

    switch (mode)
    {
        case StackMode::scale:
            scaleRanks (tuning, note, step, copies, cents, gains);
            break;

        case StackMode::shepard:
            dsp::shepardRanks (shepardOctaves / static_cast<double> (copies),
                               copies, cents, gains);
            break;

        case StackMode::octaves:
        case StackMode::fifths:
        case StackMode::tritones:
        case StackMode::cluster:
        case StackMode::diminished:
            fixedIntervalRanks (mode, copies, cents, gains);
            break;

        // Named rather than defaulted, so a mode appended to the enum and
        // forgotten here stops the build -- the same guard the modulation
        // matrices use.
        case StackMode::detune:
        case StackMode::count:
        default:
            break;
    }
}

} // namespace tezla::sonitus
