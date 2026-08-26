#pragma once

// The editor's view of the modulation state: which slot points where, how deep,
// and which source is currently armed.
//
// It exists so the strip and the rings do not have to know about each other. A
// ring needs to answer "is anything pointed at me, and by how much" and to write
// a depth; the strip needs to arm a source and say how many slots are left. Both
// are the same three parameters per slot, so both go through here.
//
// Everything is read and written through the plugin's parameters rather than
// through the DSP object, for the reason modulation itself never writes to a
// parameter: the parameters *are* the state. The host automates them, a preset
// restores them and A/B swaps them, and a UI that kept its own copy would be
// wrong the moment any of those happened. The one thing read from the DSP side
// is what a source is doing *right now*, which is a display and nothing else.

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/Modulation.hpp>

#include "ModulationIds.hpp"
#include "Palette.hpp"

namespace tezla::ui
{

class ModulationView final : public juce::ChangeBroadcaster
{
public:
    /// `destinationParameterIds` is the plugin's destination table, in the order
    /// the `modDst` choice parameter lists them. It is passed in rather than
    /// discovered because the order is frozen forever -- a slot stores an index
    /// into it -- and that is the plugin's commitment to make, not this class's.
    ModulationView (juce::AudioProcessorValueTreeState& state,
                    dsp::Modulation& modulation,
                    const char* const* destinationParameterIds,
                    int numDestinations,
                    Palette palette);

    static constexpr int numSlots   = modIds::numSlots;
    static constexpr int numLfos    = modIds::numLfos;
    static constexpr int numSources = dsp::Modulation::kNumSources;   ///< including Off

    /// Source indices, matching dsp::Modulation::Source exactly so nothing has
    /// to be translated between the panel and the matrix.
    enum Source : int { none = 0, lfo1, lfo2, lfo3, level };

    // ---- arming -------------------------------------------------------------

    /// Which source the rings are currently assigning, or `none`.
    ///
    /// One at a time on purpose. Halo's Chebyshev page puts nine controls in
    /// seven columns; four concentric rings on a 100 px knob would be a texture
    /// rather than a reading.
    [[nodiscard]] int getArmedSource() const noexcept { return armed_; }
    void setArmedSource (int source);

    // ---- the destination table ---------------------------------------------

    [[nodiscard]] int getNumDestinations() const noexcept { return numDestinations_; }

    /// The destination index for a parameter, or -1 if it is not modulatable.
    [[nodiscard]] int destinationFor (juce::StringRef parameterId) const;

    /// The parameter a destination drives. Null only if the table names one the
    /// plugin does not have, which the plugin's own static_asserts rule out.
    [[nodiscard]] juce::RangedAudioParameter* parameterFor (int destination) const;

    /// Where a destination's knob is set, 0 to 1 -- the *base* value, before any
    /// modulation. Modulation is added in this space, so the rings draw in it.
    [[nodiscard]] double baseProportionFor (int destination) const;

    // ---- slots --------------------------------------------------------------

    /// The slot already carrying this source/destination pair, or -1.
    [[nodiscard]] int findSlot (int source, int destination) const;

    /// The same, allocating the first free slot if there is not one yet.
    /// Returns -1 when all eight are spoken for.
    [[nodiscard]] int allocateSlot (int source, int destination);

    /// Hands a slot back: depth to zero, source to Off.
    void freeSlot (int slot);

    [[nodiscard]] int getSlotsUsed() const;

    [[nodiscard]] int getSlotSource (int slot) const;
    [[nodiscard]] int getSlotDestination (int slot) const;
    [[nodiscard]] double getSlotDepth (int slot) const;

    /// A drag, reported to the host as one gesture rather than as several
    /// hundred unrelated jumps -- the same reason the spectrum's Focus drag
    /// brackets itself.
    void beginDepthGesture (int slot);
    void setSlotDepth (int slot, double depth);
    void endDepthGesture (int slot);

    // ---- what to draw -------------------------------------------------------

    /// How far modulation can move a destination, as a fraction of its travel:
    /// the sum of the absolute depths pointed at it. Zero when nothing is.
    [[nodiscard]] double totalDepthFor (int destination) const;

    /// The one source pointed at a destination, when there is exactly one --
    /// so a resting ring can be drawn in that source's colour and say who owns
    /// it without anything being armed. `none` when several do, or none does.
    [[nodiscard]] int soleSourceFor (int destination) const;

    /// What modulation is adding to a destination at this instant, read from the
    /// running matrix. Display only, and deliberately not synchronised: it is
    /// one double being read while the audio thread writes it, thirty times a
    /// second, to move a dot.
    [[nodiscard]] double liveOffsetFor (int destination) const;

    /// What a source is putting out right now, -1 to +1. Same caveat.
    [[nodiscard]] double liveSourceValue (int source) const;

    /// Where an LFO is in its cycle, 0 to 1. Zero for the level follower, which
    /// has no cycle to be anywhere in.
    [[nodiscard]] double liveSourcePhase (int source) const;

    /// The waveform an LFO is set to, read from its parameter rather than from
    /// the matrix -- the strip draws the shape whether or not anything is
    /// assigned, and with nothing assigned the matrix is not running.
    [[nodiscard]] dsp::Lfo::Wave waveOf (int source) const;

    // ---- identity -----------------------------------------------------------

    /// Each source's colour, used by its panel, its arm button and every ring it
    /// owns, so "which one is this" never needs reading.
    [[nodiscard]] static juce::Colour colourForSource (int source);
    [[nodiscard]] static juce::String nameForSource (int source);

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }
    [[nodiscard]] const Palette& getPalette() const noexcept { return palette_; }

private:
    [[nodiscard]] juce::RangedAudioParameter* slotParameter (const char* const* ids, int slot) const;
    void setParameter (juce::RangedAudioParameter* parameter, double value) const;
    void setParameterOnce (juce::RangedAudioParameter* parameter, double value) const;

    juce::AudioProcessorValueTreeState& state_;
    dsp::Modulation& modulation_;

    const char* const* destinationIds_;
    int numDestinations_;
    Palette palette_;

    int armed_ { none };
};

} // namespace tezla::ui
