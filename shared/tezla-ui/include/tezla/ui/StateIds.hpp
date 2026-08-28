// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Names for the things a plugin stores in its state tree that are *not*
// parameters.
//
// A parameter is automatable, lives in a preset, and has a frozen ID for the
// reasons CLAUDE.md section 8 spells out. Some state is none of those things --
// it is a preference about the panel rather than about the sound -- and it
// still has to be written down somewhere the whole suite spells the same way.
//
// No JUCE here on purpose: the processor writes these and the editor reads
// them, so the constants have to be reachable from the audio side without
// dragging a GUI header in behind them. Same rule as ModulationIds.hpp.

namespace tezla::ui::stateIds
{

/// Whether the panel shows its hover tooltips.
///
/// Not a parameter -- it changes nothing about the sound, and a player who
/// turned the tips off does not want the next preset turning them back on. It
/// rides in the state tree next to the A/B slots so it survives a session, and
/// one spelling across the suite means every plugin behaves the same way.
inline constexpr const char* tooltipsEnabled = "tooltipsEnabled";

} // namespace tezla::ui::stateIds
