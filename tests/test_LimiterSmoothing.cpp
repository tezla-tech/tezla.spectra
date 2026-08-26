#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <tezla/dsp/BoxStackSmoother.hpp>
#include <tezla/dsp/RunningMinimum.hpp>

using namespace tezla::dsp;

namespace
{
/// A deterministic pseudo-random source, so a failure is reproducible.
struct Random
{
    std::uint64_t state { 0x9e3779b97f4a7c15ULL };

    double next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<double> (state >> 11) * (1.0 / 9007199254740992.0);
    }
};

/// What the answer is, computed the slow obvious way.
double bruteForceMinimum (const std::vector<double>& values, int at, int window, double primed)
{
    double lowest = values[static_cast<std::size_t> (at)];

    for (int i = at; i > at - window; --i)
        lowest = std::min (lowest, i >= 0 ? values[static_cast<std::size_t> (i)] : primed);

    return lowest;
}
} // namespace

TEZLA_TEST (running_minimum_matches_a_brute_force_scan)
{
    Random random;
    std::vector<double> values (4000);

    for (auto& v : values)
        v = random.next();

    for (const int window : { 1, 2, 3, 17, 64, 257, 1000 })
    {
        RunningMinimum minimum;
        minimum.prepare (1024);
        minimum.setLength (window);
        minimum.reset (1.0);

        bool matched = true;

        for (int i = 0; i < static_cast<int> (values.size()); ++i)
        {
            const double got = minimum.process (values[static_cast<std::size_t> (i)]);
            const double expected = bruteForceMinimum (values, i, std::min (window, 1024), 1.0);

            if (std::abs (got - expected) > 0.0)
                matched = false;
        }

        CHECK (matched);
    }
}

TEZLA_TEST (running_minimum_survives_the_window_growing_mid_stream)
{
    // The case the retained history exists for, and the one that would break
    // the limiter silently. Growing the window brings samples back into range
    // that the deque had already thrown away from its front; without the ring
    // the minimum would be taken over a window narrower than it claims, which
    // is exactly the condition that lets a peak through.
    Random random;
    std::vector<double> values (3000);

    for (auto& v : values)
        v = random.next();

    RunningMinimum minimum;
    minimum.prepare (2048);
    minimum.setLength (8);
    minimum.reset (1.0);

    bool matched = true;
    int window = 8;

    for (int i = 0; i < static_cast<int> (values.size()); ++i)
    {
        // Grow, shrink, and grow again, at points that are not round numbers.
        if (i == 733)  { window = 1024; minimum.setLength (window); }
        if (i == 1471) { window = 32;   minimum.setLength (window); }
        if (i == 2099) { window = 700;  minimum.setLength (window); }

        const double got = minimum.process (values[static_cast<std::size_t> (i)]);
        const double expected = bruteForceMinimum (values, i, window, 1.0);

        if (std::abs (got - expected) > 0.0)
            matched = false;
    }

    CHECK (matched);
}

TEZLA_TEST (running_minimum_holds_a_constant_and_starts_at_unity)
{
    RunningMinimum minimum;
    minimum.prepare (256);
    minimum.setLength (128);
    minimum.reset (1.0);

    // Primed at unity, so a limiter does not fade in on every transport start.
    CHECK (minimum.get() == 1.0);

    for (int i = 0; i < 500; ++i)
        CHECK (minimum.process (0.75) <= 0.75 + 1.0e-15);

    CHECK (std::abs (minimum.get() - 0.75) < 1.0e-15);
}

TEZLA_TEST (box_stack_kernel_is_non_negative_and_sums_to_one)
{
    // The three properties the guarantee needs. An impulse response reads them
    // all directly: any negative tap, any shortfall in the sum, or any tap
    // outside the stated support would each break it in a different way.
    //
    // Non-negativity is exact, because the stages clamp there. The sum is
    // within a few ULP and not exact, because a recursive moving average
    // accumulates its sum rather than recomputing it -- measured at 4.4e-16 on
    // a 97-tap kernel. Asserting equality would be asserting that floating
    // point is something it is not.
    for (const int length : { 4, 5, 16, 97, 512 })
    {
        BoxStackSmoother smoother;
        smoother.prepare (1024);
        smoother.setLength (length);
        smoother.reset (0.0);

        double sum = 0.0;
        double mostNegative = 0.0;
        int lastNonZero = -1;

        // One sample of 1, then silence: the output is the kernel.
        for (int i = 0; i < 2048; ++i)
        {
            const double tap = smoother.process (i == 0 ? 1.0 : 0.0);

            sum += tap;
            mostNegative = std::min (mostNegative, tap);

            // Well above the rounding residue and far below the smallest real
            // tap: a 512-length kernel's outermost is about 6e-10.
            if (tap > 1.0e-15)
                lastNonZero = i;
        }

        CHECK (mostNegative >= 0.0);
        CHECK (std::abs (sum - 1.0) < 1.0e-14);
        CHECK (lastNonZero == length - 1);
    }
}

