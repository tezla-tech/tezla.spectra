#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/EnvelopeFollower.hpp>
#include <tezla/dsp/GainComputer.hpp>

using namespace tezla::dsp;

TEZLA_TEST (gain_computer_leaves_quiet_signal_alone)
{
    GainComputer computer;
    computer.setCeilingDb (-0.3);
    computer.setKneeDb (6.0);

    // The knee starts 6 dB below the ceiling, so anything quieter is untouched.
    for (const double levelDb : { -60.0, -30.0, -12.0, -6.31 })
        CHECK_NEAR (computer.computeGainReductionDb (levelDb), 0.0, 1.0e-12);
}

TEZLA_TEST (gain_computer_holds_the_ceiling_exactly)
{
    // Ceiling has to mean the level it says. A limiter whose output plateaus
    // half a knee below its own setting is a limiter nobody can trust.
    for (const double ceilingDb : { -12.0, -6.0, -0.3, 0.0 })
        for (const double kneeDb : { 0.0, 3.0, 6.0, 24.0 })
        {
            GainComputer computer;
            computer.setCeilingDb (ceilingDb);
            computer.setKneeDb (kneeDb);

            for (const double overshootDb : { 0.1, 3.0, 12.0, 40.0 })
            {
                const double inputDb  = ceilingDb + overshootDb;
                const double outputDb = inputDb + computer.computeGainReductionDb (inputDb);

                CHECK (outputDb <= ceilingDb + 1.0e-9);

                // Above the knee it should sit right on the ceiling, not below.
                if (overshootDb >= kneeDb)
                    CHECK_NEAR (outputDb, ceilingDb, 1.0e-9);
            }
        }
}

TEZLA_TEST (gain_computer_knee_is_smooth)
{
    // A kink in the curve is audible as a hardness right at the onset of
    // compression. Check the first derivative has no jumps across the knee.
    GainComputer computer;
    computer.setCeilingDb (0.0);
    computer.setKneeDb (12.0);

    constexpr double step = 1.0e-4;
    double previousSlope = 1.0;

    for (double levelDb = -24.0; levelDb <= 12.0; levelDb += 0.05)
    {
        const double below = levelDb - step + computer.computeGainReductionDb (levelDb - step);
        const double above = levelDb + step + computer.computeGainReductionDb (levelDb + step);
        const double slope = (above - below) / (2.0 * step);

        CHECK (slope >= -1.0e-6);          // never gains on the way up
        CHECK (slope <= 1.0 + 1.0e-6);     // never expands
        CHECK (std::abs (slope - previousSlope) < 0.02);

        previousSlope = slope;
    }
}

TEZLA_TEST (gain_computer_knee_control_moves_the_onset)
{
    // The Knee control claims to say how far below the ceiling compression
    // begins. Verify that literally.
    for (const double kneeDb : { 3.0, 6.0, 12.0, 24.0 })
    {
        GainComputer computer;
        computer.setCeilingDb (0.0);
        computer.setKneeDb (kneeDb);

        CHECK_NEAR (computer.computeGainReductionDb (-kneeDb - 0.01), 0.0, 1.0e-9);
        CHECK (computer.computeGainReductionDb (-kneeDb + 1.0) < 0.0);
    }
}

TEZLA_TEST (envelope_attack_and_release_hit_their_stated_times)
{
    constexpr double fs = 48000.0;

    EnvelopeFollower envelope;
    envelope.prepare (fs);
    envelope.setAttackMs (10.0);
    envelope.setReleaseMs (100.0);
    envelope.reset();

    // Attack: 1/e time constant, so ~63% of the way in 10 ms.
    const int attackSamples = static_cast<int> (0.010 * fs);
    for (int i = 0; i < attackSamples; ++i)
        (void) envelope.process (-12.0);

    CHECK_NEAR (envelope.getCurrentGainReductionDb(), -12.0 * 0.632, 0.15);

    // Settle, then release.
    for (int i = 0; i < static_cast<int> (fs); ++i)
        (void) envelope.process (-12.0);
    CHECK_NEAR (envelope.getCurrentGainReductionDb(), -12.0, 0.01);

    const int releaseSamples = static_cast<int> (0.100 * fs);
    for (int i = 0; i < releaseSamples; ++i)
        (void) envelope.process (0.0);

    CHECK_NEAR (envelope.getCurrentGainReductionDb(), -12.0 * (1.0 - 0.632), 0.15);
}

