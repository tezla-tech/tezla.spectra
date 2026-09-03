// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A stack of detuned oscillators, which is where a reese comes from.
//
// ---------------------------------------------------------------------------
// A reese is a comb you cannot see
// ---------------------------------------------------------------------------
//
// Two saws a few cents apart drift in and out of phase with each other at the
// difference frequency. Where they agree, harmonics add; where they oppose,
// harmonics cancel. That is a comb filter whose notches sweep -- and the sweep
// rate is the detune, in Hz, at each harmonic. It is the same mechanism as the
// flanger downstream of it, arrived at from the other direction, and stacking
// seven of them is what turns a beat into a churn.
//
// So the detune is the instrument's first tone control, and it is in cents
// rather than Hz on purpose: a fixed cent spread beats faster on high notes
// than low ones, which is what keeps a bass line's movement proportional to
// its pitch instead of turning to mud at the bottom.
//
// ---------------------------------------------------------------------------
// Three things that separate a good stack from seven copies
// ---------------------------------------------------------------------------
//
// **Phases start apart.** Seven oscillators reset to the same phase are one
// oscillator seven times as loud, and they stay that way for as long as it
// takes the detune to pull them apart -- which at 5 cents on a low E is over a
// second. The attack of every note would be a single loud saw. So note-on
// randomises them.
//
// **The sum is normalised for incoherent addition.** N uncorrelated sources sum
// to sqrt(N), not N. Dividing by N makes a seven-voice stack quieter than a
// one-voice one and the control unusable; dividing by sqrt(N) holds the level
// where the ear expects it. Measured in the tests.
//
// **The drift is not the detune.** A real analogue stack is never exactly in
// tune with itself, and never exactly out either -- each oscillator wanders. A
// static detune gives a periodic churn that the ear locks onto within a bar. A
// slow random wander on top of it never repeats, and that is the whole of what
// the "analogue" knob on an old polysynth was doing.
//
// **And the drift does not retrigger.** It is a property of the voice card,
// not of the note: a key going down does not reset the temperature of a
// transistor. So a cold note re-scatters the phases -- the same seven values
// every time, the unison retriggers exactly as it always has -- and leaves the
// drift walk wherever it was, still walking. For a long time note-on called
// the full reset, which re-seeded one shared random stream and zeroed the
// walk, and a given voice slot played the same wander on every press. Two
// streams now: one for the scatter, re-seeded per note; one for the drift,
// seeded once and never by a note. See `reset` and `restartNote`.
//
// ---------------------------------------------------------------------------
// The stack does not have to be a detune
// ---------------------------------------------------------------------------
//
// Everything above is one way of placing N oscillators: symmetric, in cents,
// shaped by `kSpreadExponent`. It is a reese and it is the right default. It
// is also the *only* thing this class knew how to be, and one array short of
// being three other instruments.
//
// `setRankOffsets` supplies a pitch offset in cents and a gain **per copy**,
// on top of the detune rather than instead of it -- so a stack placed at
// musical intervals can still churn, and the detune control never becomes a
// dead knob in some mode. What the offsets *mean* is not this class's
// business: musical intervals, degrees of a loaded tuning and a Shepard
// glissando are all the same array from here, and the two that need to know
// about scales or about a moving phase are computed by the caller. See
// `Shepard.hpp` for the one whose arithmetic has a theorem in it.
//
// **The neutral case is bit-exact, and by arithmetic rather than by a branch.**
// Adding 0.0 to a finite double returns it unchanged; the single exception is
// -0.0 + 0.0 = +0.0, which happens for the lower half of the stack when the
// detune is zero, and which cannot be observed because `pow(2, +-0.0)` is
// exactly 1.0 either way. A gain of 1.0 multiplies exactly, and the
// normalisation below sums N ones to exactly N. So a caller that never touches
// these arrays gets the same samples it always did -- CLAUDE.md section 7 wants
// a neutral setting proved with a signal, and `tests/test_UnisonBank.cpp`
// proves it twice: once for a bank that was never told, and once for one told
// explicitly to be neutral, which is the path that would catch a regression.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Oscillator.hpp"
#include "SmallRandom.hpp"

namespace tezla::dsp {

class UnisonBank
{
public:
    static constexpr int kMaxVoices = 7;

