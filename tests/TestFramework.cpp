// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace tezla::test {

namespace {
int  failuresInCurrentTest = 0;
} // namespace

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

Registrar::Registrar (std::string name, std::function<void()> body)
{
    registry().push_back ({ std::move (name), std::move (body) });
}

void reportFailure (const std::string& message, const char* file, int line)
{
    ++failuresInCurrentTest;
    std::printf ("      %s:%d\n        %s\n", file, line, message.c_str());
}

int runAll (const std::string& filter)
{
    int passed = 0;
    int failed = 0;

    for (auto& test : registry())
    {
        if (! filter.empty() && test.name.find (filter) == std::string::npos)
            continue;

        failuresInCurrentTest = 0;
        std::printf ("  %-48s", test.name.c_str());
        std::fflush (stdout);

        std::printf ("\n");
        test.body();

        if (failuresInCurrentTest == 0)
        {
            ++passed;
            std::printf ("      PASS\n");
        }
        else
        {
            ++failed;
            std::printf ("      FAIL (%d)\n", failuresInCurrentTest);
        }
    }

    std::printf ("\n  %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace tezla::test
