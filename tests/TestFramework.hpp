#pragma once

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

} // namespace tezla::test

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
