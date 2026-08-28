// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include <tezla/ui/AbCompare.hpp>

namespace tezla::ui
{

namespace
{
constexpr auto kRootType   = "abCompare";
constexpr auto kSlotType   = "slot";
constexpr auto kActiveProp = "active";
constexpr auto kIndexProp  = "index";
} // namespace

AbCompare::AbCompare (juce::AudioProcessorValueTreeState& state,
                      juce::StringArray excludedParameterIds)
    : state_ (state), excluded_ (std::move (excludedParameterIds))
{
}

juce::ValueTree AbCompare::captureCurrent() const
{
    juce::ValueTree snapshot { kSlotType };

    // Normalised values, not real ones. A normalised value means the same thing
    // whatever the parameter's range, so a range that is widened in a later
    // version restores an old snapshot to the same *position* rather than to a
    // number that now means something else.
    for (auto* parameter : state_.processor.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            if (excluded_.contains (withId->paramID))
                continue;

            snapshot.setProperty (juce::Identifier (withId->paramID),
                                  withId->getValue(), nullptr);
        }
    }

    return snapshot;
}

void AbCompare::applySnapshot (const juce::ValueTree& snapshot)
{
    if (! snapshot.isValid())
        return;

    for (auto* parameter : state_.processor.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            if (excluded_.contains (withId->paramID))
                continue;

            const juce::Identifier id { withId->paramID };
            if (! snapshot.hasProperty (id))
                continue;

            // Through the host, not around it: setValueNotifyingHost is what
            // keeps an automation lane, a control surface and the editor in
            // agreement about where the knob now is.
            withId->setValueNotifyingHost (static_cast<float> (snapshot.getProperty (id)));
        }
    }
}

bool AbCompare::otherSlotFilled() const noexcept
{
    return snapshots_[activeSlot_ ^ 1].isValid();
}

void AbCompare::swapSlots()
{
    snapshots_[activeSlot_] = captureCurrent();

    const int other = activeSlot_ ^ 1;

    // An empty other slot is copied into rather than applied from. Swapping to
    // a slot that has never been used would otherwise reset every control to
    // its default and lose the sound being worked on, which is the opposite of
    // what the button is for.
    if (! snapshots_[other].isValid())
        snapshots_[other] = snapshots_[activeSlot_];
    else
        applySnapshot (snapshots_[other]);

    activeSlot_ = other;

    if (onChanged)
        onChanged();
}

void AbCompare::copyToOtherSlot()
{
    snapshots_[activeSlot_ ^ 1] = captureCurrent();

    if (onChanged)
        onChanged();
}

juce::ValueTree AbCompare::toValueTree() const
{
    juce::ValueTree root { kRootType };
    root.setProperty (kActiveProp, activeSlot_, nullptr);

    for (int slot = 0; slot < 2; ++slot)
    {
        if (! snapshots_[slot].isValid())
            continue;

        auto copy = snapshots_[slot].createCopy();
        copy.setProperty (kIndexProp, slot, nullptr);
        root.appendChild (copy, nullptr);
    }

    return root;
}

void AbCompare::restoreFromValueTree (const juce::ValueTree& tree)
{
    snapshots_[0] = {};
    snapshots_[1] = {};
    activeSlot_ = 0;

    if (! tree.isValid() || ! tree.hasType (kRootType))
        return;

    activeSlot_ = juce::jlimit (0, 1, static_cast<int> (tree.getProperty (kActiveProp, 0)));

    for (const auto& child : tree)
    {
        const int slot = juce::jlimit (0, 1, static_cast<int> (child.getProperty (kIndexProp, 0)));
        snapshots_[slot] = child.createCopy();
    }

    if (onChanged)
        onChanged();
}

} // namespace tezla::ui
