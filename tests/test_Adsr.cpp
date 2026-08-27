#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <tezla/dsp/Adsr.hpp>

using namespace tezla::dsp;

namespace
{
/// Runs the envelope until it leaves `stage`, and returns how many samples that
/// took. Bounded, so a segment that never ends fails rather than hangs.
int samplesInStage (Adsr& envelope, AdsrStage stage, int limit)
{
    int count = 0;

    while (envelope.getStage() == stage && count < limit)
    {
        (void) envelope.process();
        ++count;
    }

    return count;
}

Adsr made (double rate = 48000.0, double attack = 0.1, double decay = 0.1,
           double sustain = 0.5, double release = 0.1, double shape = 0.35)
{
    Adsr envelope;
    envelope.prepare (rate);
    envelope.setAttackSeconds (attack);
    envelope.setDecaySeconds (decay);
    envelope.setSustain (sustain);
    envelope.setReleaseSeconds (release);
    envelope.setShape (shape);
    return envelope;
}
} // namespace

// ---------------------------------------------------------------------------
// The claim the shape control rests on
// ---------------------------------------------------------------------------

TEZLA_TEST (a_segment_lasts_the_time_it_was_asked_for_at_every_shape)
{
    // The whole point of deriving tau from ln(T/(T-1)) rather than using the
    // stated time as the time constant directly. Without it the shape control
    // is also a time control, and there is no way to have a curved attack and
    // a slow one at the same time.
    //
    // Break-checked by removing the scale -- using `tau = seconds` -- which
    // makes a 100 ms attack take 6 ms at shape 0 and 22 ms at shape 1.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const double shape : { 0.0, 0.25, 0.5, 0.75, 1.0 })
            for (const double seconds : { 0.005, 0.1, 1.0 })
            {
                auto envelope = made (rate, seconds, seconds, 0.0, seconds, shape);
                envelope.noteOn();

                const int attack = samplesInStage (envelope, AdsrStage::attack,
                                                   static_cast<int> (rate * 10.0));

                // Within a sample either way. The stage ends on the first
                // sample that crosses, so it can only ever be a fraction long.
                CHECK_NEAR (attack / rate, seconds, 1.5 / rate);

                // The decay travels 1 -> 0 here, which is the same distance
                // against the same fraction of the same curve.
                const int decay = samplesInStage (envelope, AdsrStage::decay,
                                                  static_cast<int> (rate * 10.0));

                CHECK_NEAR (decay / rate, seconds, 1.5 / rate);
            }
}

TEZLA_TEST (the_shape_control_changes_the_curve_and_only_the_curve)
{
    // The other half of the same claim: if the timing is held constant, the
    // shape has to actually be doing something. Measured at the half-way point
    // of a 100 ms attack, which is where an exponential and a straight line are
    // furthest apart.
    //
    //     shape   overshoot   level at 50 ms
    //      0.00        1.05        0.8276
    //      0.25        1.44        0.7136
    //      0.50        1.98        0.6438
    //      0.75        2.73        0.5974
    //      1.00        3.76        0.5651     <- 0.5 would be a straight line
    constexpr double rate = 48000.0;

    double previous = 1.0;

    for (const double shape : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        auto envelope = made (rate, 0.1, 0.1, 0.5, 0.1, shape);
        envelope.noteOn();

        for (int i = 0; i < static_cast<int> (rate * 0.05); ++i)
            (void) envelope.process();

        const double halfway = envelope.getLevel();

        // Curved means above the straight line, always -- a saturating
        // exponential is concave.
        CHECK (halfway > 0.5);

        // And straighter as the control rises.
        CHECK (halfway < previous);

        previous = halfway;
    }

    // A hard exponential is a long way from a straight line; a soft one is
    // close to it but never reaches it.
    auto sharpest = made (rate, 0.1, 0.1, 0.5, 0.1, 0.0);
    auto straightest = made (rate, 0.1, 0.1, 0.5, 0.1, 1.0);

    sharpest.noteOn();
    straightest.noteOn();

    for (int i = 0; i < static_cast<int> (rate * 0.05); ++i)
    {
        (void) sharpest.process();
        (void) straightest.process();
    }

    CHECK (sharpest.getLevel() - straightest.getLevel() > 0.2);
}

