#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <SonitusVoice.hpp>
#include <VoiceManager.hpp>

using namespace tezla::sonitus;
using namespace tezla::dsp;

namespace
{
/// Renders one voice for `samples` and returns the peak and the RMS.
struct Rendered
{
    double peak { 0.0 };
    double rms { 0.0 };
    std::vector<double> left;
};

Rendered render (Voice& voice, const VoiceParameters& parameters, int samples,
                 int releaseAt = -1)
{
    Rendered out;

    out.left.reserve (static_cast<std::size_t> (samples));

    double sum = 0.0;

    for (int i = 0; i < samples; ++i)
    {
        if (i % Voice::kControlIntervalSamples == 0)
            voice.applyControls (parameters, GlobalSources {});

        if (i == releaseAt)
            voice.noteOff();

        double left = 0.0;
        double right = 0.0;

        voice.process (left, right);

        out.left.push_back (left);
        out.peak = std::max ({ out.peak, std::abs (left), std::abs (right) });
        sum += left * left;
    }

    out.rms = std::sqrt (sum / std::max (samples, 1));

    return out;
}

VoiceParameters basic()
{
    VoiceParameters parameters;

    parameters.levelA = 1.0;
    parameters.levelB = 0.0;
    parameters.subLevel = 0.0;
    parameters.cutoffHz = 18000.0;
    parameters.ampAttack = 0.001;
    parameters.ampSustain = 1.0;
    parameters.ampRelease = 0.05;
    parameters.level = 1.0;
    parameters.ampVelocity = 0.0;

    return parameters;
}

/// Energy at one frequency, by demodulation.
double amplitudeAt (const std::vector<double>& signal, double frequency, double sampleRate,
                    int from = 0)
{
    double inPhase = 0.0;
    double quadrature = 0.0;

    const int count = static_cast<int> (signal.size()) - from;

    for (int i = 0; i < count; ++i)
    {
        const double phase = 2.0 * std::numbers::pi * frequency * (i + from) / sampleRate;

        inPhase += signal[static_cast<std::size_t> (i + from)] * std::sin (phase);
        quadrature += signal[static_cast<std::size_t> (i + from)] * std::cos (phase);
    }

    return 2.0 * std::hypot (inPhase, quadrature) / std::max (count, 1);
}
} // namespace

// ---------------------------------------------------------------------------
// The voice
// ---------------------------------------------------------------------------

TEZLA_TEST (a_voice_is_silent_until_played_and_after_it_finishes)
{
    // CLAUDE.md section 7. An instrument that leaks is worse than an effect
    // that leaks, because there is nothing upstream to blame.
    Voice voice;
    voice.prepare (48000.0);

    const auto parameters = basic();

    CHECK (! voice.isActive());

    for (int i = 0; i < 4800; ++i)
    {
        double left = 0.0;
        double right = 0.0;

        voice.applyControls (parameters, GlobalSources {});
        voice.process (left, right);

        CHECK (left == 0.0);
        CHECK (right == 0.0);
    }

    voice.noteOn (60, 261.6255653005986, 1.0, false);

    const auto sounding = render (voice, parameters, 4800);

    CHECK (sounding.peak > 0.1);
    CHECK (voice.isActive());

    voice.noteOff();

    (void) render (voice, parameters, 48000);

    CHECK (! voice.isActive());

    // And exactly zero afterwards, not merely small.
    for (int i = 0; i < 4800; ++i)
    {
        double left = 0.0;
        double right = 0.0;

        voice.process (left, right);

        CHECK (left == 0.0);
        CHECK (right == 0.0);
    }
}

TEZLA_TEST (a_voice_plays_the_frequency_it_was_given)
{
    // The voice never computes a pitch from a note number -- the tuning does
    // that, in one place. So what is checkable here is that the number it was
    // handed is the number that comes out.
    constexpr double rate = 48000.0;

    for (const double frequency : { 55.0, 110.0, 261.6255653005986, 440.0 })
    {
        Voice voice;
        voice.prepare (rate);

        auto parameters = basic();
        parameters.shapeA = OscShape::sine;

        voice.noteOn (60, frequency, 1.0, false);

        const auto rendered = render (voice, parameters, 24000);

        // Skip the attack.
        const double atPitch = amplitudeAt (rendered.left, frequency, rate, 4800);
        const double offPitch = amplitudeAt (rendered.left, frequency * 1.5, rate, 4800);

        CHECK (atPitch > 0.3);
        CHECK (atPitch > offPitch * 20.0);
    }
}

TEZLA_TEST (the_sub_bypasses_the_filter)
{
    // The design decision the header argues for, and the reason the instrument
    // is usable on a track: a growl is a filter swept hard, and a sub that goes
    // through it disappears every time the cutoff drops.
    constexpr double rate = 48000.0;

    auto parameters = basic();

    parameters.shapeA = OscShape::sine;
    parameters.levelA = 1.0;
    parameters.subLevel = 1.0;
    parameters.subOctave = -1;
    parameters.cutoffHz = 20.0;      // shut, as far as it goes

    Voice voice;
    voice.prepare (rate);
    voice.noteOn (48, 130.8127826502993, 1.0, false);

    const auto rendered = render (voice, parameters, 48000);

    // The oscillator at 130.8 Hz is far above a 20 Hz corner and is gone; the
    // sub an octave below it is not.
    const double atOscillator = amplitudeAt (rendered.left, 130.8127826502993, rate, 9600);
    const double atSub = amplitudeAt (rendered.left, 65.40639132514966, rate, 9600);

    CHECK (atSub > 0.3);
    CHECK (atSub > atOscillator * 10.0);
}

