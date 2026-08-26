#include "TestFramework.hpp"

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

#include <tezla/dsp/BoxStackSmoother.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/RunningMinimum.hpp>

#include <cmath>

#include "EmberdriveEngine.hpp"
#include "HaloEngine.hpp"

using namespace tezla;

// CLAUDE.md 2.2: "No allocation, no locks, no file I/O, no logging, no
// exceptions in processBlock. Ever." Until now nothing checked it, and the rule
// had been broken quietly for a year -- setParameters() called prepare() when
// the oversampling factor changed, and prepare() allocated. It was never
// audible because the glitch is masked by the mode change itself, which is
// exactly how this class of bug survives.
//
// This replaces the global allocator for the test binary and counts. Counting
// is off except inside a scope that asks for it, so nothing else in the suite
// pays for it or trips it.

namespace
{
std::atomic<int>  gAllocations { 0 };
std::atomic<bool> gCounting    { false };

/// Counts allocations for as long as it is alive.
struct AllocationCounter
{
    AllocationCounter() noexcept
    {
        gAllocations.store (0, std::memory_order_relaxed);
        gCounting.store (true, std::memory_order_relaxed);
    }

    ~AllocationCounter() noexcept { gCounting.store (false, std::memory_order_relaxed); }

    [[nodiscard]] int count() const noexcept
    {
        return gAllocations.load (std::memory_order_relaxed);
    }
};

void* allocate (std::size_t size)
{
    if (gCounting.load (std::memory_order_relaxed))
        gAllocations.fetch_add (1, std::memory_order_relaxed);

    // Zero-sized allocations still have to return distinct pointers.
    void* pointer = std::malloc (size == 0 ? 1 : size);

    if (pointer == nullptr)
        throw std::bad_alloc();

    return pointer;
}
} // namespace

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}

void operator delete (void* pointer) noexcept { std::free (pointer); }
void operator delete[] (void* pointer) noexcept { std::free (pointer); }
void operator delete (void* pointer, std::size_t) noexcept { std::free (pointer); }
void operator delete[] (void* pointer, std::size_t) noexcept { std::free (pointer); }
void operator delete (void* pointer, const std::nothrow_t&) noexcept { std::free (pointer); }
void operator delete[] (void* pointer, const std::nothrow_t&) noexcept { std::free (pointer); }

TEZLA_TEST (allocation_counter_actually_counts)
{
    // The instrument before the measurement. A counter that never fires would
    // make every test below pass for a plugin that allocates constantly.
    AllocationCounter counter;

    std::vector<double> forced;
    forced.reserve (4096);

    CHECK (counter.count() > 0);
}

TEZLA_TEST (oversampler_changes_factor_without_allocating)
{
    dsp::Oversampler oversampler;
    oversampler.prepare (512, 2, 4);

    // Every factor, including the ones prepare() was not given -- that is the
    // point of building all three stages up front.
    AllocationCounter counter;

    for (const int factor : { 1, 2, 4, 8, 4, 2, 1, 8 })
        oversampler.setFactor (factor);

    CHECK (counter.count() == 0);
}

TEZLA_TEST (halo_changes_oversampling_without_allocating)
{
    // The path the bug was on: a parameter change from the audio thread.
    tezla::halo::Engine engine;
    engine.prepare (48000.0, 512, 2);

    auto parameters = tezla::halo::Parameters {};
    engine.setParameters (parameters);

    std::vector<double> left (512, 0.1), right (512, 0.1);
    double* pointers[2] { left.data(), right.data() };

    AllocationCounter counter;

    for (const auto mode : { dsp::OversamplingMode::X8, dsp::OversamplingMode::Off,
                             dsp::OversamplingMode::X2, dsp::OversamplingMode::X4,
                             dsp::OversamplingMode::Auto })
    {
        parameters.oversampling = mode;
        engine.setParameters (parameters);
        engine.process (pointers, 2, 512);
    }

    CHECK (counter.count() == 0);
}

