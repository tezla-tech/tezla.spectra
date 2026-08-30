// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

namespace tezla::syrinx {

SyrinxEditor::SyrinxEditor (SyrinxProcessor& processor)
    : AudioProcessorEditor (&processor),
      processor_ (processor),
      generic_ (processor)
{
    addAndMakeVisible (generic_);

    setResizable (true, true);
    setResizeLimits (420, 400, 1400, 1600);
    setSize (560, 760);
}

void SyrinxEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161a));
}

void SyrinxEditor::resized()
{
    generic_.setBounds (getLocalBounds());
}

} // namespace tezla::syrinx