TEZLA_TEST (box_stack_step_response_never_overshoots)
{
    // A smoother that rings would climb above the minimum it is smoothing, and
    // the whole guarantee is that it does not. Non-negative taps make that
    // impossible, so this is really a test that the taps stay non-negative for
    // an input the impulse test does not exercise.
    BoxStackSmoother smoother;
    smoother.prepare (512);
    smoother.setLength (256);
    smoother.reset (1.0);

    double highest = 0.0;

    for (int i = 0; i < 4000; ++i)
        highest = std::max (highest, smoother.process (i < 2000 ? 0.25 : 1.0));

    // Three ULP, measured, from the accumulated sum -- not ringing, which would
    // be percent rather than 1e-16. The limiter clamps this away at the end.
    CHECK (highest <= 1.0 + 8.0 * 2.220446049250313e-16);
}

TEZLA_TEST (box_stack_latency_is_the_support_not_the_group_delay)
{
    // Reading the group delay instead -- half the support, which is what a
    // moving average's phase response says -- would put the gain curve half a
    // window early and let every peak through. The alignment is what the
    // guarantee is about, so it gets its own assertion.
    BoxStackSmoother smoother;
    smoother.prepare (1024);
    smoother.setLength (301);

    CHECK (smoother.getLatencySamples() == 300);
}

TEZLA_TEST (box_stack_holds_a_constant_exactly)
{
    BoxStackSmoother smoother;
    smoother.prepare (2048);
    smoother.setLength (1000);
    smoother.reset (1.0);

    double worst = 0.0;

    // Long enough to cross several resync intervals, so the drift correction
    // is exercised rather than assumed.
    for (int i = 0; i < 400000; ++i)
        worst = std::max (worst, std::abs (smoother.process (1.0) - 1.0));

    CHECK (worst < 1.0e-15);
}

TEZLA_TEST (minimum_then_smoothing_never_rises_above_the_delayed_input)
{
    // The property the limiter is built on, tested on the two stages alone
    // before any gain computer or ceiling exists. If this holds, the ceiling
    // holds; if it does not, nothing downstream can rescue it.
    //
    // The alignment is the whole content: with a smoother of support M the
    // output must be compared against the input M-1 samples ago, and the
    // minimum window must be at least M. Both are asserted by construction
    // below, and the two negative controls at the end show the assertion is
    // not vacuous.
    Random random;

    const auto worstExcess = [&random] (int smootherLength, int minimumWindow)
    {
        RunningMinimum minimum;
        BoxStackSmoother smoother;

        minimum.prepare (4096);
        smoother.prepare (4096);
        minimum.setLength (minimumWindow);
        smoother.setLength (smootherLength);
        minimum.reset (1.0);
        smoother.reset (1.0);

        const int latency = smoother.getLatencySamples();

        std::vector<double> history;
        history.reserve (60000);

        double excess = 0.0;

        for (int i = 0; i < 60000; ++i)
        {
            // A gain-shaped signal: mostly unity, with sudden deep dips of the
            // kind a transient produces, plus noise so nothing is periodic.
            double g = 1.0;

            if (i % 977 < 3)   g = 0.05;
            if (i % 4001 < 60) g = 0.3;

            g *= 0.9 + 0.1 * random.next();
            g = std::clamp (g, 0.0, 1.0);

            history.push_back (g);

            const double smoothed = smoother.process (minimum.process (g));

            if (i >= latency)
                excess = std::max (excess,
                                   smoothed - history[static_cast<std::size_t> (i - latency)]);
        }

        return excess;
    };

    // Correct: the minimum window is at least the smoother's support.
    //
    // A bound rather than zero. The cascade accumulates its running sums rather
    // than recomputing them, so the smoothed value lands about 1e-14 above the
    // sample it should stay under -- measured, and shown to be rounding rather
    // than misalignment in BoxStackSmoother's header. This bound is fifty times
    // that and still ten billion times below the smallest wrong answer at the
    // end of the test, so it catches a structural regression while not
    // asserting that floating point is exact.
    constexpr double slack = 1.0e-12;

    CHECK (worstExcess (256, 256) <= slack);
    CHECK (worstExcess (64, 400) <= slack);    // extra hold, which is free
    CHECK (worstExcess (7, 7) <= slack);       // and a degenerate short one

    // Wrong, and both must stay wrong or the test above proves nothing.
    CHECK (worstExcess (256, 64) > 0.01);      // minimum narrower than the kernel
    CHECK (worstExcess (256, 1) > 0.1);        // no minimum stage at all
}
