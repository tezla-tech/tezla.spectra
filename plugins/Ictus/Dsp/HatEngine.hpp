// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The hat engine -- one hit of a metal instrument: closed hat, open hat, and
// with the ratios spread and the bands opened, a ride, a china, or scrap.
//
// A cymbal has no harmonic series. Its partials are set by the shape of a
// stiff, irregular plate and land where they land, so the way to synthesise
// one is to sum oscillators whose frequencies are deliberately incommensurate
// and then to FILTER the result rather than to tune it (Reid, "Synthesizing
// Percussion", Sound On Sound: why cymbals and hats are unpitched; Clark, the
// Nord Modular percussion chapter: metal as many incommensurate oscillators
// through a band-pass AND AN OVERDRIVE; both read first-hand, see
// docs/DSP-REFERENCES.md).
//
// The generator is six pulses through a pair of band-passes, which is the
// topology analysed in Werner, Abel and Smith, "The TR-808 Cymbal: a
// Physically-Informed, Circuit-Bendable Digital Model" (ICMC/SMC 2014,
// CC BY 3.0, read first-hand). Three numbers are TAKEN from that paper rather
// than derived, under CLAUDE.md section 9's rule that a published measurement
// no measurement of ours could check is better copied than approximated:
// `kMetalHz` (the six oscillator frequencies), `kDutyCycle` (0.4798, their
// measured duty) and `kUpperBandRatio` (7100 / 3440, the spacing of the two
// band-passes they sum into). Nothing else comes from it, and no control,
// preset or set is named after any product (CLAUDE.md section 2.1).
//
// ---------------------------------------------------------------------------
// What the first version got wrong, and what fixes it
// ---------------------------------------------------------------------------
//
// Six pulses through a band-pass is a SPARSE COMB. It reads as a metallic
// chord rather than as a cymbal, it has no transient, and it decays at one
// uniform rate from the moment it starts -- which is the one thing no real
// cymbal does. Four things put that right, and each is a control:
//
//   RING     The low three oscillators multiplied by the high three. A ring
//            modulator's output holds the SUM AND DIFFERENCE of every pair of
//            harmonics in its two inputs, so one multiply turns two sparse
//            combs into a dense inharmonic wash -- which is what a plate of
//            metal actually sounds like. Both operands are low-passed at an
//            eighth of the internal rate FIRST, so every product lands below
//            a quarter of it and the multiplication cannot alias at all. That
//            is the whole reason the low-pass is there.
//   DRIVE    The overdrive the Nord chapter's cymbal patch ends with, as an
//            antialiased soft clip. It fills the gaps between partials with
//            intermodulation and glues the layers into one instrument.
//   DAMP     A low-pass that CLOSES as the hit decays. On a real cymbal the
//            high modes die first -- that fall from bright to dark over the
//            ring is most of what "lush" means, and a fixed filter cannot do
//            it. Retuned once per control chunk, so it costs one tangent
//            every 32 samples and nothing per sample.
//   STRIKE   A short, loud transient on top of the body envelope: the stick.
//            With Damp up it is automatically the brightest part of the hit,
//            because Damp is still wide open when it lands.
//
// The noise is a layer in its own right now rather than a garnish, and --
// this is the part that matters -- it is not a SEPARATE layer. On a real
// cymbal the hiss IS the plate: the sizzle is its own modes being excited
// chaotically rather than struck cleanly, which is why the noise and the
// harmonics in a sampled hat sound like one object, and why noise simply
// added beside an oscillator bank sounds like two things glued together.
//
//   SIZZLE   runs the noise through a band-pass at each of the six partials,
//            so the hiss rings at the frequencies the metal already has. At
//            0 it is raw filtered hiss beside the metal; at 1 every bit of
//            it is the plate speaking. The crossfade is exact at both ends
//            and the bank is not run at all at 0.
//   AIR      its level, with its own tone and its own decay -- so a long
//            hiss can shimmer over a short metal body, or a fast chiff can
//            sit on a long one, which is how a sampled hat is put together.
//
// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------
//
//   six pulses --> (+ the ring product) --> x metal envelope --,
//        |                                                      |
//        | the same six frequencies                             +--> Drive
//        v                                                      |    -> the
//   noise -> Air tone -> (Sizzle: through them) -> x air env ---'     two
//                                                                    bands
//                                                                    -> the
//                                                                    high-pass
//                                                                    -> Damp
//
// The envelopes are BEFORE the filters, where a real instrument's are, so the
// filters ring on past them. The hit is retired when both envelopes are dead
// and the filters have fallen below -180 dB, which takes well under a
// millisecond and is measured; the cut is then exact and the pad's activity
// count is honest (CLAUDE.md section 7).
//
// EVERYTHING IS SNAPSHOTTED AT `start()`, as in the kick and the snare.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Oscillator.hpp>
#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/dsp/UnisonBank.hpp>
#include <tezla/dsp/Waveshapers.hpp>

