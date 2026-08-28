// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cstdio>
#include <string>

#include <tezla/dsp/Version.hpp>

int main (int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : std::string {};

    std::printf ("tezla-dsp %s -- unit tests\n\n", tezla::dsp::kVersionString);

    return tezla::test::runAll (filter);
}