    /// How far the outermost pair sits, as a multiple of the detune control.
    ///
    /// The spread is not linear across the stack. Spacing seven voices evenly
    /// puts most of them close to the centre where they beat slowly and do
    /// little; pushing the outer pairs further gives a wide, dense churn from
    /// the same nominal detune. The exponent below is what shapes that.
    static constexpr double kSpreadExponent = 1.6;

    /// The drift's corner, in Hz. Slow enough to be a wander rather than a
    /// vibrato -- above about 2 Hz it stops sounding like tuning and starts
    /// sounding like modulation, which is what the LFOs are for.
    static constexpr double kDriftHz = 0.35;

    /// How often the drift moves and the increments are pushed at the
    /// oscillators, in samples. CLAUDE.md section 7: anything too expensive to
    /// recompute per sample gets a timer counted in samples -- and this one
    /// was recomputed per sample for months while a comment in Oscillator.hpp
    /// claimed otherwise. Per sample, the push cost a pow(2, drift) per
    /// oscillator and, for the shapes whose state depends on pitch, the whole
    /// shape re-derivation: one three-note Harmonic chord measured **226% of
    /// a core** against 16% for the same chord on saw, all of it sixteen
    /// pow() calls per oscillator per sample that produced the same answers
    /// every time. On this timer the same chord costs what the saw costs.
    /// The step this quantises the drift into is microscopic -- the wander's
    /// corner is 0.35 Hz, so 32 samples move it by around a millionth of its
    /// range -- and the timer is counted in samples, so the output cannot
    /// depend on the host's buffer size.
    static constexpr int kIncrementIntervalSamples = 32;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        // The noise corner derived in setMorph depends on the rate, so a
        // rate change must make the next setMorph go through the guard.
        morph_ = -1.0;

        // A one-pole at kDriftHz, stepped once per interval rather than per
        // sample, so the wander is bounded and smooth at any rate.
        driftCoefficient_ = std::clamp (6.283185307179586 * kDriftHz
                                          * kIncrementIntervalSamples / sampleRate_,
                                        0.0, 1.0);

        updateIncrements();
    }

    /// The seed the phase scatter and the drift are drawn from.
    ///
    /// Settable, because **every bank scattering identically defeats the
    /// point**. With one fixed seed, two voices playing the same note start in
    /// exactly the same phase relationship and sum coherently -- +6 dB rather
    /// than the +3 the sqrt(N) normalisation assumes -- and oscillator A and
    /// oscillator B inside one voice drift in lockstep instead of against each
    /// other. Left alone it keeps the value it always had.
    void setSeed (std::uint64_t seed) noexcept { seed_ = seed | 1ull; }

    /// The **full** reset, for `prepare` and a graph rebuild: the drift stream
    /// is re-seeded and the walk goes back to zero, then everything a cold note
    /// does. A note-on wants `restartNote()` instead, which leaves the drift
    /// alone -- see the header.
    void reset (bool randomisePhases = true) noexcept
    {
        driftRandom_.seed (seed_ ^ kDriftSalt);

        for (auto& value : drift_)
            value = 0.0;

        for (auto& value : driftTarget_)
            value = 0.0;

        driftCountdown_ = 0;

        restartNote (randomisePhases);
    }

    /// What a cold note needs: every oscillator cleared and, unless told
    /// otherwise, its phase scattered -- see the header: a stack that starts in
    /// phase is one loud saw. The scatter is drawn from a stream re-seeded here,
    /// so it is the same seven values on every note and the unison retriggers
    /// exactly as it always has.
    ///
    /// **The drift is not touched.** Not the walk, not its targets, not its
    /// timer, not its stream: it carries on from wherever it was, so two
    /// presses of the same key are two different notes. That is the one thing
    /// in the bank a note-on is not allowed to restart.
    void restartNote (bool randomisePhases = true) noexcept
    {
        phaseRandom_.seed (seed_);

        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            voices_[index].reset (randomisePhases ? phaseRandom_.next() : 0.0);

            // Every voice its own noise stream, derived from the bank's seed:
            // a noise stack whose voices agree is mono however wide the pan.
            voices_[index].seedNoise (seed_ ^ (std::uint64_t { 0x9e3779b97f4a7c15 }
                                                 * static_cast<std::uint64_t> (i + 1)));
        }

