// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <algorithm>

#include <limits>

#include <chrono>

// A deliberately tiny test framework. The point of tests/ is that it builds and
// runs with nothing installed -- no JUCE, no Catch2, no network -- so that DSP
// can be verified on any machine, in CI, and offline.

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace tezla::test {

struct TestCase
{
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();

struct Registrar
{
    Registrar (std::string name, std::function<void()> body);
};

/// Runs every registered test. Returns 0 when all pass.
int runAll (const std::string& filter = {});

/// Records a failure for the current test and keeps going, so one run reports
/// every problem rather than only the first.
void reportFailure (const std::string& message, const char* file, int line);

/// Whether a wall-clock CPU budget means anything on this run.
///
/// A budget is a claim about real hardware. Under an emulator it measures the
/// emulator, and by a large factor: the same five budget tests were measured
/// on this tree at 8.8x to 29.8x native under `qemu-aarch64` -- Ferrite 15.8%
/// of a core became 139.4%, the 64-mode resonator 0.37% became 11.02%. Every
/// one of them failed, and not one of them was a defect.
///
/// So the run is *told* it is emulated, by whoever launched it, through
/// `TEZLA_EMULATED=1`. The binary cannot work this out for itself -- a
/// cross-built AArch64 binary is identical whether it runs under qemu or on an
/// Apple Silicon machine -- and guessing from the architecture would be wrong
/// in exactly the case that matters, since real ARM64 hardware must assert.
///
/// **This is the instrument being declared invalid, not the requirement being
/// dropped**, which is the distinction CLAUDE.md section 10 draws. The work
/// still runs under emulation, so a crash, a hang or a NaN is still caught,
/// and `checkCpuBudget` still prints the measurement. Only the comparison is
/// withheld, and it says so in the output rather than passing quietly.
[[nodiscard]] bool cpuBudgetsAreMeasurable();

/// Assert `seconds < budget`, or -- on an emulated run -- report the figure
/// and say plainly that it was not asserted. `what` names the budget in the
/// output, e.g. "16 bowed voices".
void checkCpuBudget (double seconds, double budget, const std::string& what,
                     const char* file, int line);

/// Time `work` `runs` times and return the **fastest**.
///
/// ---------------------------------------------------------------------------
/// **The minimum, because contention only ever adds**
/// ---------------------------------------------------------------------------
///
/// A wall-clock CPU budget is a claim about how much work the code does. On a
/// shared machine a single timing measures the code *plus* whatever else the
/// box was doing, and those are not separable after the fact -- so a single
/// reading is an upper bound on the cost and a lower bound on nothing.
///
/// Measured here: Stryda's eight-voice budget reads **1.178 s** on a quiet
/// container and **1.803 s** with the cores busy, against a 1.600 s budget. It
/// failed two runs in five with nothing deliberately loading the machine, which
/// is worse than no test at all -- a check that is red two times in five
/// teaches you to ignore red, and the next real regression goes with it.
///
/// The minimum of a few runs is the standard answer and it keeps the guard's
/// teeth: contention can only make a run slower, so the fastest of three is the
/// closest estimate of the code's own cost, and a genuine regression -- which is
/// what these budgets exist to catch, and which is a factor rather than a few
/// per cent -- is slower on *every* run and still fails.
///
/// This does **not** paper over a slow build: the budget is unchanged and the
/// figure printed is the one asserted.
template <typename Work>
[[nodiscard]] double fastestOf (int runs, Work&& work)
{
    double best = std::numeric_limits<double>::max();

    for (int run = 0; run < std::max (1, runs); ++run)
    {
        const auto start = std::chrono::steady_clock::now();
        work();
        const auto elapsed = std::chrono::duration<double> (
                                 std::chrono::steady_clock::now() - start).count();

        best = std::min (best, elapsed);
    }

    return best;
}

} // namespace tezla::test

/// A CPU budget assertion. Use this rather than a bare CHECK on a duration:
/// see cpuBudgetsAreMeasurable() for why a raw comparison is a defect report
/// on an emulator and nothing else.
#define CHECK_CPU_BUDGET(seconds, budget, what)                                 \
    ::tezla::test::checkCpuBudget ((seconds), (budget), (what), __FILE__, __LINE__)

#define TEZLA_CONCAT_INNER(a, b) a##b
#define TEZLA_CONCAT(a, b) TEZLA_CONCAT_INNER(a, b)

#define TEZLA_TEST(testName)                                                    \
    static void testName();                                                     \
    static const ::tezla::test::Registrar TEZLA_CONCAT(tezlaRegistrar_, __LINE__) { #testName, testName }; \
    static void testName()

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (! (condition))                                                      \
            ::tezla::test::reportFailure ("CHECK failed: " #condition, __FILE__, __LINE__); \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                 \
    do {                                                                        \
        const double tezlaActual   = static_cast<double> (actual);              \
        const double tezlaExpected = static_cast<double> (expected);            \
        if (! (std::abs (tezlaActual - tezlaExpected) <= static_cast<double> (tolerance))) \
            ::tezla::test::reportFailure (                                      \
                std::string ("CHECK_NEAR failed: " #actual " = ") + std::to_string (tezlaActual) \
                    + ", expected " + std::to_string (tezlaExpected)            \
                    + " +/- " + std::to_string (static_cast<double> (tolerance)), \
                __FILE__, __LINE__);                                            \
    } while (false)