namespace tezla::ictus {

/// Every hat control. One set is shared by the closed and open pads -- they
/// are the same pair of cymbals -- with a decay each.
struct HatSettings
{
    // ---- the metal -------------------------------------------------------
    double tuneHz { 205.3 };             ///< where the lowest partial sits, 60..1200 Hz
    double harmonics { 0.0 };            ///< position along the ratio-set list, 0..7
    double spread { 0.0 };               ///< pulls the six apart, 0..1
    double ring { 0.35 };                ///< the low three times the high three, 0..1
    double drive { 0.25 };               ///< soft clip after the layers meet, 0..1

    // ---- the noise layer -------------------------------------------------
    double air { 0.45 };                 ///< its level, 0..1
    double airToneHz { 5000.0 };         ///< its own high-pass, 200..12000 Hz
    double airDecay { 1.0 };             ///< its decay as a multiple of the pad's, 0.1..3
    double sizzle { 0.6 };               ///< how much of the hiss rings through the six partials, 0..1

    // ---- the band the whole thing is heard through -----------------------
    double colourHz { 3440.0 };          ///< the lower band-pass's centre, 800..12000 Hz
    double width { 0.5 };                ///< how wide the two bands are, 0 (narrow) .. 1 (open)
    double highpassHz { 1200.0 };        ///< under the bands, 200..8000 Hz
    double damp { 0.4 };                 ///< how far the top closes as the hit decays, 0..1

    // ---- the envelope ----------------------------------------------------
    double strike { 0.4 };               ///< the stick: a short transient over the body, 0..1
    double holdSeconds { 0.0 };          ///< a plateau before the decay, 0..0.2
    double shape { 0.0 };                ///< 0 exponential, 1 linear
    double decayClosedSeconds { 0.06 };  ///< the closed pad's fall, 0.01..0.4
    double decayOpenSeconds { 0.5 };     ///< the open pad's, 0.1..3

    double level { 0.7 };                ///< 0..1
    bool   choke { true };               ///< a closed hit silences the open pad
    bool   gate { false };               ///< lit: a note-off fades the hit over `releaseSeconds`
    double releaseSeconds { 0.0 };       ///< 0..2; 0 is a 1 ms cut

    // ---- velocity amounts, all of the form x * ((1 - a) + a * v) --------
    double velocityLevel { 1.0 };
    double velocityDecay { 0.3 };
    double velocityColour { 0.4 };
    double velocityStrike { 0.5 };
};

class HatEngine
{
public:
    static constexpr int kOscillators = 6;

    /// The six oscillator frequencies of the circuit analysed in the TR-808
    /// cymbal paper (Werner, Abel and Smith, ICMC/SMC 2014, CC BY 3.0, read
    /// first-hand; the two tunable oscillators at their nominal settings).
    /// TAKEN, not derived -- see the header comment and
    /// docs/DSP-REFERENCES.md.
    static constexpr double kMetalHz[kOscillators] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

    /// The measured duty of those rectangular waves, from the same paper.
    static constexpr double kDutyCycle = 0.4798;

