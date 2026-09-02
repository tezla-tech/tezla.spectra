// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// TensionDrop lives in shared/tezla-dsp now (Ictus I1): a kick drum strikes at
// raised tension and glides down exactly as a struck membrane does, so the
// same class serves both instruments. Malleus keeps its own name for it here
// so that nothing in this folder had to change to let it go.

#include <tezla/dsp/TensionDrop.hpp>

namespace tezla::malleus {

using dsp::TensionDrop;

} // namespace tezla::malleus
