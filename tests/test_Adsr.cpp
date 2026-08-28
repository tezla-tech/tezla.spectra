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
           double sustain = 0.5, double release = 0.1, double tension = 0.35)
{
    Adsr envelope;
    envelope.prepare (rate);
    envelope.setAttackSeconds (attack);
    envelope.setDecaySeconds (decay);
    envelope.setSustain (sustain);
    envelope.setReleaseSeconds (release);
    envelope.setAttackTension (tension);
    envelope.setDecayTension (tension);
    envelope.setReleaseTension (tension);
    return envelope;
}

/// Sets all three tensions at once, which is what most of these tests want.
void setTension (Adsr& envelope, double tension)
{
    envelope.setAttackTension (tension);
    envelope.setDecayTension (tension);
    envelope.setReleaseTension (tension);
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

TEZLA_TEST (the_tension_control_bends_the_curve_both_ways)
{
    // The other half of the timing claim: if the duration is held constant, the
    // tension has to actually be doing something. Measured at the half-way
    // point of a 100 ms attack, which is where a curve and a straight line are
    // furthest apart.
    //
    //     tension  overshoot   level at 50 ms
    //      -1.00        1.05        0.179
    //      -0.50        5.79        0.455
    //       0.00       32.00        0.504    <- a straight line would be 0.500
    //      +0.50        5.79        0.545
    //      +1.00        1.05        0.821
    //
    // **The old control could only produce the bottom half of that table's
    // right-hand column above 0.5.** No value of an overshoot bends a segment
    // the other way; the mirror is a separate branch and this is what it buys.
    constexpr double rate = 48000.0;

    const auto halfwayAt = [] (double tension)
    {
        auto envelope = made (rate, 0.1, 0.1, 0.5, 0.1, tension);
        envelope.noteOn();

        for (int i = 0; i < static_cast<int> (rate * 0.05); ++i)
            (void) envelope.process();

        return envelope.getLevel();
    };

    double previous = -1.0;

    for (const double tension : { -1.0, -0.5, 0.0, 0.5, 1.0 })
    {
        const double halfway = halfwayAt (tension);

        // Monotonic across the whole range, which a unipolar control cannot be
        // because it has no other side to be monotonic into.
        CHECK (halfway > previous);

        previous = halfway;
    }

    // Zero is straight to four decimal places. Not exactly straight -- that is
    // the limit of an infinite overshoot and degenerates the time expression --
    // but far closer than anything is going to hear or see.
    CHECK_NEAR (halfwayAt (0.0), 0.5, 0.01);

    // And the two ends are a long way either side of it.
    CHECK (halfwayAt (1.0) > 0.8);
    CHECK (halfwayAt (-1.0) < 0.2);
}

TEZLA_TEST (a_negative_tension_is_exactly_the_mirror_of_a_positive_one)
{
    // The claim the implementation rests on: receding from a target behind the
    // origin traces the same curve as approaching one past the destination,
    // reflected through the segment's centre. Stated as
    //
    //     level(u, -t)  ==  1 - level(1 - u, +t)
    //
    // and checked point by point rather than by eye, because "looks mirrored"
    // is exactly the kind of claim that survives being slightly wrong.
    constexpr double rate = 48000.0;
    constexpr double seconds = 0.1;

    const auto trace = [] (double tension)
    {
        auto envelope = made (rate, seconds, 0.1, 0.5, 0.1, tension);
        envelope.noteOn();

        std::vector<double> levels;

        for (int i = 0; i < static_cast<int> (rate * seconds); ++i)
            levels.push_back (envelope.process());

        return levels;
    };

    for (const double tension : { 0.25, 0.6, 1.0 })
    {
        const auto positive = trace (tension);
        const auto negative = trace (-tension);

        CHECK (positive.size() == negative.size());

        double worst = 0.0;

        for (std::size_t i = 0; i < positive.size(); ++i)
        {
            const std::size_t mirrored = positive.size() - 1 - i;

            worst = std::max (worst, std::abs (negative[i] - (1.0 - positive[mirrored])));
        }

        // One sample of grid offset is all the difference there is.
        CHECK (worst < 0.01);
    }
}

TEZLA_TEST (the_hold_stage_sits_at_full_level_for_the_time_it_was_given)
{
    // AHDSR. Without a hold, the only way to keep an envelope at the top for a
    // moment is to set the sustain to 1 and shorten the note, which is not the
    // same thing at all -- the release then starts from wherever the key was
    // let go rather than from the top.
    constexpr double rate = 48000.0;

    auto envelope = made (rate, 0.01, 0.2, 0.0, 0.1, 0.0);
    envelope.setHoldSeconds (0.05);

    envelope.noteOn();

    int atFullLevel = 0;
    int sawHold = 0;

    for (int i = 0; i < static_cast<int> (rate * 0.5); ++i)
    {
        const double level = envelope.process();

        if (envelope.getStage() == AdsrStage::hold)
        {
            ++sawHold;

            // Exactly 1.0, not nearly: the hold is a hold, not a very slow
            // decay.
            CHECK (level == 1.0);
        }

        if (level >= 1.0)
            ++atFullLevel;
    }

    // 50 ms at 48 kHz, within a sample of the attack's own arrival.
    CHECK_NEAR (sawHold, 2400, 2);
    CHECK (atFullLevel >= sawHold);

    // With no hold the stage is skipped entirely rather than entered for zero
    // samples -- otherwise a zero hold would still cost a branch and a stage
    // transition on every note.
    auto none = made (rate, 0.01, 0.2, 0.0, 0.1, 0.0);
    none.setHoldSeconds (0.0);
    none.noteOn();

    bool everHeld = false;

    for (int i = 0; i < static_cast<int> (rate * 0.1); ++i)
    {
        (void) none.process();

        if (none.getStage() == AdsrStage::hold)
            everHeld = true;
    }

    CHECK (! everHeld);
}

TEZLA_TEST (a_hold_can_be_lengthened_or_cut_short_while_it_runs)
{
    // The rule every other setter here follows: change a running stage and it
    // bends, it does not restart. The hold counts elapsed samples rather than
    // counting down a remaining time, which is what makes that true for free --
    // a hold shortened past where it already is ends at once, and one
    // lengthened simply goes on.
    constexpr double rate = 48000.0;

    auto envelope = made (rate, 0.001, 0.2, 0.0, 0.1, 0.0);
    envelope.setHoldSeconds (1.0);
    envelope.noteOn();

    // Well into the hold.
    for (int i = 0; i < static_cast<int> (rate * 0.1); ++i)
        (void) envelope.process();

    CHECK (envelope.getStage() == AdsrStage::hold);

    // Cut it to less than has already elapsed: it should end on the next
    // sample rather than run for another 50 ms.
    envelope.setHoldSeconds (0.05);

    // The sample the hold ends on is still at full level -- the transition
    // happens at its end, and the decay's first step is the sample after. What
    // matters is that it happened now rather than 50 ms from now.
    CHECK_NEAR (envelope.process(), 1.0, 1.0e-12);
    CHECK (envelope.getStage() == AdsrStage::decay);

    CHECK (envelope.process() < 1.0);
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

        setTension (envelope, 1.0);
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

TEZLA_TEST (the_tension_maps_geometrically_so_the_control_is_even)
{
    // Linear in the overshoot factor would spend most of its travel between
    // "straight" and "slightly straighter", because everything interesting
    // happens below 2.
    double previousRatio = 0.0;
    double previous = 0.0;

    for (const double tension : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        const double overshoot = Adsr::overshootFor (tension);

        if (previous > 0.0)
        {
            const double ratio = overshoot / previous;

            if (previousRatio > 0.0)
                CHECK_NEAR (ratio, previousRatio, 1.0e-9);

            previousRatio = ratio;
        }

        previous = overshoot;
    }

    // Zero is the straight end and either extreme is the sharpest -- the
    // control is bipolar, so its *magnitude* sets the curvature and its sign
    // sets which way the curve bends.
    CHECK_NEAR (Adsr::overshootFor (0.0), Adsr::kStraightestOvershoot, 1.0e-12);
    CHECK_NEAR (Adsr::overshootFor (1.0), Adsr::kSharpestOvershoot, 1.0e-12);
    CHECK_NEAR (Adsr::overshootFor (-1.0), Adsr::kSharpestOvershoot, 1.0e-12);
}

// ---------------------------------------------------------------------------
// A control-rate caller re-applies every setting every chunk
// ---------------------------------------------------------------------------

namespace
{
/// What a synth voice does every control chunk: push all eight settings at the
/// envelope whether they changed or not. The setters must treat an unchanged
/// value as the no-op it is -- CLAUDE.md section 7's general rule, in its
/// fourth appearance -- because re-aiming a running release from the current
/// level restarts the curve at its own steep end, and the finite-time exit
/// becomes a geometric crawl that never gets there.
void reapply (Adsr& envelope)
{
    envelope.setAttackSeconds (envelope.getAttackSeconds());
    envelope.setHoldSeconds (envelope.getHoldSeconds());
    envelope.setDecaySeconds (envelope.getDecaySeconds());
    envelope.setSustain (envelope.getSustain());
    envelope.setReleaseSeconds (envelope.getReleaseSeconds());
    envelope.setAttackTension (envelope.getAttackTension());
    envelope.setDecayTension (envelope.getDecayTension());
    envelope.setReleaseTension (envelope.getReleaseTension());
}
} // namespace

TEZLA_TEST (a_release_is_not_stretched_by_reapplied_settings)
{
    // The failure this pins down reached the user as a CPU meter, not as a
    // sound: with the setters re-aiming from the current level every 32
    // samples, the release's remaining level shrinks by a fixed ratio
    // (1 - T*eps) per chunk instead of following one aimed-past-zero curve,
    // and crossing the -100 dB floor takes ln(1e5) / (T * ln(T/(T-1))) times
    // the stated release -- about 11x at every tension. Voices retired 11x
    // slower than chords arrived, so a played passage pinned the meter at
    // full price and stayed there long after every key was up.
    const double rate = 48000.0;
    const double release = 0.5;
    const int chunk = 32;

    auto envelope = made (rate, 0.0, 0.0, 1.0, release);

    envelope.noteOn();
    (void) envelope.skip (8);
    CHECK (envelope.getStage() == AdsrStage::sustain);

    envelope.noteOff();

    int taken = 0;
    const int limit = static_cast<int> (4.0 * release * rate);

    while (envelope.isActive() && taken < limit)
    {
        reapply (envelope);
        (void) envelope.skip (chunk);
        taken += chunk;
    }

    CHECK (! envelope.isActive());
    CHECK_NEAR (taken / rate, release, 0.01 + 2.0 * chunk / rate);
}

TEZLA_TEST (a_decay_is_not_stretched_by_reapplied_settings)
{
    // Same mechanism, other segment. A decay re-aimed from the current level
    // never arrives at all: each re-aim targets past the sustain from where
    // it now is, so the remaining distance shrinks geometrically and the
    // crossing that ends the stage recedes forever -- it fires only when the
    // arithmetic finally rounds the difference away, minutes later.
    // Musically, this was every preset's decay running several times slower
    // than its knob said, since the day the instrument first made a sound.
    const double rate = 48000.0;
    const double decay = 0.2;
    const int chunk = 32;

    auto envelope = made (rate, 0.0, decay, 0.4, 0.1);

    envelope.noteOn();
    (void) envelope.process();
    CHECK (envelope.getStage() == AdsrStage::decay);

    int taken = 0;
    const int limit = static_cast<int> (4.0 * decay * rate);

    while (envelope.getStage() == AdsrStage::decay && taken < limit)
    {
        reapply (envelope);
        (void) envelope.skip (chunk);
        taken += chunk;
    }

    CHECK (envelope.getStage() == AdsrStage::sustain);
    CHECK_NEAR (taken / rate, decay, 0.01 + 2.0 * chunk / rate);
}

TEZLA_TEST (a_release_whose_settings_move_mid_flight_still_ends_on_time)
{
    // The no-op guard covers the unchanged case; this is the hard one. A
    // caller that genuinely changes the release while it runs -- a knob
    // dragged during a tail -- must still get a release that ends, which is
    // why the release aims from the level it *started* at rather than from
    // wherever it currently is: the target stays put however often the
    // segment is re-aimed, so the exit stays finite under any rate of change.
    const double rate = 48000.0;
    const int chunk = 32;

    auto envelope = made (rate, 0.0, 0.0, 1.0, 0.4);

    envelope.noteOn();
    (void) envelope.skip (8);
    envelope.noteOff();

    int taken = 0;
    const int limit = static_cast<int> (4.0 * 0.5 * rate);
    bool wobble = false;

    while (envelope.isActive() && taken < limit)
    {
        envelope.setReleaseSeconds (wobble ? 0.4 : 0.5);
        wobble = ! wobble;
        (void) envelope.skip (chunk);
        taken += chunk;
    }

    // Somewhere between the two stated times, and certainly not the limit:
    // the two coefficients bracket the journey.
    CHECK (! envelope.isActive());
    CHECK (taken / rate <= 0.5 * 1.1);
    CHECK (taken / rate >= 0.4 * 0.9);
}