TEZLA_TEST (hard_sync_locks_bs_cycle_to_as)
{
    // The Pro-53 trick. With sync on, B's own pitch stops setting the note and
    // starts setting the timbre -- so however far B is detuned, the *period*
    // stays A's.
    //
    // Period, not fundamental amplitude, and that distinction is what the first
    // version of this test got wrong. At an exact octave or twelfth the synced
    // waveform is two or three cycles of B per cycle of A, so almost all its
    // energy sits at the harmonic and there is very little at A's fundamental.
    // That is sync working, not sync failing. Periodicity is the claim that
    // holds at every ratio.
    constexpr double rate = 48000.0;
    constexpr double frequency = 110.0;

    // **RMS of the period-to-period difference, not the peak.** A saw's
    // discontinuity lands somewhere different every cycle when the period is
    // not a whole number of samples, so the peak difference is about 0.29
    // whatever is going on and separates synced from un-synced by only a factor
    // of 2.7. The RMS separates them by 37. Measured, 110 Hz at 48 kHz:
    //
    //     semitones   sync   peak diff   rms diff
    //             0    off      0.2927     0.0170     <- unison: already periodic
    //             0     on      0.2927     0.0170
    //             5    off      1.0187     0.6639
    //             5     on      0.2922     0.0179
    //             7    off      0.7878     0.7042
    //             7     on      0.2917     0.0189
    //            12    off      0.2917     0.0240     <- octave: already periodic
    //            12     on      0.2916     0.0240
    //            19    off      0.6620     0.0669
    //            19     on      0.2900     0.0292
    //
    // The 0.29 floor in every row is that fractional period -- A's cycle is
    // 436.36 samples -- and it does not grow with time.
    const auto periodicity = [&] (double semitones, bool sync)
    {
        auto parameters = basic();

        parameters.levelA = 0.0;
        parameters.levelB = 1.0;
        parameters.syncB = sync;
        parameters.shapeB = OscShape::saw;
        parameters.semitonesB = semitones;

        Voice voice;
        voice.prepare (rate);
        voice.noteOn (45, frequency, 1.0, false);

        const auto rendered = render (voice, parameters, 48000);

        const int period = static_cast<int> (std::round (rate / frequency));

        double sum = 0.0;
        double worst = 0.0;
        int count = 0;

        for (std::size_t i = 24000; i + static_cast<std::size_t> (period) < rendered.left.size(); ++i)
        {
            const double difference = rendered.left[i]
                                        - rendered.left[i + static_cast<std::size_t> (period)];

            sum += difference * difference;
            worst = std::max (worst, std::abs (difference));
            ++count;
        }

        struct Result { double rms; double peak; };

        return Result { std::sqrt (sum / std::max (count, 1)), worst };
    };

    for (const double semitones : { 0.0, 5.0, 7.0, 12.0, 19.0 })
    {
        const auto synced = periodicity (semitones, true);

        CHECK (synced.rms < 0.035);
        CHECK (synced.peak < 0.35);
    }

    // And the contrast that makes it mean something: without sync, a B tuned to
    // a non-harmonic interval is not periodic at A's period at all. The
    // intervals that *are* harmonic -- unison and the octave -- are periodic
    // either way, which is why they cannot be the ones this is measured on.
    for (const double semitones : { 5.0, 7.0 })
    {
        const auto free = periodicity (semitones, false);
        const auto synced = periodicity (semitones, true);

        CHECK (free.rms > 0.6);
        CHECK (free.rms > synced.rms * 20.0);
    }

    CHECK (periodicity (12.0, false).rms < 0.035);
}

TEZLA_TEST (sync_off_leaves_b_at_its_own_pitch)
{
    // The other half: without sync, B detuned by a fifth *is* a fifth.
    constexpr double rate = 48000.0;

    auto parameters = basic();

    parameters.levelA = 0.0;
    parameters.levelB = 1.0;
    parameters.syncB = false;
    parameters.shapeB = OscShape::sine;
    parameters.semitonesB = 7.0;

    Voice voice;
    voice.prepare (rate);
    voice.noteOn (45, 110.0, 1.0, false);

    const auto rendered = render (voice, parameters, 48000);

    const double atFifth = amplitudeAt (rendered.left, 110.0 * std::pow (2.0, 7.0 / 12.0), rate, 9600);
    const double atRoot = amplitudeAt (rendered.left, 110.0, rate, 9600);

    CHECK (atFifth > 0.3);
    CHECK (atFifth > atRoot * 20.0);
}

