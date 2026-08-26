#pragma once

// The forty-five modulation parameters, defined once.
//
// Both plugins declare the same slots, the same three LFOs and the same level
// follower, with the same ranges, defaults and formatting. Writing that twice
// would mean two tables of frozen identifiers to keep in step by eye, and a
// difference between them -- a range that stops at 16 Hz in one plugin and 20 in
// the other, a default division of a bar here and a half-bar there -- would look
// like a bug in the LFO rather than like a typo in a table.
//
// The destination list stays with each plugin. It is the one part that genuinely
// differs, and its order is that plugin's own permanent commitment; see
// CLAUDE.md section 8.

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/Modulation.hpp>

#include "ModulationIds.hpp"

namespace tezla::ui::modulation
{

/// Note divisions for a tempo-synced LFO, as cycles per beat.
///
/// **Append-only, exactly like a parameter ID**: a choice parameter stores an
/// index, so inserting a division silently retunes every synced LFO in every
/// saved project that uses it.
///
/// A 4/4 bar is four beats, so one cycle a bar is 0.25 cycles per beat. A
/// triplet fits three in the space of two, hence 1.5x; a dotted note lasts one
/// and a half times as long, hence 2/3.
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

/// One cycle a bar, which is where a sweep usually wants to be.
inline constexpr int defaultDivision = 3;

/// What a slot's source can be. Also append-only, and in the order
/// `dsp::Modulation::Source` declares.
[[nodiscard]] juce::StringArray sourceNames();

/// The seven waveforms, in the order `dsp::Lfo::Wave` declares.
[[nodiscard]] juce::StringArray waveNames();

[[nodiscard]] juce::StringArray divisionNames();

/// Appends every modulation parameter to a plugin's layout.
///
/// `schemaVersion` is the version hint these are introduced at -- the plugin's
/// own, since the two are on different numbers. `destinationNames` is the
/// plugin's destination list, in its frozen order.
void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                    int schemaVersion,
                    const juce::StringArray& destinationNames);

/// Pushes every LFO, follower and slot setting into the matrix. Once per block:
/// these are controls, not audio.
///
/// `numDestinations` clamps a slot's target, because a destination index is a
/// choice index and a stale project could hold anything.
void pushSettings (juce::AudioProcessorValueTreeState& state,
                   dsp::Modulation& modulation,
                   int numDestinations);

} // namespace tezla::ui::modulation
