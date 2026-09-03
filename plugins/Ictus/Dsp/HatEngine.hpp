// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The hat engine -- one hit of a metal instrument: closed hat, open hat, and
// with the ratios spread out, something more like a ride or a piece of scrap.
//
// A cymbal has no harmonic series. Its partials are set by the shape of a
// stiff, irregular plate and land where they land, so the way to synthesise
// one is to sum a handful of oscillators whose frequencies are deliberately
// incommensurate and then to filter the result rather than to tune it
// (Reid, "Synthesizing Percussion", Sound On Sound: why cymbals and hats
// are unpitched; Clark, the Nord Modular percussion chapter: metal as many
// incommensurate oscillators through a band-pass; both read first-hand, see
// docs/DSP-REFERENCES.md).
//
// The generator here is six pulses through a pair of band-passes, which is
// the topology analysed in Werner, Abel and Smith, "The TR-808 Cymbal: a
// Physically-Informed, Circuit-Bendable Digital Model" (ICMC/SMC 2014,
// CC BY 3.0, read first-hand). Two numbers are TAKEN from that paper rather
// than derived, under CLAUDE.md section 9's rule that a published measurement
// no measurement of ours could check is better copied than approximated:
//
//   * `kMetalHz` -- the six oscillator frequencies of the analysed circuit,
//     205.3, 369.6, 304.4, 522.7 Hz and the two tunable ones at nominally
//     800 and 540 Hz. They become the *Metal* ratio set, sorted ascending and
//     divided by the lowest so that Tune moves the whole set.
//   * `kDutyCycle` = 0.4798, the measured duty of those rectangular waves,
//     and `kUpperBandRatio` = 7100 / 3440, the spacing of the two band-passes
//     the paper's generator sums into.
//
// Nothing else comes from it, and no control, preset or set is named after
// any product (CLAUDE.md section 2.1): the set is *Metal*.
//
// The paper renders at 4x oversampling because rectangular waves alias.
// Ictus runs the whole instrument inside the oversampled section AND uses
// polyBLEP pulses (`dsp::Oscillator`), so the hat is band-limited twice over
// -- a hat is the brightest thing in the kit and the one most likely to
// betray a corner cut (CLAUDE.md section 7).
//
// The controls, and what each is for:
//
//   Tune        where the whole set of six sits.
//   Harmonics   a continuous position along the ratio-set list, morphing
//               geometrically between adjacent sets by rank in frequency, so
//               a partial glides from one set's rank to the next's rather
//               than jumping. Stored as an ABSOLUTE position 0..7 with the
//               positions past the last set clamped, so appending a set later
//               never repoints a saved value (CLAUDE.md section 8).
//   Spread      pulls the six apart against each other -- the set's character
//               loosened, from tight and metallic to wide and trashy.
//   Colour      the lower band-pass's centre; the upper follows at the
//               paper's ratio, and a high-pass below keeps the pulses' own
//               fundamentals out of the hat.
//   Air         seeded noise into the same filters, so it is part of the
//               instrument rather than hiss laid over it. Skipped entirely at
//               0 -- a cost branch, not an exactness one: the multiply by
//               zero is already exact, and a break-check proved the branch
//               changes no sample. Spread's branch is the other kind, and
//               does change samples: exp2(0) is 1.0 but the multiply is not
//               free of rounding at every ratio.
//   Decay       one per pad: the closed hat's and the open hat's, so the two
//               pads are one instrument struck two ways.
//
// EVERYTHING IS SNAPSHOTTED AT `start()`, as in the kick and the snare, and
// the amplitude envelope is killed the moment it reaches its zero sustain so
// that retirement is exact and the pad's activity count is honest.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Oscillator.hpp>
#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/dsp/UnisonBank.hpp>

namespace tezla::ictus {

/// Every hat control. One set is shared by the closed and open pads -- they
/// are the same pair of cymbals -- with a decay each.
struct HatSettings
{
    double tuneHz { 205.3 };             ///< where the lowest partial sits, 60..1200 Hz
    double harmonics { 0.0 };            ///< position along the ratio-set list, 0..7
    double spread { 0.0 };               ///< pulls the six apart, 0..1
    double colourHz { 3440.0 };          ///< the lower band-pass's centre, 800..12000 Hz
    double air { 0.0 };                  ///< seeded noise through the same filters, 0..1

    double decayClosedSeconds { 0.055 }; ///< the closed pad's T60-ish fall, 0.01..0.3
    double decayOpenSeconds { 0.45 };    ///< the open pad's, 0.1..2

    double level { 0.8 };                ///< 0..1
    bool   choke { true };               ///< a closed hit silences the open pad

    // ---- velocity amounts, all of the form x * ((1 - a) + a * v) --------
    double velocityLevel { 1.0 };
    double velocityDecay { 0.3 };
    double velocityColour { 0.4 };
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

    /// The high-pass under the two bands, as a fraction of Colour. It is what
    /// keeps the six pulses' own fundamentals -- 205 Hz and up -- out of an
    /// instrument that should have no body at all.
    static constexpr double kHighpassFraction = 0.5;