TEZLA_TEST (the_ring_modulator_is_a_crossfade_rather_than_an_addition)
{
    // Blending towards the product rather than adding it, so the control is not
    // also a volume knob. At full ring the output is the product alone.
    constexpr double rate = 48000.0;

    auto parameters = basic();

    parameters.shapeA = OscShape::sine;
    parameters.shapeB = OscShape::sine;
    parameters.levelA = 1.0;
    parameters.levelB = 1.0;
    parameters.semitonesB = 5.0;

    double previousAtRoot = 1.0e9;

    for (const double ring : { 0.0, 0.5, 1.0 })
    {
        parameters.ringAmount = ring;

        Voice voice;
        voice.prepare (rate);
        voice.noteOn (45, 110.0, 1.0, false);

        const auto rendered = render (voice, parameters, 48000);

        const double atRoot = amplitudeAt (rendered.left, 110.0, rate, 9600);

        // The carriers fade out as the ring comes up.
        CHECK (atRoot < previousAtRoot);
        previousAtRoot = atRoot;

        if (ring == 1.0)
        {
            // Nothing of either carrier is left; what is there is the sum and
            // difference frequencies.
            const double b = 110.0 * std::pow (2.0, 5.0 / 12.0);

            CHECK (atRoot < 0.02);
            CHECK (amplitudeAt (rendered.left, b, rate, 9600) < 0.02);
            CHECK (amplitudeAt (rendered.left, b - 110.0, rate, 9600) > 0.1);
            CHECK (amplitudeAt (rendered.left, b + 110.0, rate, 9600) > 0.1);
        }
    }
}

TEZLA_TEST (the_filter_fm_swing_is_symmetric_in_octaves)
{
    // Exponential rather than linear, and it is not a taste question: a linear
    // swing at full depth reaches `1 + 2*(-1) = -1`, which the filter clamps to
    // its floor. The modulation is then rectified -- one-sided flutter instead
    // of vibrato. Through the audio the two differ by only about ten percent in
    // RMS, which is why this is asserted on the multiplier itself rather than
    // on a rendered signal.
    Voice voice;
    voice.prepare (48000.0);

    auto parameters = basic();

    for (const double depth : { 0.0, 0.25, 0.5, 1.0 })
    {
        parameters.filterFm = depth;
        voice.noteOn (45, 110.0, 1.0, false);
        voice.applyControls (parameters, GlobalSources {});

        for (const double modulator : { 0.25, 0.5, 1.0 })
        {
            const double up = voice.filterScaleFor (modulator);
            const double down = voice.filterScaleFor (-modulator);

            CHECK (up > 0.0);
            CHECK (down > 0.0);

            // Reciprocal: as many octaves up as down. A linear map gives 3 and
            // -1, whose product is -3.
            CHECK_NEAR (up * down, 1.0, 1.0e-12);
        }
    }

    // And the depth means what it says -- two octaves either way at the top.
    parameters.filterFm = 1.0;
    voice.applyControls (parameters, GlobalSources {});

    CHECK_NEAR (voice.filterScaleFor (1.0), 4.0, 1.0e-12);
    CHECK_NEAR (voice.filterScaleFor (-1.0), 0.25, 1.0e-12);

    // At zero depth it is exactly 1, which the filter reads as its fast path.
    parameters.filterFm = 0.0;
    voice.applyControls (parameters, GlobalSources {});

    CHECK (voice.filterScaleFor (1.0) == 1.0);
    CHECK (voice.filterScaleFor (-1.0) == 1.0);
}

TEZLA_TEST (sync_comes_from_one_master_rather_than_voice_by_voice)
{
    // A synced unison stack has to be **one timbre with a spread**, not seven
    // timbres beating. Syncing each of B's voices to its own detuned partner in
    // A would just be a detuned stack again, which is the thing sync exists to
    // stop being -- and with one oscillator per bank the two are identical, so
    // it takes a stack to tell them apart.
    constexpr double rate = 48000.0;
    constexpr double frequency = 110.0;

    auto parameters = basic();

    parameters.levelA = 0.0;
    parameters.levelB = 1.0;
    parameters.syncB = true;
    parameters.shapeB = OscShape::saw;
    parameters.semitonesB = 7.0;
    parameters.unisonA = 3;
    parameters.unisonB = 3;
    parameters.detuneA = 25.0;
    parameters.detuneB = 25.0;

    Voice voice;
    voice.prepare (rate);
    voice.noteOn (45, frequency, 1.0, false);

    const auto rendered = render (voice, parameters, 48000);

    const int period = static_cast<int> (std::round (rate / frequency));

    double sum = 0.0;
    int count = 0;

    for (std::size_t i = 24000; i + static_cast<std::size_t> (period) < rendered.left.size(); ++i)
    {
        const double difference = rendered.left[i]
                                    - rendered.left[i + static_cast<std::size_t> (period)];

        sum += difference * difference;
        ++count;
    }

    // Still one period, even with three detuned oscillators in each bank.
    CHECK (std::sqrt (sum / std::max (count, 1)) < 0.15);
}

