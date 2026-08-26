#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/LevelFollower.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

/// Feeds a constant magnitude for a number of milliseconds and returns the
/// detector's linear state -- the quantity the time constants act on, before
/// the sensitivity curve gets involved.
double feedConstant (LevelFollower& follower, double magnitude, double milliseconds)
{
    const auto samples = static_cast<int> (kRate * milliseconds * 0.001);
    std::vector<double> block (static_cast<std::size_t> (std::max (samples, 1)), magnitude);

    const double* pointers[1] { block.data() };
    (void) follower.process (pointers, 1, static_cast<int> (block.size()));

    return follower.getMagnitude();
}

LevelFollower made (double attackMs = 10.0, double releaseMs = 150.0,
                    double sensitivityDb = -6.0)
{
    LevelFollower follower;
    follower.prepare (kRate);
    follower.setAttackMs (attackMs);
    follower.setReleaseMs (releaseMs);
    follower.setSensitivityDb (sensitivityDb);
    return follower;
}
} // namespace

TEZLA_TEST (level_follower_silence_in_zero_out)
{
    // A modulation source that idles above zero moves every destination it is
    // assigned to on a silent track, which is both audible and baffling.
    auto follower = made();

    std::vector<double> silence (8192, 0.0);
    const double* pointers[1] { silence.data() };

    CHECK (follower.process (pointers, 1, 8192) == 0.0);
    CHECK (follower.getMagnitude() == 0.0);
}

TEZLA_TEST (level_follower_attack_reaches_one_over_e_at_the_stated_time)
{
    // The repository's convention is 1/e times, stated in tooltips. A follower
    // that means something else by "10 ms" makes every setting a guess.
    for (const double attackMs : { 1.0, 10.0, 50.0 })
    {
        auto follower = made (attackMs, 1000.0);

        const double atTau = feedConstant (follower, 1.0, attackMs);

        CHECK_NEAR (atTau, 1.0 - std::exp (-1.0), 0.01);
    }
}

TEZLA_TEST (level_follower_release_falls_at_the_stated_time)
{
    for (const double releaseMs : { 20.0, 150.0, 800.0 })
    {
        auto follower = made (0.1, releaseMs);

        // Charge it fully, then let it go.
        (void) feedConstant (follower, 1.0, releaseMs * 4.0);
        const double charged = follower.getMagnitude();
        CHECK (charged > 0.99);

        const double afterOneTau = feedConstant (follower, 0.0, releaseMs);

        CHECK_NEAR (afterOneTau, std::exp (-1.0), 0.01);
    }
}

TEZLA_TEST (level_follower_attack_and_release_are_not_the_same_control)
{
    // Fast up, slow down is the shape that makes a follower useful on
    // percussive material. If the two ever got wired to the same coefficient
    // this is what would notice.
    auto follower = made (1.0, 500.0);

    (void) feedConstant (follower, 1.0, 5.0);       // five attack constants
    const double risen = follower.getMagnitude();

    (void) feedConstant (follower, 0.0, 5.0);       // the same elapsed time
    const double fallen = follower.getMagnitude();

    CHECK (risen > 0.99);
    CHECK (fallen > 0.98);   // barely moved, because release is 500 ms
}

TEZLA_TEST (level_follower_sensitivity_sets_what_reads_as_full)
{
    // The control's promise: an input at the sensitivity level reads 1.0, and
    // one kRangeDb below it reads 0.
    for (const double sensitivityDb : { -20.0, -6.0, 0.0 })
    {
        auto follower = made (0.1, 10000.0, sensitivityDb);

        (void) feedConstant (follower, dbToGain (sensitivityDb), 5.0);
        CHECK_NEAR (follower.getValue(), 1.0, 0.02);

        auto quiet = made (0.1, 10000.0, sensitivityDb);
        (void) feedConstant (quiet, dbToGain (sensitivityDb - LevelFollower::kRangeDb * 0.5), 5.0);
        CHECK_NEAR (quiet.getValue(), 0.5, 0.02);

        auto silent = made (0.1, 10000.0, sensitivityDb);
        (void) feedConstant (silent, dbToGain (sensitivityDb - LevelFollower::kRangeDb - 6.0), 5.0);
        CHECK (silent.getValue() == 0.0);
    }
}

