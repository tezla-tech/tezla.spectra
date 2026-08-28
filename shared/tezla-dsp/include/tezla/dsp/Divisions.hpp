// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Note divisions for anything tempo-synced, as cycles per beat.
//
// Moved here from tezla-ui's ModulationParameters.hpp verbatim -- same names,
// same values, same order -- because the engines are framework-free (CLAUDE.md
// section 4) and cannot include a header that declares JUCE types, and the
// Sonitus engine needs these to run a synced LFO and to snap envelope times.
// tezla-ui re-exports it, so Halo's and Emberdrive's stored division indices
// keep exactly the meaning they had.
//
// **Append-only, exactly like a parameter ID**: a choice parameter stores an
// index, so inserting a division silently retunes every synced LFO in every
// saved project that uses it.
//
// A 4/4 bar is four beats, so one cycle a bar is 0.25 cycles per beat. A
// triplet fits three in the space of two, hence 1.5x; a dotted note lasts one
// and a half times as long, hence 2/3.

#include <cstddef>
#include <iterator>

namespace tezla::dsp
{

struct Division
{
    const char* name;
    double cyclesPerBeat;
};

inline constexpr Division divisions[] {
    { "8 bars", 0.03125 }, { "4 bars", 0.0625 }, { "2 bars", 0.125 },
    { "1 bar",  0.25 },    { "1/2",    0.5 },    { "1/4",    1.0 },
    { "1/8",    2.0 },     { "1/16",   4.0 },    { "1/32",   8.0 },
    { "1/2 T",  0.75 },    { "1/4 T",  1.5 },    { "1/8 T",  3.0 },
    { "1/2 D",  1.0 / 3.0 }, { "1/4 D", 2.0 / 3.0 }, { "1/8 D", 4.0 / 3.0 }
};

inline constexpr int numDivisions = static_cast<int> (std::size (divisions));

/// Pinned at the move from tezla-ui: growing the table is fine, and anything
/// else is a retune of every project.
static_assert (numDivisions == 15, "the division table is append-only");

/// One cycle a bar, which is where a sweep usually wants to be.
inline constexpr int defaultDivision = 3;

/// How long one cycle of a division lasts. The answer for a synced envelope
/// stage; a synced LFO wants the reciprocal as a rate instead.
[[nodiscard]] constexpr double divisionSeconds (int index, double bpm) noexcept
{
    const double beats = bpm > 0.0 ? bpm : 120.0;
    const auto clamped = index < 0 ? 0 : (index >= numDivisions ? numDivisions - 1 : index);

    return (60.0 / beats) / divisions[static_cast<std::size_t> (clamped)].cyclesPerBeat;
}

/// A synced rate in Hz for a division at a tempo.
[[nodiscard]] constexpr double divisionRateHz (int index, double bpm) noexcept
{
    return 1.0 / divisionSeconds (index, bpm);
}

/// Snaps a time to the nearest note length at a tempo. For envelope stages.
///
/// Nearest in **log time**, because musical nearness is a ratio: 90 ms is
/// "about a 1/16" at 120 bpm (125 ms) rather than "about a 1/32" (62.5 ms),
/// even though the linear distances say otherwise.
///
/// Values below half the shortest division pass through unchanged -- an
/// instant attack must stay instant, and quantising 4 ms up to a 1/32 would
/// turn every pluck into a swell. Values past the longest division snap to
/// it, which is what snapping means.
[[nodiscard]] inline double snapSeconds (double seconds, double bpm) noexcept
{
    const double shortest = divisionSeconds (8, bpm);   // "1/32"

    if (! (seconds > 0.5 * shortest))
        return seconds;

    double bestSeconds = seconds;
    double bestDistance = 1.0e300;

    for (int index = 0; index < numDivisions; ++index)
    {
        const double candidate = divisionSeconds (index, bpm);
        const double distance = candidate > 0.0 && seconds > 0.0
            ? (candidate > seconds ? candidate / seconds : seconds / candidate)
            : 1.0e300;

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestSeconds = candidate;
        }
    }

    return bestSeconds;
}

} // namespace tezla::dsp