        incrementCountdown_ = 0;
    }

    /// Where oscillator `index`'s drift walk is, in [-1, 1] before the cents
    /// scaling. For the test that a note does not restart it.
    [[nodiscard]] double driftOf (int index) const noexcept
    {
        return drift_[static_cast<std::size_t> (std::clamp (index, 0, kMaxVoices - 1))];
    }

    /// Oscillator `index`'s phase, for the test that a note scatters it exactly
    /// as it always has.
    [[nodiscard]] double phaseOf (int index) const noexcept
    {
        return voices_[static_cast<std::size_t> (std::clamp (index, 0, kMaxVoices - 1))].getPhase();
    }

    /// 1 to 7. One voice is a plain oscillator and costs what one costs.
    void setVoiceCount (int count) noexcept
    {
        const int wanted = std::clamp (count, 1, kMaxVoices);

        if (wanted == voiceCount_)
            return;

        voiceCount_ = wanted;
        updateIncrements();
    }

    [[nodiscard]] int getVoiceCount() const noexcept { return voiceCount_; }

    /// Total spread of the outermost pair, in cents.
    void setDetuneCents (double cents) noexcept
    {
        const double wanted = std::clamp (cents, 0.0, 100.0);

        if (isExactly (wanted, detuneCents_))
            return;

        detuneCents_ = wanted;
        updateIncrements();
    }

    /// 0 keeps the stack in the middle; 1 puts the outermost pair hard left and
    /// right. The centre voice, when there is one, stays centred either way.
    ///
    /// **This has to recompute, and for a long time it did not.** The pan gains
    /// live in `updateIncrements` alongside the detuned increments -- one loop
    /// fills both -- so a setter that only assigned `spread_` left the old gains
    /// in place until something *else* forced a rebuild. The symptom was exactly
    /// that: move the spread knob and nothing happens, then touch detune and the
    /// spread you set a minute ago suddenly appears. It is the same shape of
    /// mistake as a filter that only re-designs when its Q moves.
    void setSpread (double spread) noexcept
    {
        const double wanted = std::clamp (spread, 0.0, 1.0);

        if (isExactly (wanted, spread_))
            return;

        spread_ = wanted;
        updateIncrements();
    }

    /// How far each oscillator is allowed to wander, in cents.
    void setDrift (double cents) noexcept
    {
        const double wanted = std::clamp (cents, 0.0, 50.0);

        if (isExactly (wanted, driftCents_))
            return;

        driftCents_ = wanted;
        incrementCountdown_ = 0;
    }

    /// A pitch offset in cents and a gain, **per copy**, on top of the detune.
    ///
    /// `count` copies are read from each array; the rest are left neutral, and
    /// a null pointer means neutral for that array alone. So the default state
    /// -- 0 cents, gain 1 -- is what a caller that never calls this gets, bit
    /// for bit. See the header for why that is arithmetic rather than a branch.
    ///
    /// **Guarded, and the guard is here rather than at the call site.** Every
    /// stack mode but the sliding one pushes the same numbers on every control
    /// chunk, and what this triggers is `updateIncrements()`: seven `pow`s and
    /// fourteen trigonometric calls. Comparing fourteen doubles is cheaper by
    /// two orders of magnitude, and a guard in the caller would desynchronise
    /// the moment a second caller disagreed -- CLAUDE.md section 7.
    void setRankOffsets (const double* cents, const double* gains, int count) noexcept
    {
        const int wanted = std::clamp (count, 0, kMaxVoices);

        bool changed = false;

        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            const double cent = (cents != nullptr && i < wanted) ? cents[index] : 0.0;
            const double gain = (gains != nullptr && i < wanted) ? gains[index] : 1.0;

            if (! isExactly (cent, rankCents_[index]))
            {
                rankCents_[index] = cent;
                changed = true;
            }

            if (! isExactly (gain, rankGains_[index]))
            {
                rankGains_[index] = gain;
                changed = true;
            }
        }

        if (changed)
            updateIncrements();
    }

    /// Copy `index`'s offset and gain as currently set, for tests and displays.
    [[nodiscard]] double rankCentsOf (int index) const noexcept
    {
        return rankCents_[static_cast<std::size_t> (std::clamp (index, 0, kMaxVoices - 1))];
    }

    [[nodiscard]] double rankGainOf (int index) const noexcept
    {
        return rankGains_[static_cast<std::size_t> (std::clamp (index, 0, kMaxVoices - 1))];
    }

    void setFrequency (double hz) noexcept
    {
        const double wanted = std::max (hz, 0.0);

        if (isExactly (wanted, frequency_))
            return;

        frequency_ = wanted;
        updateIncrements();
    }

    [[nodiscard]] double getFrequency() const noexcept { return frequency_; }

    void setShape (OscShape shape) noexcept
    {
        for (auto& voice : voices_)
            voice.setShape (shape);
    }

    void setWidth (double width) noexcept
    {
        for (auto& voice : voices_)
            voice.setWidth (width);
    }

    void setMorph (double morph) noexcept
    {
        // Guarded: Sonitus pushes this at every control chunk, and below is a
        // `pow` plus a loop over every oscillator. The oscillators guard their
        // own setMorph already; this guard covers the corner computation and
        // the loop. Same value in, nothing recomputed, nothing changed.
        const double clamped = std::clamp (morph, 0.0, 1.0);
        if (isExactly (clamped, morph_))
            return;
        morph_ = clamped;

        for (auto& voice : voices_)
            voice.setMorph (morph);

        // The Noise shape's colour lives here because the oscillator does not
        // know the sample rate: morph sweeps the one-pole's corner from wide
        // open down to ~200 Hz, logarithmically.
        const double cornerHz = 20000.0 * std::pow (200.0 / 20000.0, clamped);
        const double g = std::clamp (6.283185307179586 * cornerHz / sampleRate_, 0.0, 1.0);

        for (auto& voice : voices_)
            voice.setNoiseCoefficient (g);
    }

    /// Direct access, so a caller can wire hard sync between two banks voice by
    /// voice -- the master's Nth oscillator driving the slave's Nth.
    [[nodiscard]] Oscillator& voice (int index) noexcept
    {
        return voices_[static_cast<std::size_t> (std::clamp (index, 0, kMaxVoices - 1))];
    }

    /// Operator feedback, applied to every voice in the stack.
    ///
    /// Per voice rather than on the summed output, and that is the point: each
    /// detuned copy is its own operator, feeding back on its own phase at its
    /// own pitch. Feeding the *sum* back into all of them would lock the stack
    /// into one shared timbre and lose the reason for detuning it.
    void setFeedback (double cycles) noexcept
    {
        for (auto& voice : voices_)
            voice.setFeedback (cycles);
    }

    /// One sample, into a stereo pair. `phaseMod` is applied to every voice.
    void process (double phaseMod, double& left, double& right) noexcept
    {
        // The drift and the increment push live on the sample-counted timer
        // above, not in the sample loop -- see kIncrementIntervalSamples for
        // the measurement that put them there. A control change zeroes the
        // countdown, so nothing waits for the timer to notice it.
        if (incrementCountdown_ <= 0)
        {
            advanceDrift();

            for (int i = 0; i < voiceCount_; ++i)
            {
                const auto index = static_cast<std::size_t> (i);

                // The drift rides on top of the detune, as a further ratio.
                voices_[index].setIncrement (increments_[index] * driftRatio (index));
            }

            incrementCountdown_ = kIncrementIntervalSamples;
        }

        --incrementCountdown_;

        left = 0.0;
        right = 0.0;

        for (int i = 0; i < voiceCount_; ++i)
        {
            const auto index = static_cast<std::size_t> (i);
            const double value = voices_[index].advance (phaseMod);

            left  += value * gainL_[index];
            right += value * gainR_[index];
        }

        left *= normalisation_;
        right *= normalisation_;
    }

    /// What the stack is being scaled by, for tests and for a display.
    [[nodiscard]] double getNormalisation() const noexcept { return normalisation_; }