// ---------------------------------------------------------------------------
// It ends, and it ends exactly
// ---------------------------------------------------------------------------

TEZLA_TEST (the_release_reaches_exactly_zero_and_goes_idle)
{
    // A voice manager cannot free a voice whose envelope only approaches zero,
    // and a bus is not silent while it is still being fed 1e-40. Both are
    // properties of this test rather than of a gate somewhere downstream.
    for (const double rate : { 44100.0, 192000.0 })
        for (const double release : { 0.0, 0.001, 0.5 })
            for (const double shape : { 0.0, 1.0 })
            {
                auto envelope = made (rate, 0.001, 0.001, 0.8, release, shape);

                envelope.noteOn();

                for (int i = 0; i < static_cast<int> (rate * 0.05); ++i)
                    (void) envelope.process();

                CHECK (envelope.getStage() == AdsrStage::sustain);

                envelope.noteOff();

                const int taken = samplesInStage (envelope, AdsrStage::release,
                                                  static_cast<int> (rate * 10.0));

                CHECK (envelope.getStage() == AdsrStage::idle);
                CHECK (! envelope.isActive());

                // Exactly zero. Not nearly zero.
                CHECK (envelope.getLevel() == 0.0);
                CHECK (envelope.process() == 0.0);

                // And it took about as long as it said it would. The release
                // stops at -100 dB rather than at zero, so it finishes a hair
                // early -- ln(T/(T-1)) against ln(T/(T - 1 + 1e-5)).
                CHECK_NEAR (taken / rate, release, 0.002 + 2.0 / rate);
            }
}

TEZLA_TEST (an_idle_envelope_is_exactly_silent)
{
    auto envelope = made();

    for (int i = 0; i < 4096; ++i)
        CHECK (envelope.process() == 0.0);

    CHECK (! envelope.isActive());
}

TEZLA_TEST (a_zero_length_segment_takes_one_sample_rather_than_dividing_by_zero)
{
    // Zero is a legitimate setting -- it is what a click is made of, and a
    // percussive bass wants it. What it must not be is a NaN.
    //
    // Each zero-length segment costs one sample, because `process()` completes
    // one stage per call rather than cascading. With attack and decay both at
    // zero the envelope is at 1.0 after one sample and at the sustain after
    // two: 42 microseconds at 48 kHz, and a definition that does not need a
    // loop in the audio path to state.
    auto envelope = made (48000.0, 0.0, 0.0, 0.6, 0.0, 0.5);

    envelope.noteOn();

    const double first = envelope.process();

    CHECK (std::isfinite (first));
    CHECK (envelope.getStage() == AdsrStage::decay);
    CHECK_NEAR (first, 1.0, 1.0e-12);

    const double second = envelope.process();

    CHECK (envelope.getStage() == AdsrStage::sustain);
    CHECK_NEAR (second, 0.6, 1.0e-12);

    envelope.noteOff();

    const double afterRelease = envelope.process();

    CHECK (std::isfinite (afterRelease));
    CHECK (afterRelease == 0.0);
    CHECK (! envelope.isActive());
}

// ---------------------------------------------------------------------------
// Playing it
// ---------------------------------------------------------------------------

TEZLA_TEST (the_envelope_never_leaves_zero_to_one)
{
    // Every combination, sampled hard, including the settings that make a
    // segment finish inside one sample. The overshoot target is above 1 by
    // construction, so a missing clamp shows up here as a level of 3.76.
    double lowest = 1.0e9;
    double highest = -1.0e9;

    for (const double attack : { 0.0, 0.001, 0.05 })
        for (const double decay : { 0.0, 0.001, 0.05 })
            for (const double sustain : { 0.0, 0.5, 1.0 })
                for (const double release : { 0.0, 0.001, 0.05 })
                    for (const double shape : { 0.0, 0.5, 1.0 })
                    {
                        auto envelope = made (48000.0, attack, decay, sustain, release, shape);

                        envelope.noteOn();

                        for (int i = 0; i < 4800; ++i)
                        {
                            const double y = envelope.process();

                            CHECK (std::isfinite (y));
                            lowest = std::min (lowest, y);
                            highest = std::max (highest, y);

                            if (i == 2400)
                                envelope.noteOff();
                        }
                    }

    CHECK (lowest >= 0.0);
    CHECK (highest <= 1.0);
}

