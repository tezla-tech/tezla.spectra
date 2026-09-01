// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cstdio>
#include <cstdlib>
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

bool cpuBudgetsAreMeasurable()
{
    // Read once. The environment cannot change under us mid-run, and a budget
    // test that answered differently in two places would be worse than either
    // answer -- see the header for why the caller is the one who knows.
    static const bool measurable = [] {
        const char* flag = std::getenv ("TEZLA_EMULATED");
        return flag == nullptr || flag[0] == '\0' || std::string (flag) == "0";
    }();

    return measurable;
}

void checkCpuBudget (double seconds, double budget, const std::string& what,
                     const char* file, int line)
{
    if (! cpuBudgetsAreMeasurable())
    {
        // Loud on purpose. A budget silently not checked is a budget nobody
        // knows is unchecked, which is the failure mode this whole mechanism
        // exists to avoid -- the number is still here to read.
        std::printf ("        [%s] %.4f s against a %.4f s budget"
                     " -- NOT ASSERTED, emulated run (TEZLA_EMULATED)\n",
                     what.c_str(), seconds, budget);
        return;
    }

    if (seconds < budget)
        return;

    reportFailure ("CPU budget exceeded: " + what + " took "
                       + std::to_string (seconds) + " s against a budget of "
                       + std::to_string (budget) + " s",
                   file, line);
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
