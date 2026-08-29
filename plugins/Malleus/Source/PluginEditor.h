// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// **A placeholder, and deliberately so.** M8 is the JUCE layer -- the
// parameters, the state, the tuning hand-off, the presets -- and this is
// the smallest editor that lets a host show all of it: JUCE's generic
// parameter view, which needs no layout decisions and cannot be wrong
// about them.
//
// M9 replaces this whole file with the house panel: the bone-ivory accent,
// the OBJECT / EXCITE / RESONANCE / TUNING tabs, the shared TuningPanel,
// and the mode-stack visualiser (the object's partials drawn as lines with
// the scale's degrees ghosted behind them as Overtone Lock rises -- the
// picture that explains what this instrument is). The processor already
// exposes what that needs: snapshotModeStack() and getScale().

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace tezla::malleus {

class MalleusEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MalleusEditor (MalleusProcessor& processor);
    ~MalleusEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    MalleusProcessor& processor_;
    juce::GenericAudioProcessorEditor parameters_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MalleusEditor)
};

} // namespace tezla::malleus