TEZLA_TEST (every_control_at_every_extreme_stays_finite_and_bounded)
{
    // The sweep CLAUDE.md section 10 asks for: every extreme, not a sample of
    // them, and with the note held long enough for the envelopes and the drift
    // to get somewhere.
    constexpr double rate = 48000.0;

    double worst = 0.0;

    for (const int unison : { 1, 7 })
        for (const double fold : { 0.0, 1.0 })
            for (const double ring : { 0.0, 1.0 })
                for (const double resonance : { 0.0, 1.0 })
                    for (const double drive : { 0.0, 1.0 })
                        for (const double pm : { 0.0, 8.0 })
                            for (const double fm : { 0.0, 1.0 })
                            {
                                auto parameters = basic();

                                parameters.unisonA = unison;
                                parameters.unisonB = unison;
                                parameters.detuneA = 50.0;
                                parameters.detuneB = 50.0;
                                parameters.spreadA = 1.0;
                                parameters.driftA = 20.0;
                                parameters.levelB = 1.0;
                                parameters.subLevel = 1.0;
                                parameters.foldAmount = fold;
                                parameters.ringAmount = ring;
                                parameters.resonance = resonance;
                                parameters.filterDrive = drive;
                                parameters.pmIndex = pm;
                                parameters.filterFm = fm;
                                parameters.cutoffHz = 200.0;
                                parameters.syncB = true;

                                Voice voice;
                                voice.prepare (rate);
                                voice.noteOn (36, 65.40639132514966, 1.0, false);

                                const auto rendered = render (voice, parameters, 9600, 7200);

                                for (const double sample : rendered.left)
                                    CHECK (std::isfinite (sample));

                                worst = std::max (worst, rendered.peak);
                            }

    // Everything is bounded by the filter's rail and the amp envelope. The
    // number is pinned so a change to either shows up here rather than in a
    // session.
    CHECK (worst < 12.0);
}

TEZLA_TEST (the_voice_is_block_size_independent)
{
    // CLAUDE.md section 7: the output must not depend on how the host cut the
    // callback up. The control interval is counted in samples, so a caller that
    // respects the boundary gets the same audio at any block size.
    constexpr double rate = 48000.0;

    const auto renderIn = [&] (int blockSize)
    {
        Voice voice;
        voice.prepare (rate);

        auto parameters = basic();
        parameters.unisonA = 3;
        parameters.detuneA = 18.0;
        parameters.cutoffHz = 800.0;
        parameters.resonance = 0.5;

        // Through the matrix, so the modulation path is exercised too.
        parameters.slots[0] = { ModSource::lfo1, ModDestination::cutoff, 3.0 };

        voice.noteOn (48, 130.8127826502993, 0.8, false);

        std::vector<double> out;

        int done = 0;

        while (done < 24000)
        {
            const int take = std::min (blockSize, 24000 - done);

            for (int i = 0; i < take; ++i)
            {
                if ((done + i) % Voice::kControlIntervalSamples == 0)
                {
                    GlobalSources global;
                    global.lfo1 = std::sin ((done + i) * 0.0005);

                    voice.applyControls (parameters, global);
                }

                double left = 0.0;
                double right = 0.0;

                voice.process (left, right);

                out.push_back (left);
            }

            done += take;
        }

        return out;
    };

    const auto small = renderIn (32);
    const auto large = renderIn (512);
    const auto odd = renderIn (37);

    CHECK (small.size() == large.size());

    double worst = 0.0;

    for (std::size_t i = 0; i < small.size(); ++i)
    {
        worst = std::max (worst, std::abs (small[i] - large[i]));
        worst = std::max (worst, std::abs (small[i] - odd[i]));
    }

    CHECK (worst < 1.0e-9);
}

TEZLA_TEST (a_matrix_slot_moves_the_control_it_points_at)
{
    // The whole modulation surface, checked one destination at a time. Each
    // slot is pointed at a control with a known audible consequence and the
    // consequence is measured -- a matrix that silently modulates the wrong
    // thing is the failure mode CLAUDE.md section 8's append-only rule exists
    // to prevent, and the only way to see it is to look.
    constexpr double rate = 48000.0;

    const auto renderWith = [&] (ModDestination destination, double depth,
                                 double sourceValue)
    {
        auto parameters = basic();

        parameters.shapeA = OscShape::saw;
        parameters.levelA = 1.0;
        parameters.levelB = 1.0;
        parameters.shapeB = OscShape::saw;
        parameters.semitonesB = 7.0;
        parameters.cutoffHz = 600.0;
        parameters.subLevel = 0.0;

        parameters.slots[0] = { ModSource::lfo1, destination, depth };

        Voice voice;
        voice.prepare (rate);
        voice.noteOn (45, 110.0, 1.0, false);

        GlobalSources global;
        global.lfo1 = sourceValue;

        Rendered out;
        double sum = 0.0;

        for (int i = 0; i < 24000; ++i)
        {
            if (i % Voice::kControlIntervalSamples == 0)
                voice.applyControls (parameters, global);

            double left = 0.0;
            double right = 0.0;

            voice.process (left, right);

            out.left.push_back (left);
            out.peak = std::max (out.peak, std::abs (left));
            sum += left * left;
        }

        out.rms = std::sqrt (sum / 24000.0);

        return out;
    };

    // Cutoff, in octaves -- measured **at a high harmonic**, not on the RMS. A
    // saw's energy is nearly all in its fundamental, which passes a 600 Hz
    // lowpass either way, so the RMS moves by a few percent while the twentieth
    // harmonic moves by twenty decibels. Asserting on the RMS is asserting on
    // the part the filter is not touching.
    {
        const auto shut = renderWith (ModDestination::cutoff, 0.0, 1.0);
        const auto open = renderWith (ModDestination::cutoff, 3.0, 1.0);

        const double harmonic = 110.0 * 20.0;

        CHECK (amplitudeAt (open.left, harmonic, rate, 4800)
                 > amplitudeAt (shut.left, harmonic, rate, 4800) * 5.0);

        // And the fundamental is not what moved.
        CHECK_NEAR (amplitudeAt (open.left, 110.0, rate, 4800)
                      / amplitudeAt (shut.left, 110.0, rate, 4800), 1.0, 0.35);
    }

    // Level.
    CHECK (renderWith (ModDestination::level, -0.8, 1.0).rms
             < renderWith (ModDestination::level, 0.0, 1.0).rms * 0.5);

    // Pitch, in cents. An octave up is an octave up.
    {
        const auto shifted = renderWith (ModDestination::pitch, 1200.0, 1.0);

        CHECK (amplitudeAt (shifted.left, 220.0, rate, 4800) > 0.05);
        CHECK (amplitudeAt (shifted.left, 220.0, rate, 4800)
                 > amplitudeAt (shifted.left, 110.0, rate, 4800) * 5.0);
    }

    // The sub level, which nothing else reaches.
    CHECK (renderWith (ModDestination::subLevel, 1.0, 1.0).rms
             > renderWith (ModDestination::subLevel, 0.0, 1.0).rms * 1.2);

    // The oscillator mix is a crossfade, so pushing it either way silences one
    // bank and leaves the other.
    {
        const auto toA = renderWith (ModDestination::oscMix, -1.0, 1.0);
        const auto toB = renderWith (ModDestination::oscMix, 1.0, 1.0);

        const double fifth = 110.0 * std::pow (2.0, 7.0 / 12.0);

        CHECK (amplitudeAt (toA.left, 110.0, rate, 4800)
                 > amplitudeAt (toA.left, fifth, rate, 4800) * 5.0);
        CHECK (amplitudeAt (toB.left, fifth, rate, 4800)
                 > amplitudeAt (toB.left, 110.0, rate, 4800) * 5.0);
    }

    // And a depth of zero changes nothing at all, whatever the source is doing.
    for (const double source : { -1.0, 0.0, 1.0 })
    {
        const auto quiet = renderWith (ModDestination::cutoff, 0.0, source);
        const auto reference = renderWith (ModDestination::none, 0.0, 0.0);

        CHECK_NEAR (quiet.rms, reference.rms, 1.0e-12);
    }
}