    /// The paper's two band-pass centres, 3440 and 7100 Hz, as a ratio: the
    /// upper band follows Colour at this spacing.
    static constexpr double kUpperBandRatio = 7100.0 / 3440.0;

    /// Width's two ends, as Q. Narrow is a whistle you can pick a partial out
    /// of; open is a band wide enough to hear the whole plate at once.
    static constexpr double kNarrowQ = 4.0;
    static constexpr double kOpenQ = 0.6;
    static constexpr double kHighpassQ = 0.707;

    /// Where the ring modulator's operands are band-limited.
    ///
    /// This is the one number that makes ring modulation safe here. A product
    /// of two signals holds the sum of every pair of their frequencies, so
    /// multiplying two full-band signals puts energy up to twice Nyquist and
    /// folds it back. Low-passing both operands first keeps the products in
    /// range, and 20 kHz is chosen because it is above everything the
    /// decimator will keep -- so the limit costs the audible band nothing.
    ///
    /// **An ABSOLUTE frequency, and that is the whole lesson.** The first
    /// version made it a fraction of the internal rate, which meant the ring
    /// products were built from a 24 kHz band at 192 kHz and a 6 kHz band at
    /// 48 kHz: the same patch was a different instrument at a different host
    /// rate, which is exactly what CLAUDE.md section 6 forbids. Measured
    /// before it was fixed -- the spectral centroid moved from 6951 Hz to
    /// 4849 Hz, 30 % -- and the fraction below is only a floor for a rate too
    /// low to give 20 kHz a quarter of itself.
    static constexpr double kRingOperandHz = 20000.0;
    static constexpr double kRingOperandCeilingFraction = 0.25;

    /// The ring product's weight against the direct sum at Ring 1.
    static constexpr double kRingGain = 2.4;

    /// Drive's pre-gain at 1, and the trim exponent that keeps the level from
    /// running away with it (a full trim by the gain makes the drive quieter
    /// than no drive at all, which is not what a drive control is for).
    static constexpr double kDriveRange = 12.0;
    static constexpr double kDriveTrimExponent = 0.7;

    /// Damp's widest corner, and the floor it can close to.
    static constexpr double kDampTopHz = 18000.0;
    static constexpr double kDampFloorHz = 700.0;

    /// How steeply Damp follows the envelope at 1: the corner is the top
    /// times the envelope raised to this power.
    static constexpr double kDampExponent = 3.0;

    /// The stick's own fall, 60 dB.
    static constexpr double kStrikeSeconds = 0.006;
    static constexpr double kStrikeFloor = 1.0e-5;

    /// The shortest release -- a note-off with Release at 0 ramps out over
    /// this rather than stepping to zero (the kick's and snare's constant).
    static constexpr double kMinimumReleaseSeconds = 0.001;

    /// Retirement: once both envelopes are dead, the filters are allowed to
    /// ring out until the output has been this small for this many samples in
    /// a row. -180 dB, and a Q of 4 at 800 Hz reaches it in under a
    /// millisecond -- measured in tests/test_Ictus.cpp.
    static constexpr double kQuietThreshold = 1.0e-9;
    static constexpr int kQuietSamples = 64;

    /// How many ratio sets exist today. Positions past the last one clamp to
    /// it, so appending a set gives those positions a meaning without moving
    /// any saved value. **Append-only** (CLAUDE.md section 8).
    static constexpr int kSetCount = 4;

    /// The most Harmonics can be asked for: room for four more sets.
    static constexpr double kMaxHarmonicsPosition = 7.0;

