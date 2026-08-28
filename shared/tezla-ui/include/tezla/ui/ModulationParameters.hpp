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

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/Divisions.hpp>
#include <tezla/dsp/Modulation.hpp>

#include "ModulationIds.hpp"

namespace tezla::ui::modulation
{

/// The division table lives in tezla-dsp now -- the engines need it and are
/// framework-free -- and these aliases keep every existing use of
/// `modulation::divisions` spelling exactly what it always spelt.
using dsp::Division;
using dsp::divisions;
using dsp::numDivisions;
using dsp::defaultDivision;

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

/// One LFO, as a factory preset sets it.
struct LfoPreset
{
    int    wave     { 0 };                  ///< dsp::Lfo::Wave
    double rateHz   { 1.0 };
    bool   sync     { false };
    int    division { defaultDivision };
    double phase    { 0.0 };
    double smooth   { 0.0 };
};

/// One assignment. `source` is 0 for an unused slot, matching
/// dsp::Modulation::Source.
struct SlotPreset
{
    int    source      { 0 };
    int    destination { 0 };
    double depth       { 0.0 };
};

/// The modulation half of a factory preset.
///
/// A default-constructed one is "no modulation at all", and applying it is how
/// every preset that predates this feature stops leaving the last patch's
/// assignments behind. A preset is a complete parameter set or it is a trap.
struct Settings
{
    std::array<LfoPreset, dsp::Modulation::kNumLfos>   lfos {};
    std::array<SlotPreset, dsp::Modulation::kNumSlots> slots {};

    double envAttackMs      { 10.0 };
    double envReleaseMs     { 150.0 };
    double envSensitivityDb { -12.0 };
};

/// Writes every modulation parameter from a preset -- including the ones it
/// leaves neutral, which is the point.
///
/// Through the parameters rather than into the matrix, so the host sees the
/// change, the editor follows and undo works.
void applyPreset (juce::AudioProcessorValueTreeState& state, const Settings& settings);

/// Pushes every LFO, follower and slot setting into the matrix. Once per block:
/// these are controls, not audio.
///
/// `numDestinations` clamps a slot's target, because a destination index is a
/// choice index and a stale project could hold anything.
void pushSettings (juce::AudioProcessorValueTreeState& state,
                   dsp::Modulation& modulation,
                   int numDestinations);

} // namespace tezla::ui::modulation