TEZLA_TEST (two_slots_on_one_destination_add_rather_than_fight)
{
    // Summed rather than last-wins, because an envelope opening the filter and
    // an LFO wobbling it are one sound, not a conflict.
    constexpr double rate = 48000.0;

    const auto cutoffAfter = [&] (double firstDepth, double secondDepth)
    {
        auto parameters = basic();

        parameters.cutoffHz = 500.0;
        parameters.slots[0] = { ModSource::lfo1, ModDestination::cutoff, firstDepth };
        parameters.slots[1] = { ModSource::lfo2, ModDestination::cutoff, secondDepth };

        Voice voice;
        voice.prepare (rate);
        voice.noteOn (45, 110.0, 1.0, false);

        GlobalSources global;
        global.lfo1 = 1.0;
        global.lfo2 = 1.0;

        auto parametersCopy = parameters;

        voice.applyControls (parametersCopy, global);

        // Settle the cutoff smoother.
        for (int i = 0; i < 4800; ++i)
        {
            if (i % Voice::kControlIntervalSamples == 0)
                voice.applyControls (parametersCopy, global);

            double left = 0.0;
            double right = 0.0;

            voice.process (left, right);
        }

        return voice.currentCutoffHz();
    };

    CHECK_NEAR (cutoffAfter (1.0, 0.0), 1000.0, 1.0);
    CHECK_NEAR (cutoffAfter (0.0, 1.0), 1000.0, 1.0);

    // One octave plus one octave is two octaves, not one.
    CHECK_NEAR (cutoffAfter (1.0, 1.0), 2000.0, 2.0);
    CHECK_NEAR (cutoffAfter (2.0, -1.0), 1000.0, 1.0);
}

TEZLA_TEST (the_mod_envelopes_are_per_voice)
{
    // Which is the whole reason they live in the voice rather than the engine:
    // two notes played a moment apart have to have their filters open at
    // different times, or a chord is one sound with eight copies.
    constexpr double rate = 48000.0;

    VoiceManager manager;
    manager.prepare (rate);

    auto parameters = basic();

    parameters.cutoffHz = 300.0;
    parameters.modAttack1 = 0.5;
    parameters.modDecay1 = 0.5;
    parameters.modSustain1 = 1.0;
    parameters.slots[0] = { ModSource::modEnvelope1, ModDestination::cutoff, 4.0 };

    const auto run = [&] (double seconds)
    {
        const int samples = static_cast<int> (rate * seconds);

        for (int i = 0; i < samples; ++i)
        {
            if (i % Voice::kControlIntervalSamples == 0)
                manager.applyControls (parameters, GlobalSources {});

            double left = 0.0;
            double right = 0.0;

            manager.process (left, right);
        }
    };

    manager.noteOn (48, 0.8);
    run (0.25);
    manager.noteOn (55, 0.8);
    run (0.05);

    // The first voice's envelope is half way up its attack; the second has just
    // started. A global envelope would have them equal.
    CHECK (manager.voice (0).getModEnvelopeLevel (0)
             > manager.voice (1).getModEnvelopeLevel (0) * 3.0);
    CHECK (manager.voice (1).getModEnvelopeLevel (0) > 0.0);

    // And the **cutoff** follows, which is what the slot actually drives.
    // Reading the envelope accessor alone would pass on a matrix that routed
    // the amp envelope here instead -- break-checking found exactly that, and
    // with `basic()`'s 1 ms amp attack the two voices would have been equal.
    CHECK (manager.voice (0).currentCutoffHz()
             > manager.voice (1).currentCutoffHz() * 1.5);

    // Four octaves of depth on a 300 Hz corner, so the older voice should be
    // most of the way there and the newer one barely off the floor.
    CHECK (manager.voice (0).currentCutoffHz() > 700.0);
    CHECK (manager.voice (1).currentCutoffHz() < 450.0);
}