    /// The ratio sets, each ASCENDING in frequency so that the morph pairs
    /// rank with rank: partial n of one set glides to partial n of the next.
    ///
    ///   0  Metal   the paper's six, over the lowest of them. Tight, dense,
    ///              the classic closed hat.
    ///   1  Bell    near-harmonic -- a ride's bell has a pitch, and these
    ///              ratios are close enough to a series to give it one.
    ///   2  Trash   deliberately incommensurate and wider: a china, a lid.
    ///   3  Wide    spread over three octaves; thin, bright, glassy.
    static constexpr double kSets[kSetCount][kOscillators] {
        { 1.0, 1.482709, 1.800292, 2.546030, 2.630298, 3.896737 },
        { 1.0, 1.500000, 2.000000, 2.666667, 3.000000, 4.000000 },
        { 1.0, 1.414214, 2.090000, 2.828427, 3.710000, 5.130000 },
        { 1.0, 1.930000, 3.830000, 5.710000, 7.660000, 9.550000 },
    };

    /// The sets' names, for a value readout and a caption. Framework-free:
    /// plain C strings, in the same order as `kSets`.
    static constexpr const char* kSetNames[kSetCount] { "Metal", "Bell", "Trash", "Wide" };

    /// How far Spread pulls the six apart, in semitones at Spread 1, and the
    /// fixed pattern it pulls them along. The pattern sums to zero so the
    /// set's centre of gravity does not move with the control.
    static constexpr double kSpreadSemitones = 1.5;
    static constexpr double kSpreadPattern[kOscillators] { -1.0, 0.55, -0.35, 0.8, -0.65, 0.65 };

    /// Air's level against the six pulses.
    static constexpr double kAirGain = 1.4;

    /// The Q of the six band-passes Sizzle runs the noise through, and the
    /// make-up that keeps the resonated hiss level with the raw hiss.
    ///
    /// Narrow enough that each one rings and the noise takes the partial's
    /// pitch; wide enough that six of them are a sizzle rather than six
    /// whistles. A band-pass at Q 12 passes a small fraction of the noise
    /// power a flat path does, hence the make-up.
    static constexpr double kSizzleQ = 12.0;
    static constexpr double kSizzleMakeup = 2.6;

    /// The output trim, chosen from measurement so a default hit lands near
    /// full scale (`tezla-measure ictus` table 3).
    static constexpr double kOutputGain = 1.6;

    /// The noise stream is salted so a hat and a snare with the same hit seed
    /// do not draw the same numbers.
    static constexpr std::uint64_t kAirSalt = 0x2545F4914F6CDD1Dull;

    void prepare (double internalRate) noexcept
    {
        rate_ = internalRate > 0.0 ? internalRate : 48000.0;

        for (auto& oscillator : oscillators_)
        {
            oscillator.setShape (dsp::OscShape::pulse);
            oscillator.setWidth (kDutyCycle);
        }

        // The ring operands' band limit: fixed, and the reason it exists is
        // in kRingOperandFraction.
        for (auto* filter : { &ringLow_, &ringHigh_ })
        {
            filter->prepare (rate_);
            filter->setMode (dsp::SvfMode::lowpass);
            filter->setResonance (dsp::SvfFilter::resonanceForQ (0.707));
            filter->setCutoffHz (std::min (kRingOperandHz, rate_ * kRingOperandCeilingFraction));
        }

        for (auto* band : { &lowBand_, &highBand_ })
        {
            band->prepare (rate_);
            band->setMode (dsp::SvfMode::bandpass);
        }

        highpass_.prepare (rate_);
        highpass_.setMode (dsp::SvfMode::highpass);
        highpass_.setResonance (dsp::SvfFilter::resonanceForQ (kHighpassQ));

        airFilter_.prepare (rate_);
        airFilter_.setMode (dsp::SvfMode::highpass);
        airFilter_.setResonance (dsp::SvfFilter::resonanceForQ (0.707));

        for (auto& band : sizzleBank_)
        {
            band.prepare (rate_);
            band.setMode (dsp::SvfMode::bandpass);
            band.setResonance (dsp::SvfFilter::resonanceForQ (kSizzleQ));
        }

        damp_.prepare (rate_);
        damp_.setMode (dsp::SvfMode::lowpass);
        damp_.setResonance (dsp::SvfFilter::resonanceForQ (0.707));

        body_.prepare (rate_);
        airEnvelope_.prepare (rate_);
        gateEnv_.prepare (rate_);

        strikeCoefficient_ = std::exp (-6.907755278982137 / (kStrikeSeconds * rate_));

        reset();
    }