TEZLA_TEST (envelope_timing_is_sample_rate_independent)
{
    // A 10 ms attack must be 10 ms at 192 kHz too, or the plugin changes
    // character with the session rate.
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        EnvelopeFollower envelope;
        envelope.prepare (fs);
        envelope.setAttackMs (10.0);
        envelope.setReleaseMs (100.0);
        envelope.reset();

        for (int i = 0; i < static_cast<int> (0.010 * fs); ++i)
            (void) envelope.process (-12.0);

        CHECK_NEAR (envelope.getCurrentGainReductionDb(), -12.0 * 0.632, 0.1);
    }
}

TEZLA_TEST (envelope_never_overshoots_its_target)
{
    EnvelopeFollower envelope;
    envelope.prepare (48000.0);
    envelope.setAttackMs (1.0);
    envelope.setReleaseMs (50.0);
    envelope.reset();

    for (int i = 0; i < 48000; ++i)
    {
        const double target = (i / 512) % 2 == 0 ? -18.0 : 0.0;
        const double value = envelope.process (target);

        CHECK (value <= 1.0e-12);
        CHECK (value >= -18.0 - 1.0e-9);
    }
}

TEZLA_TEST (program_dependent_release_recovers_slower_after_sustained_reduction)
{
    constexpr double fs = 48000.0;

    const auto recoveryAfter = [fs] (bool programDependent, int heldSamples)
    {
        EnvelopeFollower envelope;
        envelope.prepare (fs);
        envelope.setAttackMs (1.0);
        envelope.setReleaseMs (100.0);
        envelope.setProgramDependent (programDependent);
        envelope.reset();

        for (int i = 0; i < heldSamples; ++i)
            (void) envelope.process (-12.0);

        for (int i = 0; i < static_cast<int> (0.100 * fs); ++i)
            (void) envelope.process (0.0);

        return envelope.getCurrentGainReductionDb();
    };

    // A brief peak: both modes should have let go substantially.
    const double shortFixed   = recoveryAfter (false, static_cast<int> (0.005 * fs));
    const double shortProgram = recoveryAfter (true,  static_cast<int> (0.005 * fs));
    CHECK (shortFixed > -6.0);
    CHECK (shortProgram > -6.0);

    // Sustained reduction: the program-dependent mode must still be holding on.
    const double longFixed   = recoveryAfter (false, static_cast<int> (2.0 * fs));
    const double longProgram = recoveryAfter (true,  static_cast<int> (2.0 * fs));
    CHECK (longProgram < longFixed - 2.0);
}

TEZLA_TEST (limiter_holds_a_sine_below_the_ceiling)
{
    // End to end through the static curve and the time constants: a steady tone
    // well over the ceiling must settle at the ceiling and stay there.
    constexpr double fs = 48000.0;
    constexpr double ceilingDb = -6.0;

    GainComputer computer;
    computer.setCeilingDb (ceilingDb);
    computer.setKneeDb (3.0);

    EnvelopeFollower envelope;
    envelope.prepare (fs);
    envelope.setAttackMs (1.0);
    envelope.setReleaseMs (50.0);
    envelope.reset();

    const double omega = 2.0 * std::numbers::pi * 200.0 / fs;
    const double inputGain = dbToGain (6.0);   // 12 dB over the ceiling

    double worstOutputDb = -200.0;

    for (int i = 0; i < static_cast<int> (fs); ++i)
    {
        const double input = inputGain * std::sin (omega * static_cast<double> (i));
        const double levelDb = gainToDb (std::abs (input));
        const double gainDb = envelope.process (computer.computeGainReductionDb (levelDb));
        const double output = input * dbToGain (gainDb);

        // Skip the attack, then look at the steady state.
        if (i > static_cast<int> (0.2 * fs))
            worstOutputDb = std::max (worstOutputDb, gainToDb (std::abs (output)));
    }

    // A feed-forward limiter with a finite attack tracks the envelope rather
    // than predicting it, so a little ripple above the ceiling is expected and
    // honest. Anything more than a dB would not be.
    CHECK (worstOutputDb <= ceilingDb + 1.0);
    CHECK (worstOutputDb >= ceilingDb - 2.0);
}