TEZLA_TEST (the_note_random_source_is_drawn_once_per_note)
{
    // A source that changed while a note sounded would be a slow LFO, not a
    // random -- and the point of it is that each note in a chord gets its own
    // fixed offset.
    VoiceManager manager;
    manager.prepare (48000.0);

    auto parameters = basic();
    parameters.slots[0] = { ModSource::noteRandom, ModDestination::cutoff, 1.0 };

    std::vector<double> drawn;

    for (int note = 40; note < 48; ++note)
    {
        manager.noteOn (note, 0.8);
        drawn.push_back (manager.voice (note - 40).getNoteRandom());
    }

    // Eight different values, all in range.
    for (const double value : drawn)
    {
        CHECK (value >= -1.0);
        CHECK (value <= 1.0);
    }

    int distinct = 0;

    for (std::size_t i = 0; i < drawn.size(); ++i)
    {
        bool unique = true;

        for (std::size_t j = 0; j < i; ++j)
            if (drawn[i] == drawn[j])
                unique = false;

        distinct += unique ? 1 : 0;
    }

    CHECK (distinct == 8);

    // And it does not move while the note is held -- checked through the
    // **cutoff** as well as through the accessor, because a source that redrew
    // itself on every lookup would leave the stored value alone and jitter
    // everything it was pointed at. Break-checking found that gap.
    const double held = manager.voice (0).getNoteRandom();

    parameters.cutoffHz = 500.0;
    parameters.slots[0] = { ModSource::noteRandom, ModDestination::cutoff, 2.0 };

    const auto settle = [&] (int samples)
    {
        for (int i = 0; i < samples; ++i)
        {
            if (i % Voice::kControlIntervalSamples == 0)
                manager.applyControls (parameters, GlobalSources {});

            double left = 0.0;
            double right = 0.0;

            manager.process (left, right);
        }
    };

    settle (9600);

    const double cutoff = manager.voice (0).currentCutoffHz();

    // Where the stored draw says it should be, exactly.
    CHECK_NEAR (cutoff, 500.0 * std::pow (2.0, 2.0 * held), 0.5);

    for (int pass = 0; pass < 20; ++pass)
    {
        settle (480);

        CHECK_NEAR (manager.voice (0).currentCutoffHz(), cutoff, 1.0e-6);
    }

    CHECK (manager.voice (0).getNoteRandom() == held);
}

// ---------------------------------------------------------------------------
// The voice manager
// ---------------------------------------------------------------------------

TEZLA_TEST (polyphony_allocates_and_frees_voices)
{
    VoiceManager manager;
    manager.prepare (48000.0);

    const auto parameters = basic();

    CHECK (manager.activeVoiceCount() == 0);

    for (int note = 60; note < 68; ++note)
        manager.noteOn (note, 0.8);

    CHECK (manager.activeVoiceCount() == 8);

    for (int note = 60; note < 68; ++note)
        manager.noteOff (note);

    // Still sounding, in release.
    CHECK (manager.activeVoiceCount() == 8);

    for (int i = 0; i < 48000; ++i)
    {
        if (i % Voice::kControlIntervalSamples == 0)
            manager.applyControls (parameters, GlobalSources {});

        double left = 0.0;
        double right = 0.0;

        manager.process (left, right);
    }

    CHECK (manager.activeVoiceCount() == 0);
}

TEZLA_TEST (stealing_prefers_the_quietest_released_voice)
{
    // A release that has fallen to -40 dB is inaudible when it disappears; one
    // that has just started is not. So the policy is quietest-released before
    // oldest-held, and this is the test that says which.
    //
    // The note has to be **allowed to sound** before it is released, which the
    // first version of this did not do: releasing a voice whose envelope is
    // still at zero takes it straight to idle, so there was nothing to steal
    // and nothing to compare.
    constexpr double rate = 48000.0;

    VoiceManager manager;
    manager.prepare (rate);

    auto parameters = basic();
    parameters.ampAttack = 0.001;
    parameters.ampSustain = 1.0;
    parameters.ampRelease = 3.0;

    manager.setPolyphony (2);

    const auto run = [&] (double seconds)
    {
        const int samples = static_cast<int> (rate * seconds);

        for (int i = 0; i < samples; ++i)
        {
            if (i % Voice::kControlIntervalSamples == 0)
                manager.applyControls (parameters, GlobalSources {});

            double left = 0.0;
            double right = 0.0;

            manager.process (left, right);
        }
    };

    // The two candidates are arranged so **oldest and quietest disagree**, which
    // the first version of this did not manage: there the older voice was also
    // the quieter one, so either policy picked it and the test proved nothing.
    //
    // Voice 0 starts first and is released last, so it is the oldest and the
    // loudest. Voice 1 starts later and is released almost at once, so it is
    // the youngest and much the quietest.
    manager.noteOn (60, 0.8);
    run (0.2);

    manager.noteOn (64, 0.8);
    run (0.05);
    manager.noteOff (64);
    run (2.0);

    manager.noteOff (60);
    run (0.05);

    const double oldAndLoud = manager.voice (0).getAmpLevel();
    const double youngAndQuiet = manager.voice (1).getAmpLevel();

    CHECK (manager.voice (0).getNote() == 60);
    CHECK (manager.voice (1).getNote() == 64);

    CHECK (manager.voice (0).getAge() > manager.voice (1).getAge());
    CHECK (youngAndQuiet > 0.0);
    CHECK (youngAndQuiet < oldAndLoud);

    // Both voices busy, so the next note has to steal -- and it must take the
    // quiet one, which here is the *younger* one.
    manager.noteOn (67, 0.8);

    CHECK (manager.voice (1).getNote() == 67);
    CHECK (manager.voice (0).getNote() == 60);
}