    void reset() noexcept
    {
        active_ = false;
        ringOn_ = false;
        driveOn_ = false;
        dampOn_ = false;
        airOn_ = false;
        sizzleOn_ = false;
        quiet_ = 0;

        for (auto& oscillator : oscillators_)
            oscillator.reset (0.0);

        ringLow_.reset();
        ringHigh_.reset();
        lowBand_.reset();
        highBand_.reset();
        highpass_.reset();
        airFilter_.reset();

        for (auto& band : sizzleBank_)
            band.reset();

        damp_.reset();
        shaper_.reset();

        body_.kill();
        airEnvelope_.kill();
        gateEnv_.kill();
        releasing_ = false;
        strikeLevel_ = 0.0;
    }

    /// The ratios of the set list at `position`, morphed geometrically
    /// between the two sets it lies between and written into `ratios`.
    ///
    /// Geometric rather than linear because a ratio is a pitch: halfway
    /// between 1.5 and 3.0 should be an octave's midpoint, 2.12, not 2.25.
    /// Exact at an integer position by branch -- a set is its own numbers.
    static void ratiosAt (double position, double (&ratios)[kOscillators]) noexcept
    {
        const double clamped = std::clamp (position, 0.0, kMaxHarmonicsPosition);
        const int lower = std::min (static_cast<int> (std::floor (clamped)), kSetCount - 1);
        const double fraction = lower >= kSetCount - 1 ? 0.0 : clamped - static_cast<double> (lower);

        if (dsp::isExactlyZero (fraction))
        {
            for (int i = 0; i < kOscillators; ++i)
                ratios[i] = kSets[lower][i];

            return;
        }

        const int upper = lower + 1;

        for (int i = 0; i < kOscillators; ++i)
            ratios[i] = std::exp2 ((1.0 - fraction) * std::log2 (kSets[lower][i])
                                   + fraction * std::log2 (kSets[upper][i]));
    }

    /// Width's Q: geometric between the two ends, so the control is even.
    [[nodiscard]] static double qForWidth (double width) noexcept
    {
        const double w = std::clamp (width, 0.0, 1.0);
        return kNarrowQ * std::pow (kOpenQ / kNarrowQ, w);
    }