    /// The band-passes' Q, and the high-pass's.
    ///
    /// Named as Q rather than as the filter's control, because the control is
    /// geometric and 0.9 on it is **Q 250** -- two whistles, not a hat. A hat
    /// is a wide band of metal: Q 1.2 is about an octave, which passes the
    /// partials and their beating rather than one of them.
    static constexpr double kBandQ = 1.2;
    static constexpr double kHighpassQ = 0.707;

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

    /// Air's level against the six pulses, chosen so that Air 1 is noise and
    /// metal in comparable measure rather than noise on top of a whisper.
    static constexpr double kAirGain = 0.6;

    /// The output trim: six pulses at +-1 sum to +-6 before the filters, so
    /// the sum is normalised and this is what puts a default hit near full
    /// scale (measured, `tezla-measure ictus` table 3).
    static constexpr double kOutputGain = 2.2;

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

        lowBand_.prepare (rate_);
        lowBand_.setMode (dsp::SvfMode::bandpass);
        lowBand_.setResonance (dsp::SvfFilter::resonanceForQ (kBandQ));

        highBand_.prepare (rate_);
        highBand_.setMode (dsp::SvfMode::bandpass);
        highBand_.setResonance (dsp::SvfFilter::resonanceForQ (kBandQ));

        highpass_.prepare (rate_);
        highpass_.setMode (dsp::SvfMode::highpass);
        highpass_.setResonance (dsp::SvfFilter::resonanceForQ (kHighpassQ));

        envelope_.prepare (rate_);

        reset();
    }

    void reset() noexcept
    {
        active_ = false;
        airOn_ = false;

        for (auto& oscillator : oscillators_)
            oscillator.reset (0.0);

        lowBand_.reset();
        highBand_.reset();
        highpass_.reset();
        envelope_.kill();
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

        // ---- the filters ----
        const double colour = scaled (std::clamp (s.colourHz, 800.0, std::min (12000.0, rate_ * 0.4)),
                                      s.velocityColour);

        lowBand_.setCutoffHz (colour);
        highBand_.setCutoffHz (colour * kUpperBandRatio);
        highpass_.setCutoffHz (colour * kHighpassFraction);

        // ---- air ----
        air_ = std::clamp (s.air, 0.0, 1.0);
        airOn_ = ! dsp::isExactlyZero (air_);

        if (airOn_)
            random_.seed (seed ^ kAirSalt);

        // ---- the envelope ----
        const double decay = open ? std::clamp (s.decayOpenSeconds, 0.1, 2.0)
                                  : std::clamp (s.decayClosedSeconds, 0.01, 0.3);

        envelope_.setAttackSeconds (0.0);
        envelope_.setAttackTension (0.0);
        envelope_.setHoldSeconds (0.0);
        envelope_.setDecaySeconds (scaled (decay, s.velocityDecay));
        envelope_.setDecayTension (1.0);
        envelope_.setSustain (0.0);
        envelope_.noteOn();

        gain_ = scaled (std::clamp (s.level, 0.0, 1.0), s.velocityLevel) * kOutputGain;

        active_ = true;

        if (samplesToBoundary > 0)
            advanceControl (samplesToBoundary);
    }

    /// Nothing moves inside a hat hit: the partials, the filters and the
    /// envelope's shape are all fixed at note-on. The tick is here because
    /// the pad calls it, and it is where a later phase's per-chunk work
    /// would go.
    void advanceControl (int) noexcept {}

    /// A hat is a one-shot; a note-off does not shorten it. The open pad is
    /// silenced by the closed pad's choke instead, which is what a foot on
    /// the pedal does.
    void release() noexcept {}

    /// One internal sample. Exactly 0.0 once the envelope has landed.
    [[nodiscard]] double process() noexcept
    {
        if (! active_)
            return 0.0;

        const double env = envelope_.process();

        // Sustain is 0, so arriving there IS the end (the zombie lesson,
        // CLAUDE.md section 7): killed here, and the next call returns an
        // exact zero rather than running six oscillators for nothing.
        if (envelope_.getStage() == dsp::AdsrStage::sustain)
        {
            envelope_.kill();
            active_ = false;
            return 0.0;
        }

        double sum = 0.0;

        for (auto& oscillator : oscillators_)
            sum += oscillator.advance();

        sum *= 1.0 / static_cast<double> (kOscillators);

        // Air joins the metal BEFORE the filters, so it is coloured by the
        // same bands and belongs to the instrument.
        if (airOn_)
            sum += air_ * kAirGain * random_.bipolar();

        const double bands = lowBand_.process (sum) + highBand_.process (sum);

        return highpass_.process (bands) * env * gain_;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// The six partials of the hit that is sounding, in Hz -- what the
    /// panel's picture draws and the measurement checks.
    [[nodiscard]] double getPartialHz (int index) const noexcept
    {
        return index >= 0 && index < kOscillators ? partials_[index] : 0.0;
    }

    [[nodiscard]] double getSampleRate() const noexcept { return rate_; }

private:
    double rate_ { 48000.0 };
    bool active_ { false };

    dsp::Oscillator oscillators_[kOscillators];
    double partials_[kOscillators] {};

    dsp::SvfFilter lowBand_;
    dsp::SvfFilter highBand_;
    dsp::SvfFilter highpass_;

    dsp::Adsr envelope_;

    dsp::SmallRandom random_;
    bool airOn_ { false };
    double air_ { 0.0 };

    double gain_ { 1.0 };
};

} // namespace tezla::ictus
