#pragma once

// Two complete settings, and a swap between them.
//
// The point of A/B is to hear one decision against another without having to
// remember where the knobs were, so a partial snapshot is worse than none: if
// half the controls move and half do not, the comparison is between two things
// neither of which you built. This captures every parameter the plugin has,
// with one deliberate exception -- bypass, which is a monitoring control rather
// than part of a sound, and would otherwise make swapping unmute you.
//
// Both slots live in the plugin's state, so they survive saving the project.

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

namespace tezla::ui
{

class AbCompare
{
public:
    /// `excludedParameterIds` are left out of both snapshots. Bypass belongs
    /// here; anything that makes a sound does not.
    AbCompare (juce::AudioProcessorValueTreeState& state,
               juce::StringArray excludedParameterIds);

    /// Stores the current settings in the live slot, then makes the other slot
    /// live and applies it. The first swap of a session copies across rather
    /// than jumping to defaults -- landing on an empty B would throw away the
    /// sound you had been working on, which is never what the button meant.
    void swapSlots();

    /// Copies the current settings into the slot that is not live, so there is
    /// something to swap to.
    void copyToOtherSlot();

    [[nodiscard]] bool isSlotB() const noexcept { return activeSlot_ == 1; }
    [[nodiscard]] bool otherSlotFilled() const noexcept;

    /// Saved with the plugin's state and restored from it.
    [[nodiscard]] juce::ValueTree toValueTree() const;
    void restoreFromValueTree (const juce::ValueTree& tree);

    /// Fired after a swap or a copy, so the editor can update its labels.
    std::function<void()> onChanged;

private:
    [[nodiscard]] juce::ValueTree captureCurrent() const;
    void applySnapshot (const juce::ValueTree& snapshot);

    juce::AudioProcessorValueTreeState& state_;
    juce::StringArray excluded_;

    juce::ValueTree snapshots_[2];
    int activeSlot_ { 0 };
};

} // namespace tezla::ui
