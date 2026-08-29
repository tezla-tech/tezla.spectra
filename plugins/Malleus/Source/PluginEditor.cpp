// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <tezla/ui/Palette.hpp>

namespace tezla::malleus {

MalleusEditor::MalleusEditor (MalleusProcessor& processor)
    : juce::AudioProcessorEditor (&processor),
      processor_ (processor),
      parameters_ (processor)
{
    addAndMakeVisible (parameters_);

    setResizable (true, true);
    setResizeLimits (520, 400, 1600, 1200);
    setSize (620, 720);
}

void MalleusEditor::paint (juce::Graphics& g)
{
    // The bone-ivory accent M9 will build the panel around: the hammer
    // bone, against Anvil's steel.
    ui::Palette palette;
    palette.accent = juce::Colour (0xffe4dcc6);

    g.fillAll (palette.background);
}

void MalleusEditor::resized()
{
    parameters_.setBounds (getLocalBounds());
}

} // namespace tezla::malleus