TEZLA_TEST (emberdrive_changes_oversampling_without_allocating)
{
    tezla::emberdrive::Engine engine;
    engine.prepare (48000.0, 512, 2);

    auto parameters = tezla::emberdrive::Parameters {};
    engine.setParameters (parameters);

    std::vector<double> left (512, 0.1), right (512, 0.1);
    double* pointers[2] { left.data(), right.data() };

    AllocationCounter counter;

    for (const auto mode : { dsp::OversamplingMode::X8, dsp::OversamplingMode::Off,
                             dsp::OversamplingMode::X2, dsp::OversamplingMode::X4,
                             dsp::OversamplingMode::Auto })
    {
        parameters.oversampling = mode;
        engine.setParameters (parameters);
        engine.process (pointers, 2, 512);
    }

    CHECK (counter.count() == 0);
}

TEZLA_TEST (emberdrive_moves_the_dc_corner_without_allocating)
{
    // Worse than the factor, and the reason this had to be fixed before
    // anything could modulate: the expert DC corner is a *continuous* control,
    // so it can be automated -- and it used to drag the whole engine through
    // prepare() every time it moved.
    tezla::emberdrive::Engine engine;
    engine.prepare (48000.0, 512, 2);

    auto parameters = tezla::emberdrive::Parameters {};
    parameters.expert.enabled = true;
    engine.setParameters (parameters);

    std::vector<double> left (512, 0.1), right (512, 0.1);
    double* pointers[2] { left.data(), right.data() };

    AllocationCounter counter;

    for (int step = 0; step <= 40; ++step)
    {
        parameters.expert.dcBlockerHz = 2.0 + 0.9 * step;
        engine.setParameters (parameters);
        engine.process (pointers, 2, 512);
    }

    CHECK (counter.count() == 0);
}

TEZLA_TEST (neither_engine_allocates_while_processing)
{
    // The rule itself, on the ordinary path: parameters moving and audio
    // flowing, with no structural change at all.
    std::vector<double> left (256, 0.0), right (256, 0.0);
    double* pointers[2] { left.data(), right.data() };

    const auto fill = [&] (int block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const auto t = static_cast<double> (block * 256 + i);
            left[static_cast<std::size_t> (i)]  = 0.5 * std::sin (0.07 * t);
            right[static_cast<std::size_t> (i)] = 0.5 * std::sin (0.09 * t);
        }
    };

    {
        tezla::halo::Engine engine;
        engine.prepare (48000.0, 256, 2);

        auto parameters = tezla::halo::Parameters {};
        engine.setParameters (parameters);

        AllocationCounter counter;

        for (int block = 0; block < 50; ++block)
        {
            parameters.focusHz = 500.0 + 40.0 * block;
            parameters.drive   = 0.02 * block;
            parameters.generator = block % 10 < 5 ? tezla::halo::Generator::Curve
                                                  : tezla::halo::Generator::Chebyshev;
            engine.setParameters (parameters);

            fill (block);
            engine.process (pointers, 2, 256);
        }

        CHECK (counter.count() == 0);
    }

    {
        tezla::emberdrive::Engine engine;
        engine.prepare (48000.0, 256, 2);

        auto parameters = tezla::emberdrive::Parameters {};
        engine.setParameters (parameters);

        AllocationCounter counter;

        for (int block = 0; block < 50; ++block)
        {
            parameters.driveDb    = 0.4 * block;
            parameters.foldAmount = 0.02 * block;
            parameters.multiband  = block % 8 < 4;
            engine.setParameters (parameters);

            fill (block);
            engine.process (pointers, 2, 256);
        }

        CHECK (counter.count() == 0);
    }
}

TEZLA_TEST (limiter_smoothing_stages_do_not_allocate_while_running)
{
    // Both of these have their window set from a control -- attack and hold --
    // so setLength() is reachable from the audio thread, not only from
    // prepare(). RunningMinimum::setLength rebuilds its deque and
    // BoxStackSmoother::setLength rebuilds four running sums; both do that work
    // inside the capacity prepare() allocated, and this is what says so.
    dsp::RunningMinimum minimum;
    dsp::BoxStackSmoother smoother;

    minimum.prepare (4096);
    smoother.prepare (4096);

    AllocationCounter counter;

    for (const int length : { 64, 2048, 7, 4096, 300, 1 })
    {
        minimum.setLength (length + 128);
        smoother.setLength (length);

        for (int i = 0; i < 512; ++i)
            (void) smoother.process (minimum.process (i % 97 == 0 ? 0.2 : 1.0));
    }

    minimum.reset();
    smoother.reset();

    CHECK (counter.count() == 0);
}