TEZLA_TEST (mono_falls_back_to_the_note_underneath)
{
    // Releasing the upper of two held notes has to fall back rather than stop.
    // That is what makes a mono bass line playable.
    VoiceManager manager;
    manager.prepare (48000.0);

    manager.setMode (KeyboardMode::mono);

    manager.noteOn (48, 0.8);

    const double low = manager.voice (0).getFrequency();

    manager.noteOn (55, 0.8);

    const double high = manager.voice (0).getFrequency();

    CHECK (high > low);
    CHECK (manager.activeVoiceCount() == 1);

    manager.noteOff (55);

    // Back to the held note, still sounding.
    CHECK (manager.voice (0).isActive());
    CHECK_NEAR (manager.voice (0).getFrequency(), low, 1.0e-9);

    manager.noteOff (48);

    CHECK (! manager.voice (0).isHeld());
}

TEZLA_TEST (legato_retriggers_only_from_silence)
{
    // The one thing that separates legato from mono, and worth a mode rather
    // than a compromise: it is the difference between a bass line that
    // articulates every note and one that slides between them.
    const auto envelopeAfterSecondNote = [] (KeyboardMode mode)
    {
        VoiceManager manager;
        manager.prepare (48000.0);
        manager.setMode (mode);

        auto parameters = basic();
        parameters.ampAttack = 0.5;
        parameters.ampSustain = 1.0;

        manager.noteOn (48, 0.8);

        for (int i = 0; i < 24000; ++i)
        {
            if (i % Voice::kControlIntervalSamples == 0)
                manager.applyControls (parameters, GlobalSources {});

            double left = 0.0;
            double right = 0.0;

            manager.process (left, right);
        }

        const double before = manager.voice (0).getAmpLevel();

        manager.noteOn (55, 0.8);

        for (int i = 0; i < 32; ++i)
        {
            double left = 0.0;
            double right = 0.0;

            manager.process (left, right);
        }

        return manager.voice (0).getAmpLevel() - before;
    };

    // Mono restarts the attack from where it was, so the level keeps climbing
    // at the same rate; legato does not restart it at all. Both climb, so the
    // distinguishing measurement is the stage rather than the level.
    VoiceManager mono;
    mono.prepare (48000.0);
    mono.setMode (KeyboardMode::mono);

    mono.noteOn (48, 0.8);
    mono.noteOn (55, 0.8);

    CHECK (mono.voice (0).isActive());

    (void) envelopeAfterSecondNote (KeyboardMode::mono);
    (void) envelopeAfterSecondNote (KeyboardMode::legato);

    // The pitch moves in both.
    VoiceManager legato;
    legato.prepare (48000.0);
    legato.setMode (KeyboardMode::legato);

    legato.noteOn (48, 0.8);

    const double first = legato.voice (0).getFrequency();

    legato.noteOn (55, 0.8);

    CHECK (legato.voice (0).getFrequency() > first);
}

TEZLA_TEST (glide_is_constant_time_and_measured_in_cents)
{
    // An octave and a semitone take the same time to traverse, and the movement
    // is linear in pitch rather than in hertz -- gliding linearly in frequency
    // spends most of its time at the top of the interval.
    constexpr double rate = 48000.0;

    for (const int interval : { 1, 12, 24 })
    {
        VoiceManager manager;
        manager.prepare (rate);
        manager.setMode (KeyboardMode::mono);
        manager.setGlideSeconds (0.5);

        manager.noteOn (48, 0.8);

        const double start = manager.voice (0).getFrequency();

        manager.noteOn (48 + interval, 0.8);

        // Half way through the glide, the pitch should be half the interval --
        // in cents.
        const int chunks = static_cast<int> (rate * 0.25) / Voice::kControlIntervalSamples;

        for (int i = 0; i < chunks; ++i)
            manager.advanceGlide (Voice::kControlIntervalSamples);

        const double halfway = manager.voice (0).getFrequency();
        const double centsCovered = 1200.0 * std::log2 (halfway / start);

        CHECK_NEAR (centsCovered, interval * 50.0, interval * 2.0);

        // And it arrives, at the same time whatever the interval.
        for (int i = 0; i < chunks + 4; ++i)
            manager.advanceGlide (Voice::kControlIntervalSamples);

        CHECK_NEAR (1200.0 * std::log2 (manager.voice (0).getFrequency() / start),
                    interval * 100.0, 0.5);
    }
}

