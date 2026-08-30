// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace tezla::syrinx
{

/// **Placeholder, replaced in V6.**
///
/// V5 is the JUCE layer -- parameters, state and presets -- and those are
/// worth landing on their own: they are what the validator checks, what a
/// saved project depends on, and what the tests in `test_SyrinxParameters.cpp`
/// pin. But a plugin needs *an* editor to load at all, so this is the smallest
/// honest one: every parameter, in order, with a slider each.
///
/// It is deliberately not a first draft of the real panel. V6 lays the strip
/// out **as the chain** -- the stages left to right in signal order, each with
/// its own gain-reduction meter -- and starting that here would mean building
/// it twice.
class SyrinxEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SyrinxEditor (SyrinxProcessor& processor);
    ~SyrinxEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SyrinxProcessor& processor_;
    juce::GenericAudioProcessorEditor generic_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyrinxEditor)
};

} // namespace tezla::syrinx