private:
    /// Where voice `i` sits in [-1, 1] across the stack.
    ///
    /// Symmetric, and shaped by kSpreadExponent so the outer pairs sit further
    /// out than an even spacing would put them.
    [[nodiscard]] double position (int i) const noexcept
    {
        if (voiceCount_ <= 1)
            return 0.0;

        const double linear = 2.0 * static_cast<double> (i)
                                / static_cast<double> (voiceCount_ - 1) - 1.0;

        return std::copysign (std::pow (std::abs (linear), kSpreadExponent), linear);
    }

    void updateIncrements() noexcept
    {
        double power = 0.0;

        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            // The rank offset rides on the detune rather than replacing it, so
            // a stack placed at intervals can still churn.
            const double offset = position (i) * detuneCents_ * 0.5 + rankCents_[index];

            // Cents to a frequency ratio, then to cycles per sample.
            const double ratio = std::pow (2.0, offset / 1200.0);

            increments_[index] = frequency_ * ratio / sampleRate_;

            // Equal-power panning, so the stack's width does not change its
            // loudness. The centre voice of an odd stack lands at 0.5/0.5.
            const double pan = position (i) * spread_;
            const double angle = (pan * 0.5 + 0.5) * 1.5707963267948966;

            gainL_[index] = std::cos (angle) * rankGains_[index];
            gainR_[index] = std::sin (angle) * rankGains_[index];

            if (i < voiceCount_)
                power += rankGains_[index] * rankGains_[index];
        }

        // sqrt(N), not N: the voices are uncorrelated once their phases have
        // scattered, so that is how their sum actually grows.
        //
        // With per-copy gains that generalises to the root of their summed
        // *power*, and the generalisation is exact rather than merely equal:
        // summing N ones gives exactly N in floating point for every N this
        // class allows, so a bank with no rank gains set divides by precisely
        // the 1/sqrt(N) it always did. A windowed stack -- see `Shepard.hpp` --
        // is quieter by the window's own factor and this is what puts it back.
        normalisation_ = power > 0.0 ? 1.0 / std::sqrt (power) : 0.0;

        // A frequency, detune or count change reaches the oscillators on the
        // next sample, not up to an interval late: a stolen voice retriggered
        // at a new pitch must never play the old one first.
        incrementCountdown_ = 0;
    }

    /// A bounded random walk per voice: targets redrawn every 50 ms, a
    /// one-pole at kDriftHz walking towards them. Called once per increment
    /// interval, so the countdown -- still measured in samples -- moves by the
    /// interval per call.
    void advanceDrift() noexcept
    {
        if (driftCountdown_ <= 0)
        {
            for (int i = 0; i < kMaxVoices; ++i)
                driftTarget_[static_cast<std::size_t> (i)] = driftRandom_.bipolar();

            driftCountdown_ = static_cast<int> (sampleRate_ * 0.05);
        }

        driftCountdown_ -= kIncrementIntervalSamples;

        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            drift_[index] += driftCoefficient_ * (driftTarget_[index] - drift_[index]);
        }
    }

    [[nodiscard]] double driftRatio (std::size_t index) const noexcept
    {
        if (isExactlyZero (driftCents_))
            return 1.0;

        return std::pow (2.0, drift_[index] * driftCents_ / 1200.0);
    }

    double sampleRate_ { 48000.0 };

    /// Last morph applied, so setMorph can refuse a no-op. -1 is outside the
    /// clamped range, which makes the first call always go through.
    double morph_ { -1.0 };
    double frequency_  { 0.0 };

    int    voiceCount_   { 1 };
    double detuneCents_  { 0.0 };
    double spread_       { 0.0 };
    double driftCents_   { 0.0 };

    double normalisation_ { 1.0 };

    std::array<Oscillator, kMaxVoices> voices_ {};
    std::array<double, kMaxVoices> increments_ {};
    std::array<double, kMaxVoices> gainL_ {};
    std::array<double, kMaxVoices> gainR_ {};

    /// Per-copy offsets and gains -- see `setRankOffsets`. The gains start at
    /// one rather than zero, so a default-constructed bank is the bank this
    /// class has always been.
    std::array<double, kMaxVoices> rankCents_ {};
    std::array<double, kMaxVoices> rankGains_ { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

    std::array<double, kMaxVoices> drift_ {};
    std::array<double, kMaxVoices> driftTarget_ {};
    double driftCoefficient_ { 0.0 };
    int    driftCountdown_   { 0 };

    /// Samples until the next drift step and increment push. Zero forces one
    /// on the next sample, which is how control changes land immediately.
    int    incrementCountdown_ { 0 };

    std::uint64_t seed_ { 0x5bf03635c1e5a2b3ull };

    /// Folded into the seed for the drift stream, so the two streams are
    /// unrelated. `seed()` sets the low bit, so any value will do here.
    static constexpr std::uint64_t kDriftSalt = 0xd1b54a32d192ed03ull;

    /// Two streams on purpose -- see the header. The scatter's is re-seeded on
    /// every cold note; the drift's is seeded once, at `reset`, and never by a
    /// note.
    SmallRandom phaseRandom_;
    SmallRandom driftRandom_;
};

} // namespace tezla::dsp