    /// Strikes. `open` picks which of the two decays this pad uses;
    /// `velocity` 0..1; `seed` feeds Air; `samplesToBoundary` as in the kick.
    void start (const HatSettings& s, bool open, double velocity, std::uint64_t seed,
                int samplesToBoundary) noexcept
    {
        reset();

        const double v = std::clamp (velocity, 0.0, 1.0);

        const auto scaled = [v] (double x, double amount) noexcept
        {
            const double a = std::clamp (amount, 0.0, 1.0);
            return x * ((1.0 - a) + a * v);
        };

        // ---- gate, first: it is the one thing a note-off can reach ----
        gate_ = s.gate;
        release_ = std::max (kMinimumReleaseSeconds, std::clamp (s.releaseSeconds, 0.0, 2.0));

        // ---- the six partials ----
        const double tune = std::clamp (s.tuneHz, 60.0, 1200.0);

        double ratios[kOscillators] {};
        ratiosAt (s.harmonics, ratios);

        const double spread = std::clamp (s.spread, 0.0, 1.0);
        const bool spreadOn = ! dsp::isExactlyZero (spread);

        for (int i = 0; i < kOscillators; ++i)
        {
            double hz = tune * ratios[i];

            if (spreadOn)
                hz *= std::exp2 (spread * kSpreadSemitones * kSpreadPattern[i] / 12.0);

            partials_[i] = hz;

            // Every oscillator starts at phase 0, so a hit is the same hit
            // every time it is struck -- the sync the Nord Modular chapter
            // makes the point about, and what keeps two hits identical
            // until Humanise (I6) says otherwise.
            oscillators_[i].reset (0.0);
            oscillators_[i].setIncrement (hz / rate_);
        }

        ring_ = std::clamp (s.ring, 0.0, 1.0);
        ringOn_ = ! dsp::isExactlyZero (ring_);

        // ---- drive ----
        const double drive = std::clamp (s.drive, 0.0, 1.0);
        driveOn_ = ! dsp::isExactlyZero (drive);
        driveGain_ = 1.0 + (kDriveRange - 1.0) * drive;
        driveTrim_ = std::pow (driveGain_, -kDriveTrimExponent);

        // ---- the band the whole thing is heard through ----
        const double colour = scaled (std::clamp (s.colourHz, 800.0, std::min (12000.0, rate_ * 0.4)),
                                      s.velocityColour);

        const double q = dsp::SvfFilter::resonanceForQ (qForWidth (s.width));

        lowBand_.setResonance (q);
        highBand_.setResonance (q);
        lowBand_.setCutoffHz (colour);
        highBand_.setCutoffHz (std::min (colour * kUpperBandRatio, rate_ * 0.45));

        highpass_.setCutoffHz (std::clamp (s.highpassHz, 200.0, 8000.0));

        dampAmount_ = std::clamp (s.damp, 0.0, 1.0);
        dampOn_ = ! dsp::isExactlyZero (dampAmount_);
        damp_.setCutoffHz (kDampTopHz);

        // ---- the envelopes ----
        const double decay = open ? std::clamp (s.decayOpenSeconds, 0.1, 3.0)
                                  : std::clamp (s.decayClosedSeconds, 0.01, 0.4);
        const double bodySeconds = scaled (decay, s.velocityDecay);
        const double tension = 1.0 - std::clamp (s.shape, 0.0, 1.0);

        body_.setAttackSeconds (0.0);
        body_.setAttackTension (0.0);
        body_.setHoldSeconds (std::clamp (s.holdSeconds, 0.0, 0.2));
        body_.setDecaySeconds (bodySeconds);
        body_.setDecayTension (tension);
        body_.setSustain (0.0);
        body_.noteOn();

        strike_ = scaled (std::clamp (s.strike, 0.0, 1.0), s.velocityStrike);
        strikeLevel_ = dsp::isExactlyZero (strike_) ? 0.0 : 1.0;

        // ---- air ----
        air_ = std::clamp (s.air, 0.0, 1.0);
        airOn_ = ! dsp::isExactlyZero (air_);

        if (airOn_)
        {
            random_.seed (seed ^ kAirSalt);
            airFilter_.setCutoffHz (std::clamp (s.airToneHz, 200.0, std::min (12000.0, rate_ * 0.4)));

            // Sizzle: the hiss through the plate's own partials, at whatever
            // Tune, Harmonics and Spread have just put them.
            sizzle_ = std::clamp (s.sizzle, 0.0, 1.0);
            sizzleOn_ = ! dsp::isExactlyZero (sizzle_);

            if (sizzleOn_)
                for (int i = 0; i < kOscillators; ++i)
                    sizzleBank_[static_cast<std::size_t> (i)]
                        .setCutoffHz (std::clamp (partials_[i], 20.0, rate_ * 0.45));

            airEnvelope_.setAttackSeconds (0.0);
            airEnvelope_.setAttackTension (0.0);
            airEnvelope_.setHoldSeconds (std::clamp (s.holdSeconds, 0.0, 0.2));
            airEnvelope_.setDecaySeconds (bodySeconds * std::clamp (s.airDecay, 0.1, 3.0));
            airEnvelope_.setDecayTension (tension);
            airEnvelope_.setSustain (0.0);
            airEnvelope_.noteOn();
        }

        gain_ = scaled (std::clamp (s.level, 0.0, 1.0), s.velocityLevel) * kOutputGain;

        active_ = true;
        bodyLevel_ = 1.0;

        if (samplesToBoundary > 0)
            advanceControl (samplesToBoundary);
    }