TEZLA_TEST (a_retrigger_starts_from_where_the_envelope_is)
{
    // The click-free requirement. A retrigger that jumps to zero first is a
    // discontinuity of the whole current level, which on a bass line at 0.8 is
    // a considerably louder event than the note it is trying to play.
    constexpr double rate = 48000.0;

    auto envelope = made (rate, 0.05, 0.05, 0.7, 0.05, 0.5);

    envelope.noteOn();

    for (int i = 0; i < static_cast<int> (rate * 0.2); ++i)
        (void) envelope.process();

    const double before = envelope.getLevel();

    CHECK_NEAR (before, 0.7, 1.0e-9);

    envelope.noteOn();

    const double after = envelope.process();

    // A single sample's worth of movement, not a jump to zero.
    CHECK (after > before);
    CHECK (after - before < 0.01);

    // And it climbs from there rather than restarting.
    (void) samplesInStage (envelope, AdsrStage::attack, static_cast<int> (rate));

    CHECK_NEAR (envelope.getLevel(), 1.0, 1.0e-12);
}

TEZLA_TEST (a_note_off_during_the_attack_releases_from_there)
{
    constexpr double rate = 48000.0;

    auto envelope = made (rate, 1.0, 0.05, 0.7, 0.1, 0.5);

    envelope.noteOn();

    for (int i = 0; i < static_cast<int> (rate * 0.1); ++i)
        (void) envelope.process();

    const double interrupted = envelope.getLevel();

    CHECK (interrupted > 0.05);
    CHECK (interrupted < 0.5);

    envelope.noteOff();

    CHECK (envelope.getStage() == AdsrStage::release);

    const double first = envelope.process();

    CHECK (first < interrupted);
    CHECK (interrupted - first < 0.01);

    const int taken = samplesInStage (envelope, AdsrStage::release, static_cast<int> (rate * 10.0));

    // The release time is the time to fall from *wherever it started*, which
    // is the same fraction of the same curve however high that was.
    CHECK_NEAR (taken / rate, 0.1, 0.002 + 2.0 / rate);
    CHECK (envelope.getLevel() == 0.0);
}

TEZLA_TEST (kill_silences_a_voice_immediately)
{
    auto envelope = made (48000.0, 0.001, 0.001, 0.9, 5.0);

    envelope.noteOn();

    for (int i = 0; i < 4800; ++i)
        (void) envelope.process();

    CHECK (envelope.getLevel() > 0.8);

    envelope.kill();

    CHECK (envelope.getLevel() == 0.0);
    CHECK (! envelope.isActive());
    CHECK (envelope.process() == 0.0);
}

// ---------------------------------------------------------------------------
// Changing a control while it is running
// ---------------------------------------------------------------------------