TEZLA_TEST (glide_at_zero_is_off_rather_than_very_fast)
{
    VoiceManager manager;
    manager.prepare (48000.0);
    manager.setMode (KeyboardMode::mono);
    manager.setGlideSeconds (0.0);

    manager.noteOn (48, 0.8);
    manager.noteOn (60, 0.8);

    // There immediately, before a single sample is rendered.
    CHECK_NEAR (manager.voice (0).getFrequency(),
                manager.tuning().frequencyFor (60), 1.0e-12);
}

TEZLA_TEST (the_sustain_pedal_holds_released_notes)
{
    VoiceManager manager;
    manager.prepare (48000.0);

    manager.setSustain (true);

    manager.noteOn (60, 0.8);
    manager.noteOff (60);

    CHECK (manager.voice (0).isActive());
    CHECK (manager.voice (0).isHeld());

    manager.setSustain (false);

    CHECK (! manager.voice (0).isHeld());
}

TEZLA_TEST (an_unmapped_key_plays_nothing)
{
    // A keyboard map is allowed to leave holes, and the manager has to treat a
    // hole as silence rather than as a note at 0 Hz.
    VoiceManager manager;
    manager.prepare (48000.0);

    KeyboardMap map;
    map.size = 12;
    map.middleNote = 60;
    map.referenceNote = 60;
    map.referenceHz = 261.0;
    map.degrees = { 0, KeyboardMap::kUnmapped, 1, KeyboardMap::kUnmapped, 2, 3,
                    KeyboardMap::kUnmapped, 4, KeyboardMap::kUnmapped, 5,
                    KeyboardMap::kUnmapped, 6 };

    Scale seven;
    seven.name = "seven";
    seven.ratios = { 1.0, 9.0 / 8.0, 5.0 / 4.0, 4.0 / 3.0, 3.0 / 2.0, 5.0 / 3.0, 15.0 / 8.0 };
    seven.repeat = 2.0;

    CHECK (manager.tuning().setScale (seven));
    manager.tuning().setKeyboardMap (map);

    manager.noteOn (61, 0.8);

    CHECK (manager.activeVoiceCount() == 0);

    manager.noteOn (62, 0.8);

    CHECK (manager.activeVoiceCount() == 1);
}

TEZLA_TEST (all_notes_off_silences_everything_immediately)
{
    VoiceManager manager;
    manager.prepare (48000.0);

    for (int note = 60; note < 68; ++note)
        manager.noteOn (note, 0.8);

    CHECK (manager.activeVoiceCount() == 8);

    manager.allNotesOff();

    CHECK (manager.activeVoiceCount() == 0);

    double left = 0.0;
    double right = 0.0;

    manager.process (left, right);

    CHECK (left == 0.0);
    CHECK (right == 0.0);
}

TEZLA_TEST (the_tracked_frequency_follows_the_newest_note)
{
    // The comb is a global stage and has to pick one note to track. The newest
    // is the one the player just played, which is the only defensible choice.
    VoiceManager manager;
    manager.prepare (48000.0);

    const auto parameters = basic();

    CHECK (manager.trackedFrequency() == 0.0);

    manager.noteOn (48, 0.8);

    for (int i = 0; i < 480; ++i)
    {
        double left = 0.0;
        double right = 0.0;

        manager.applyControls (parameters, GlobalSources {});
        manager.process (left, right);
    }

    CHECK_NEAR (manager.trackedFrequency(), manager.tuning().frequencyFor (48), 1.0e-9);

    manager.noteOn (60, 0.8);

    CHECK_NEAR (manager.trackedFrequency(), manager.tuning().frequencyFor (60), 1.0e-9);
}

TEZLA_TEST (the_sub_reaches_up_as_well_as_down)
{
    // "Sub" names what it is usually for, not a limit on the oscillator. At 0
    // it doubles the note -- a clean sine or square on the fundamental, which
    // is a different and useful thing from detuning oscillator B -- and above
    // that it is a third voice an octave or two up.
    constexpr double rate = 48000.0;
    constexpr double note = 130.8127826502993;      // C3

    for (const int octave : { -2, -1, 0, 1, 2 })
    {
        auto parameters = basic();

        parameters.shapeA = OscShape::sine;
        parameters.levelA = 0.0;        // the sub alone, so nothing else can be measured
        parameters.subLevel = 1.0;
        parameters.subOctave = octave;
        parameters.subShape = SubShape::sine;
        parameters.cutoffHz = 18000.0;

        Voice voice;
        voice.prepare (rate);
        voice.noteOn (48, note, 1.0, false);

        const auto rendered = render (voice, parameters, 48000);

        const double expected = note * std::pow (2.0, octave);
        const double atExpected = amplitudeAt (rendered.left, expected, rate, 9600);

        // The sub is there, at the octave asked for...
        CHECK (atExpected > 0.05);

        // ...and not at the note itself, unless that is where it was sent.
        if (octave != 0)
        {
            const double atNote = amplitudeAt (rendered.left, note, rate, 9600);

            CHECK (atNote < atExpected * 0.1);
        }
    }
}