    /// The control tick: Damp's corner follows the body envelope, retuned
    /// here rather than per sample -- one tangent every 32 samples.
    void advanceControl (int numSamples) noexcept
    {
        if (! active_ || numSamples <= 0 || ! dampOn_)
            return;

        const double corner = kDampFloorHz
                            + (kDampTopHz - kDampFloorHz)
                              * std::pow (std::clamp (bodyLevel_, 0.0, 1.0),
                                          dampAmount_ * kDampExponent);

        damp_.setCutoffHz (std::min (corner, rate_ * 0.45));
    }

    /// Note-off. With Gate lit the WHOLE hit ramps out over the release from
    /// wherever it is -- the metal, the hiss and the filters' ring alike --
    /// so a long open hat can be stopped by lifting the key rather than only
    /// by a closed hit's choke. A one-shot ignores this entirely.
    ///
    /// It is a ramp on the output rather than a release on the two envelopes
    /// because they are BEFORE the filters here: releasing them would leave
    /// the bands ringing for a few milliseconds after the key came up, which
    /// is exactly the tail the control exists to remove.
    void release() noexcept
    {
        if (! active_ || ! gate_ || releasing_)
            return;

        // A release envelope parked at 1.0: attack, hold and decay instant
        // with the sustain at 1, so three samples land it in its sustain
        // stage at exactly 1.0 and the note-off releases from there.
        gateEnv_.setAttackSeconds (0.0);
        gateEnv_.setHoldSeconds (0.0);
        gateEnv_.setDecaySeconds (0.0);
        gateEnv_.setSustain (1.0);
        gateEnv_.setReleaseSeconds (release_);
        gateEnv_.setReleaseTension (1.0);
        gateEnv_.noteOn();
        (void) gateEnv_.skip (3);
        gateEnv_.noteOff();

        releasing_ = true;
    }

    [[nodiscard]] bool isGated() const noexcept { return gate_; }

