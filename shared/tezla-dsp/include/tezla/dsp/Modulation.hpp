#pragma once

// Three LFOs, a level follower, and eight slots pointing them at things.
//
// Framework-free on purpose: the sources, the slots and the arithmetic are all
// testable without a plugin around them, and the JUCE layer above only has to
// convert between parameter units and the normalised space this works in.
//
// Two properties matter more than anything else here, and both are about the
// plugins that already exist:
//
//   isActive() is false until something is genuinely assigned, and the caller
//   skips its whole modulation path when it is. With nothing assigned both
//   plugins are byte-for-byte what they were before any of this existed, and
//   that is a test rather than an intention.
//
//   A depth of exactly 0 contributes exactly 0. `base + 0.0 * source` is `base`
//   to the bit, which is the same reason Halo's Width control is written as a
//   departure from unity rather than as a mid/side rebuild.
//
// Offsets are in **normalised** parameter space, 0 to 1, not in the
// destination's own units. One depth control then behaves musically on a skewed
// 40 Hz -- 12 kHz Focus range and on a linear Colour alike; in real units the
// same number would be a gentle wobble on one and a jump across the whole range
// on the other.

#include <algorithm>
#include <array>
#include <cstddef>

#include "LevelFollower.hpp"
#include "Lfo.hpp"

namespace tezla::dsp {

class Modulation
{
public:
    static constexpr int kNumLfos    = 3;
    static constexpr int kNumSlots   = 8;

    /// Enough for either plugin's continuous controls with room to grow.
    /// Fixed rather than sized at prepare, so nothing here ever allocates.
    static constexpr int kMaxDestinations = 64;

    enum class Source
    {
        off = 0,
        lfo1,
        lfo2,
        lfo3,
        level
    };

    static constexpr int kNumSources = 5;   ///< including off

    struct Slot
    {
        Source source      { Source::off };
        int    destination { 0 };
        double depth       { 0.0 };   ///< -1 .. +1, in normalised parameter space
    };

    void prepare (double sampleRate)
    {
        for (auto& lfo : lfos_)
            lfo.prepare (sampleRate);

        level_.prepare (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto& lfo : lfos_)
            lfo.reset();

        level_.reset();
        offsets_.fill (0.0);
    }

    [[nodiscard]] Lfo& lfo (int index) noexcept
    {
        return lfos_[static_cast<std::size_t> (std::clamp (index, 0, kNumLfos - 1))];
    }

    [[nodiscard]] const Lfo& lfo (int index) const noexcept
    {
        return lfos_[static_cast<std::size_t> (std::clamp (index, 0, kNumLfos - 1))];
    }

    [[nodiscard]] LevelFollower& levelFollower() noexcept { return level_; }
    [[nodiscard]] const LevelFollower& levelFollower() const noexcept { return level_; }

    /// Whether an LFO takes its phase from the host transport, and how many of
    /// its cycles fit in a beat: 4 for a sixteenth, 1 for a quarter, 0.25 for a
    /// 4/4 bar.
    void setLfoSync (int index, bool synced, double cyclesPerBeat) noexcept
    {
        const auto i = static_cast<std::size_t> (std::clamp (index, 0, kNumLfos - 1));
        synced_[i] = synced;
        cyclesPerBeat_[i] = cyclesPerBeat > 0.0 ? cyclesPerBeat : 1.0;
    }

    void setSlot (int index, Slot slot) noexcept
    {
        if (index < 0 || index >= kNumSlots)
            return;

        slot.destination = std::clamp (slot.destination, 0, kMaxDestinations - 1);
        slot.depth = std::clamp (slot.depth, -1.0, 1.0);

        slots_[static_cast<std::size_t> (index)] = slot;
        updateActive();
    }

    [[nodiscard]] Slot getSlot (int index) const noexcept
    {
        if (index < 0 || index >= kNumSlots)
            return {};

        return slots_[static_cast<std::size_t> (index)];
    }

    /// Whether anything is actually assigned.
    ///
    /// The caller's fast path: when this is false there is nothing to compute
    /// and nothing to apply, so the plugin runs exactly as it did before
    /// modulation existed -- one block, one parameter push, the same samples.
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// Advances every source by one chunk.
    ///
    /// `input` is the plugin's own input, which the level follower reads; pass
    /// nullptr if there is none. `hasTransport` and `ppqPosition` come from the
    /// host: without a transport the synced LFOs free-run at their last rate,
    /// which is what makes the standalone and a stopped session behave.
    void advance (int numSamples, const double* const* input, int numChannels,
                  bool hasTransport, double ppqPosition) noexcept
    {
        if (! active_)
            return;

        for (int i = 0; i < kNumLfos; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            if (synced_[index] && hasTransport)
                (void) lfos_[index].setPhaseFromPpq (ppqPosition, cyclesPerBeat_[index], numSamples);
            else
                (void) lfos_[index].advance (numSamples);
        }

        if (input != nullptr && numChannels > 0)
            (void) level_.process (input, numChannels, numSamples);

        offsets_.fill (0.0);

        for (const auto& slot : slots_)
        {
            if (slot.source == Source::off || slot.depth == 0.0)
                continue;

            offsets_[static_cast<std::size_t> (slot.destination)] += slot.depth * valueOf (slot.source);
        }
    }

    /// The summed normalised offset for one destination. Zero for anything
    /// nothing points at.
    [[nodiscard]] double offsetFor (int destination) const noexcept
    {
        if (destination < 0 || destination >= kMaxDestinations)
            return 0.0;

        return offsets_[static_cast<std::size_t> (destination)];
    }

    /// What a source is currently putting out, for a display. LFOs are bipolar,
    /// the level follower is not.
    [[nodiscard]] double valueOf (Source source) const noexcept
    {
        switch (source)
        {
            case Source::off:   return 0.0;
            case Source::lfo1:  return lfos_[0].getValue();
            case Source::lfo2:  return lfos_[1].getValue();
            case Source::lfo3:  return lfos_[2].getValue();
            case Source::level: return level_.getValue();
        }

        return 0.0;
    }

    /// Whether any slot points at a destination, whatever its depth. A knob's
    /// ring uses this to decide whether to draw at all.
    [[nodiscard]] bool isModulated (int destination) const noexcept
    {
        for (const auto& slot : slots_)
            if (slot.source != Source::off && slot.destination == destination && slot.depth != 0.0)
                return true;

        return false;
    }

private:
    void updateActive() noexcept
    {
        active_ = false;

        for (const auto& slot : slots_)
            if (slot.source != Source::off && slot.depth != 0.0)
            {
                active_ = true;
                return;
            }
    }

    std::array<Lfo, kNumLfos> lfos_ {};
    LevelFollower             level_ {};

    std::array<bool, kNumLfos>   synced_ {};
    std::array<double, kNumLfos> cyclesPerBeat_ { 1.0, 1.0, 1.0 };

    std::array<Slot, kNumSlots> slots_ {};
    std::array<double, kMaxDestinations> offsets_ {};

    bool active_ { false };
};

} // namespace tezla::dsp