TEZLA_TEST (moving_a_control_bends_the_segment_rather_than_restarting_it)
{
    // CLAUDE.md section 7, the rule that came out of Emberdrive's DC corner:
    // a setter that resets state is never how a parameter change is applied.
    // These are all modulation destinations, so they move constantly.
    constexpr double rate = 48000.0;

    // Sustain, moved while the note is held.
    {
        auto envelope = made (rate, 0.001, 0.05, 0.8, 0.1, 0.5);

        envelope.noteOn();

        for (int i = 0; i < static_cast<int> (rate * 0.2); ++i)
            (void) envelope.process();

        CHECK_NEAR (envelope.getLevel(), 0.8, 1.0e-9);

        envelope.setSustain (0.3);

        // It has to get there, and it has to get there smoothly.
        double previous = envelope.getLevel();
        double biggestStep = 0.0;

        for (int i = 0; i < static_cast<int> (rate * 0.5); ++i)
        {
            const double y = envelope.process();

            biggestStep = std::max (biggestStep, std::abs (y - previous));
            previous = y;
        }

        CHECK_NEAR (envelope.getLevel(), 0.3, 1.0e-9);
        CHECK (biggestStep < 0.001);
    }

    // Shape and times, moved mid-attack. The level must not jump.
    {
        auto envelope = made (rate, 1.0, 0.5, 0.5, 0.5, 0.0);

        envelope.noteOn();

        for (int i = 0; i < static_cast<int> (rate * 0.1); ++i)
            (void) envelope.process();

        const double before = envelope.getLevel();

        envelope.setShape (1.0);
        envelope.setAttackSeconds (0.02);

        CHECK (envelope.getLevel() == before);
        CHECK (envelope.getStage() == AdsrStage::attack);

        const double after = envelope.process();

        CHECK (after > before);
        CHECK (after - before < 0.01);
    }

    // A sustain raised above where the decay has already fallen to. The decay
    // has to become a rise; snapping the level up to the new sustain is a
    // discontinuity of the whole difference, measured at 0.235 of full scale
    // before this was fixed.
    {
        auto envelope = made (rate, 0.001, 5.0, 0.0, 0.1, 0.5);

        envelope.noteOn();

        for (int i = 0; i < static_cast<int> (rate * 1.0); ++i)
            (void) envelope.process();

        CHECK (envelope.getStage() == AdsrStage::decay);

        const double reached = envelope.getLevel();

        CHECK (reached < 0.95);
        CHECK (reached > 0.5);

        envelope.setSustain (1.0);

        // Not a step.
        CHECK (envelope.getLevel() == reached);

        double previous = envelope.getLevel();
        double biggestStep = 0.0;

        for (int i = 0; i < static_cast<int> (rate * 6.0); ++i)
        {
            const double y = envelope.process();

            biggestStep = std::max (biggestStep, std::abs (y - previous));
            previous = y;
        }

        CHECK (biggestStep < 0.001);
        CHECK (envelope.getStage() == AdsrStage::sustain);
        CHECK_NEAR (envelope.getLevel(), 1.0, 1.0e-12);
    }
}

TEZLA_TEST (a_full_sustain_skips_the_decay_entirely)
{
    // Not a special case for its own sake: with sustain at 1 the decay has zero
    // distance to travel, and the target expression collapses to exactly 1, so
    // the stage would never end.
    auto envelope = made (48000.0, 0.001, 0.1, 1.0, 0.1, 0.5);

    envelope.noteOn();

    (void) samplesInStage (envelope, AdsrStage::attack, 48000);

    CHECK (envelope.getStage() == AdsrStage::sustain);
    CHECK_NEAR (envelope.getLevel(), 1.0, 1.0e-12);

    for (int i = 0; i < 48000; ++i)
        CHECK_NEAR (envelope.process(), 1.0, 1.0e-12);
}

TEZLA_TEST (the_shape_maps_geometrically_so_the_control_is_even)
{
    // Linear in the overshoot factor would spend most of its travel between
    // "straight" and "slightly straighter", because everything interesting
    // happens below 2.
    auto envelope = made();

    double previousRatio = 0.0;

    double previous = 0.0;

    for (const double shape : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        envelope.setShape (shape);

        const double overshoot = envelope.getOvershoot();

        if (previous > 0.0)
        {
            const double ratio = overshoot / previous;

            if (previousRatio > 0.0)
                CHECK_NEAR (ratio, previousRatio, 1.0e-9);

            previousRatio = ratio;
        }

        previous = overshoot;
    }

    envelope.setShape (0.0);
    CHECK_NEAR (envelope.getOvershoot(), Adsr::kSharpestOvershoot, 1.0e-12);

    envelope.setShape (1.0);
    CHECK_NEAR (envelope.getOvershoot(), Adsr::kStraightestOvershoot, 1.0e-12);
}