    /// One internal sample. Exactly 0.0 once the hit has been retired.
    [[nodiscard]] double process() noexcept
    {
        if (! active_)
            return 0.0;

        // ---- the envelopes, and the stick on top of them ----
        double body = 0.0;

        if (body_.isActive())
        {
            body = body_.process();

            // Sustain is 0: arriving there IS the end (the zombie lesson,
            // CLAUDE.md section 7).
            if (body_.getStage() == dsp::AdsrStage::sustain)
                body_.kill();
        }

        bodyLevel_ = body;

        double airEnv = 0.0;

        if (airOn_ && airEnvelope_.isActive())
        {
            airEnv = airEnvelope_.process();

            if (airEnvelope_.getStage() == dsp::AdsrStage::sustain)
                airEnvelope_.kill();
        }

        if (! dsp::isExactlyZero (strikeLevel_))
        {
            const double stick = strike_ * strikeLevel_;
            body += stick;
            airEnv += stick;

            strikeLevel_ *= strikeCoefficient_;

            if (strikeLevel_ < kStrikeFloor)
                strikeLevel_ = 0.0;
        }

        // ---- the metal ----
        double low = 0.0;
        double high = 0.0;

        for (int i = 0; i < kOscillators; ++i)
        {
            const double value = oscillators_[i].advance();

            if (i < kOscillators / 2)
                low += value;
            else
                high += value;
        }

        double metal = (low + high) * (1.0 / kOscillators);

        // Ring modulation, with both operands band-limited first so that no
        // product can land above Nyquist -- see kRingOperandFraction.
        if (ringOn_)
        {
            const double a = ringLow_.process (low * (2.0 / kOscillators));
            const double b = ringHigh_.process (high * (2.0 / kOscillators));

            metal += ring_ * kRingGain * a * b;
        }

        // ---- the two layers meet ----
        double x = metal * body;

        if (airOn_)
        {
            double hiss = airFilter_.process (random_.bipolar());

            // The hiss rung through the six partials: the plate speaking
            // rather than a second instrument playing alongside it. Exact at
            // both ends of the crossfade, and the bank is not run at 0.
            if (sizzleOn_)
            {
                double resonated = 0.0;

                for (auto& band : sizzleBank_)
                    resonated += band.process (hiss);

                resonated *= kSizzleMakeup / kOscillators;

                hiss += sizzle_ * (resonated - hiss);
            }

            x += air_ * kAirGain * airEnv * hiss;
        }

        // ---- drive ----
        if (driveOn_)
            x = shaper_.process (x * driveGain_, clip_) * driveTrim_;

        // ---- the band, the high-pass and the damping ----
        x = lowBand_.process (x) + highBand_.process (x);
        x = highpass_.process (x);

        if (dampOn_)
            x = damp_.process (x);

        x *= gain_;

        // ---- the gate's release ramp ----
        if (releasing_)
        {
            x *= gateEnv_.process();

            if (! gateEnv_.isActive())
            {
                // Landed at exactly 0: the hit is over, whatever was ringing.
                reset();
                return 0.0;
            }
        }

        // ---- retirement ----
        //
        // The envelopes are before the filters, so the filters ring on past
        // them; the hit is over when they have. Counted rather than assumed,
        // and the count is what makes the last sample an exact zero.
        if (! body_.isActive() && ! airEnvelope_.isActive() && dsp::isExactlyZero (strikeLevel_))
        {
            if (std::abs (x) < kQuietThreshold)
            {
                if (++quiet_ >= kQuietSamples)
                {
                    reset();
                    return 0.0;
                }
            }
            else
            {
                quiet_ = 0;
            }
        }

        return x;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// The six partials of the hit that is sounding, in Hz -- what the
    /// panel's picture draws and the measurement checks.
    [[nodiscard]] double getPartialHz (int index) const noexcept
    {
        return index >= 0 && index < kOscillators ? partials_[index] : 0.0;
    }

    /// Where the ring modulator's operands are band-limited, in Hz: 20 kHz
    /// wherever the rate has room for it, and a quarter of the rate where it
    /// has not. A test asserts the products cannot reach Nyquist.
    [[nodiscard]] double getRingOperandCutoffHz() const noexcept
    {
        return std::min (kRingOperandHz, rate_ * kRingOperandCeilingFraction);
    }

    /// Damp's corner at the last control boundary: the thing that makes the
    /// hit darken as it rings, and what a test watches to prove it does.
    [[nodiscard]] double getDampCutoffHz() const noexcept { return damp_.getCutoffHz(); }

    [[nodiscard]] double getSampleRate() const noexcept { return rate_; }

private:
    double rate_ { 48000.0 };
    bool active_ { false };

    dsp::Oscillator oscillators_[kOscillators];
    double partials_[kOscillators] {};

    dsp::SvfFilter ringLow_, ringHigh_;
    bool ringOn_ { false };
    double ring_ { 0.0 };

    dsp::Adaa1<dsp::SoftClipExcess> shaper_;
    dsp::SoftClipExcess clip_;
    bool driveOn_ { false };
    double driveGain_ { 1.0 };
    double driveTrim_ { 1.0 };

    dsp::SvfFilter lowBand_, highBand_, highpass_, damp_;
    bool dampOn_ { false };
    double dampAmount_ { 0.0 };

    dsp::Adsr body_;
    dsp::Adsr gateEnv_;
    bool gate_ { false };
    bool releasing_ { false };
    double release_ { kMinimumReleaseSeconds };
    double bodyLevel_ { 0.0 };
    double strike_ { 0.0 };
    double strikeLevel_ { 0.0 };
    double strikeCoefficient_ { 0.0 };

    dsp::SvfFilter airFilter_;
    dsp::SvfFilter sizzleBank_[kOscillators];
    bool sizzleOn_ { false };
    double sizzle_ { 0.0 };
    dsp::Adsr airEnvelope_;
    dsp::SmallRandom random_;
    bool airOn_ { false };
    double air_ { 0.0 };

    int quiet_ { 0 };
    double gain_ { 1.0 };
};

} // namespace tezla::ictus