TEZLA_TEST (level_follower_maps_in_decibels_not_in_amplitude)
{
    // Halfway down the range in dB has to read about half. Mapping amplitude
    // linearly instead would put a signal 20 dB down at 0.1, which makes the
    // top three quarters of the control do nothing on real material.
    auto follower = made (0.1, 10000.0, 0.0);

    (void) feedConstant (follower, dbToGain (-20.0), 5.0);
    CHECK_NEAR (follower.getValue(), 0.5, 0.02);
}

TEZLA_TEST (level_follower_output_never_leaves_zero_to_one)
{
    // Depth scales this, so anything outside the range would push a destination
    // past its own limits and clamp -- which reads as the follower flattening
    // off rather than as a level fault.
    auto follower = made (1.0, 50.0, -12.0);

    for (const double magnitude : { 0.0, 1.0e-9, 0.001, 0.5, 1.0, 4.0, 100.0 })
    {
        const double value = feedConstant (follower, magnitude, 20.0);
        (void) value;

        CHECK (follower.getValue() >= 0.0);
        CHECK (follower.getValue() <= 1.0);
    }
}

TEZLA_TEST (level_follower_does_not_depend_on_the_host_block_size)
{
    // It runs per sample, so a 64-sample buffer and a 1024-sample one must
    // arrive at the same place. A detector sampled once a block would not, and
    // a 1 ms attack would mean nothing at a large buffer.
    const auto run = [] (int chunk, int chunks)
    {
        auto follower = made (5.0, 200.0);
        std::vector<double> block (static_cast<std::size_t> (chunk), 0.0);

        for (int i = 0; i < chunks; ++i)
        {
            for (int n = 0; n < chunk; ++n)
            {
                const auto t = static_cast<double> (i * chunk + n) / kRate;
                block[static_cast<std::size_t> (n)] = 0.6 * std::sin (2.0 * 3.14159265358979 * 90.0 * t);
            }

            const double* pointers[1] { block.data() };
            (void) follower.process (pointers, 1, chunk);
        }

        return follower.getMagnitude();
    };

    CHECK_NEAR (run (64, 128), run (1024, 8), 1.0e-9);
}

TEZLA_TEST (level_follower_is_stereo_linked)
{
    // An independent follower per channel makes a modulated stereo image
    // wander, which is the same argument CLAUDE.md section 7 makes about
    // per-channel nonlinearity. It follows the louder side.
    auto follower = made (0.1, 10000.0, 0.0);

    std::vector<double> loud (4096, 0.5);
    std::vector<double> quiet (4096, 0.001);

    const double* pointers[2] { loud.data(), quiet.data() };
    (void) follower.process (pointers, 2, 4096);

    CHECK_NEAR (follower.getMagnitude(), 0.5, 0.01);
}

TEZLA_TEST (level_follower_tracks_a_tone_without_rippling)
{
    // A rectified sine is not a constant, so a follower that is too fast rides
    // its shape and modulates at twice the tone frequency. On a 90 Hz bass note
    // with the default release, the ripple has to stay small enough not to be
    // heard on whatever it is driving.
    auto follower = made (10.0, 150.0);

    std::vector<double> block (256, 0.0);
    double lowest = 1.0;
    double highest = 0.0;

    for (int i = 0; i < 400; ++i)
    {
        for (int n = 0; n < 256; ++n)
        {
            const auto t = static_cast<double> (i * 256 + n) / kRate;
            block[static_cast<std::size_t> (n)] = 0.7 * std::sin (2.0 * 3.14159265358979 * 90.0 * t);
        }

        const double* pointers[1] { block.data() };
        (void) follower.process (pointers, 1, 256);

        if (i > 200)   // settled
        {
            lowest  = std::min (lowest, follower.getMagnitude());
            highest = std::max (highest, follower.getMagnitude());
        }
    }

    const double rippleDb = gainToDb (highest, -200.0) - gainToDb (lowest, -200.0);
    CHECK (rippleDb < 1.5);
}
