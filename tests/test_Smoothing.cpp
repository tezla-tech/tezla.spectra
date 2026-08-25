#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/SmoothedValue.hpp>

using namespace tezla::dsp;

TEZLA_TEST (smoothed_value_reaches_target_in_about_five_time_constants)
{
    constexpr double fs  = 48000.0;
    constexpr double tau = 0.02;

    SmoothedValue<double> smoother;
    smoother.prepare (fs, tau);
    smoother.setCurrentAndTarget (0.0);
    smoother.setTarget (1.0);

    // After one time constant: 1 - 1/e = 0.632
    smoother.skip (static_cast<int> (tau * fs));
    CHECK_NEAR (smoother.getCurrent(), 0.632, 0.01);

    // After five: within a thousandth.
    smoother.skip (static_cast<int> (4.0 * tau * fs));
    CHECK (smoother.getCurrent() > 0.99);

    smoother.skip (static_cast<int> (fs));
    CHECK (! smoother.isSmoothing());
    CHECK_NEAR (smoother.getCurrent(), 1.0, 1.0e-12);
}

TEZLA_TEST (smoothed_value_never_overshoots)
{
    SmoothedValue<double> smoother;
    smoother.prepare (48000.0, 0.005);
    smoother.setCurrentAndTarget (0.0);
    smoother.setTarget (1.0);

    for (int i = 0; i < 48000; ++i)
        CHECK (smoother.next() <= 1.0);
}

TEZLA_TEST (smoothed_value_ramp_time_is_sample_rate_independent)
{
    // Same wall-clock time constant at every rate: an automation move must not
    // sound faster at 192 kHz than at 48 kHz.
    constexpr double tau = 0.02;

    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        SmoothedValue<double> smoother;
        smoother.prepare (fs, tau);
        smoother.setCurrentAndTarget (0.0);
        smoother.setTarget (1.0);
        smoother.skip (static_cast<int> (tau * fs));

        CHECK_NEAR (smoother.getCurrent(), 0.632, 0.005);
    }
}

TEZLA_TEST (dc_blocker_removes_offset_but_keeps_sub_bass)
{
    constexpr double fs = 48000.0;

    DcBlocker<double> blocker;
    blocker.prepare (fs, 10.0);

    // A constant input must decay to nothing.
    double out = 0.0;
    for (int i = 0; i < static_cast<int> (fs); ++i)
        out = blocker.process (1.0);
    CHECK (std::abs (out) < 1.0e-3);

    // A 40 Hz sub must survive essentially untouched -- this is the whole point
    // of insisting on a first-order blocker at a low corner.
    blocker.prepare (fs, 10.0);
    const double omega = 2.0 * std::numbers::pi * 40.0 / fs;
    double peak = 0.0;
    for (int i = 0; i < static_cast<int> (fs) * 2; ++i)
    {
        const double y = blocker.process (std::sin (omega * static_cast<double> (i)));
        if (i > static_cast<int> (fs))
            peak = std::max (peak, std::abs (y));
    }
    CHECK (peak > 0.96);   // better than -0.35 dB at 40 Hz
}

TEZLA_TEST (dc_blocker_corner_is_sample_rate_independent)
{
    for (const double fs : { 48000.0, 96000.0, 192000.0 })
    {
        DcBlocker<double> blocker;
        blocker.prepare (fs, 10.0);

        const double omega = 2.0 * std::numbers::pi * 10.0 / fs;
        double peak = 0.0;
        for (int i = 0; i < static_cast<int> (fs) * 2; ++i)
        {
            const double y = blocker.process (std::sin (omega * static_cast<double> (i)));
            if (static_cast<double> (i) > fs)
                peak = std::max (peak, std::abs (y));
        }

        // -3 dB at the stated corner, at every rate.
        CHECK_NEAR (peak, 0.7071, 0.02);
    }
}

TEZLA_TEST (decibel_conversions_round_trip)
{
    for (const double db : { -60.0, -24.0, -6.0, 0.0, 6.0, 12.0 })
        CHECK_NEAR (gainToDb (dbToGain (db)), db, 1.0e-9);

    CHECK_NEAR (dbToGain (0.0),   1.0,    1.0e-12);
    CHECK_NEAR (dbToGain (6.0),   1.9953, 1.0e-4);
    CHECK_NEAR (dbToGain (-6.0),  0.5012, 1.0e-4);

    // Below the floor is silence, not a very small number.
    CHECK (dbToGain (-120.0) == 0.0);
    CHECK_NEAR (gainToDb (0.0), -100.0, 1.0e-12);
}

TEZLA_TEST (snap_to_zero_kills_denormals_only)
{
    CHECK (snapToZero (1.0e-40) == 0.0);
    CHECK (snapToZero (-1.0e-40) == 0.0);
    CHECK (snapToZero (1.0e-20) == 1.0e-20);
    CHECK (snapToZero (0.5) == 0.5);
}

TEZLA_TEST (scoped_no_denormals_restores_the_fpu_mode)
{
    // isSupported() being false is a build that cannot flush denormals at all,
    // which CLAUDE.md section 2.2 does not permit -- so this is a hard failure,
    // deliberately, and not a reason to skip the rest of the test.
    //
    // It has already earned that: the guard handled x86 only, so on Apple
    // Silicon it compiled cleanly, ran happily, and did absolutely nothing.
    // Making the test skip itself there would have shipped every Mac a plugin
    // with no denormal protection and no way to notice.
    CHECK (ScopedNoDenormals::isSupported());

    // The host's code runs on this thread too and did not ask for flush-to-zero,
    // so the guard must put the control word back exactly as it found it.

    volatile double tiny = 1.0e-308;
    {
        const ScopedNoDenormals noDenormals;
        const double flushed = static_cast<double> (tiny) * 1.0e-10;
        CHECK (flushed == 0.0);
    }

    const double notFlushed = static_cast<double> (tiny) * 1.0e-10;
    CHECK (notFlushed != 0.0);
}
