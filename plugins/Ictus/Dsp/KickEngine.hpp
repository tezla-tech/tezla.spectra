// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The kick engine -- one hit of a synthesised bass drum.
//
// Built from the mechanisms the published analyses describe, never from a
// circuit's values (docs/DSP-REFERENCES.md, "Drum synthesis -- Ictus"):
//
//   * A phase-continuous sine body under TWO pitch drops, because the TR-808
//     analysis (Werner, Abel, Smith, DAFx-14) separates two effects that are
//     usually conflated: a fast jump in the attack -- "punchier and crisper",
//     not heard as pitch -- and a slow sigh through the decay, which is. Here
//     they are `Drop` (Start semitones above the landed pitch, over Drop time)
//     and `Sigh` (signed semitones over a longer time), two `TensionDrop`s
//     whose cents add. The 909's Tune Depth / Tune Decay are the same first
//     knob pair.
//   * Harmonics in PARALLEL: y = x + h (even * SoftEven(x) + (1-even) * SoftOdd(x)),
//     both through first-order ADAA. Parallel because both curves have no
//     linear term, so at h = 0 this is exactly x + 0 and a small signal gains
//     nothing but harmonics -- the "reads on a small speaker" control, and
//     the 909's diode clamp in spirit. The even curve sits on a DC pedestal,
//     so a per-hit DC blocker follows it, engaged only when it is.
//   * A Tone low-pass whose cutoff TRACKS the pitch (ratio x current Hz):
//     bright while the drop is high, pure once it has landed.
//   * The click: one mode of a `ModalResonator` (the beater's short ring) and
//     a seeded noise burst through a one-pole high-pass -- the 909's Attack
//     knob, and the thing velocity is most about. Shared with the snare's
//     crack as `ClickPair` (Click.hpp) since I3, bit-identically.
//   * Amplitude: an AHD envelope (`Adsr` with sustain 0, killed the moment it
//     lands, so the hit retires exactly) and a Tail -- a second, longer
//     envelope on the same body, mixed in by a lerp that is exact at 0.
//
// EVERYTHING IS SNAPSHOTTED AT `start()`. Nothing inside a hit re-reads a
// knob: a hit is a pure function of (settings, end pitch, velocity, seed),
// which is what makes bit-exact neutral a branch per hit, humanise a
// snapshot plus deviations, and a rendered hit equal to a played one.
//
// The pitch is updated once per control chunk and the phase increment is
// interpolated LINEARLY between exact values at the chunk's two ends. A
// staircase at 6 kHz would be audible grit on a 2 ms drop from +60
// semitones; the chord between two exact points misses the exponential by
// (dt^2 / 8) f'' -- 0.35 cents at the very start of the fastest drop, and
// nothing anywhere else. Because both ends are exact there is no lag, so
// the chunk grid can be the engine's rather than the hit's, and the block
// size cannot bend the sweep (CLAUDE.md section 7).
//
// Neutral is bit-exact by construction: Start 0, Sigh 0, Phase 0,
// Harmonics 0, Tail 0, Tone off, Click 0, Noise 0, Under 0, Knock 0 leaves
// sin(2 pi phase) * envelope * level and not one more operation.
//
// Two layers from the rig's fourth round (I4.4, "thicker"), both exact at 0:
//
//   * UNDER: a second sine an interval below the landed pitch -- an octave by
//     default -- locked to the body's phase increment, so it follows the drop
//     and the sigh at a fixed ratio and can never beat against the body. It
//     joins AFTER the harmonics and the tone filter: a clean sub under a
//     dirty punch, which is the layering trick (an 808 under a hard kick)
//     built in. Its own AHD -- an attack so it can bloom after the punch,
//     a decay as a multiple of the body's.
//   * KNOCK: a second, lower contact resonator, 150 to 800 Hz with its own
//     ring time -- the beater on the head that sampled kicks have and the
//     3 kHz click cannot give. One mode of a `ModalResonator`, cut exactly
//     after four ring times as the click is.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/ModalResonator.hpp>
#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/dsp/TensionDrop.hpp>
#include <tezla/dsp/Waveshapers.hpp>

#include "Click.hpp"

namespace tezla::ictus {

/// Every kick control, in the units the user thinks in. Copied into the hit
/// at note-on; see the header comment.
struct KickSettings
{
    // ---- pitch ----------------------------------------------------------
    double tuneHz { 50.0 };              ///< the landed pitch, 20..400 Hz
    bool   followKey { false };          ///< take the landed pitch from the MIDI note instead
    bool   noteSnap { false };           ///< snap Tune to the nearest degree of the tuning
    double startSemitones { 30.0 };      ///< the drop starts this far above the landed pitch, 0..60
    double dropSeconds { 0.03 };         ///< landing time of the drop, 0.002..0.2
    double dropCurve { 0.0 };            ///< the drop's shape: -1 a line (the laser), 0 exponential (exact), +1 hold-then-snap
    double sighSemitones { 1.5 };        ///< signed; the slow second drop, -12..12
    double sighSeconds { 0.5 };          ///< landing time of the sigh, 0.1..2
    double phaseDegrees { 0.0 };         ///< where the body starts its cycle, 0..90

    // ---- harmonics ------------------------------------------------------
    double harmonics { 0.0 };            ///< 0..1; exactly nothing at 0
    double even { 0.5 };                 ///< 0 = the odd curve only, 1 = the even curve only
    double dcBlockerHz { 10.0 };         ///< corner of the blocker the even curve needs, 5..40

    // ---- tone -----------------------------------------------------------
    bool   toneEnabled { false };        ///< off is a per-hit branch and is exact
    double toneRatio { 8.0 };            ///< cutoff = ratio x the current pitch, 1..64

    // ---- click ----------------------------------------------------------
    double click { 0.0 };                ///< resonator level, 0..1
    double clickToneHz { 3000.0 };       ///< the resonator's pitch and the noise high-pass corner, 200..8000
    double clickNoise { 0.0 };           ///< noise burst level, 0..1
    double clickNoiseSeconds { 0.002 };  ///< the burst's fall to -60 dB, 0.0005..0.008

    // ---- under: the sub layer -------------------------------------------
    double under { 0.0 };                ///< the sub's level against the body, 0..1; exactly nothing at 0
    double underSemitones { 12.0 };      ///< how far below the landed pitch, 0..24
    double underDecay { 1.0 };           ///< its decay as a multiple of Decay, 0.25..4
    double underAttackSeconds { 0.0 };   ///< its rise, 0..0.2; 0 is instant

    // ---- knock: the beater's low contact --------------------------------
    double knock { 0.0 };                ///< resonator level, 0..1; exactly nothing at 0
    double knockHz { 350.0 };            ///< its pitch, 150..800
    double knockSeconds { 0.025 };       ///< its ring-down T60, 0.005..0.08

    // ---- amplitude ------------------------------------------------------
    double attackSeconds { 0.0 };        ///< 0..0.02
    double holdSeconds { 0.0 };          ///< 0..0.05
    double decaySeconds { 0.35 };        ///< 0.02..2
    double shape { 0.0 };                ///< 0 exponential .. 1 linear
    double tailMix { 0.0 };              ///< 0..1; exactly nothing at 0
    double tailSeconds { 1.0 };          ///< the tail's decay, 0.1..4
    double level { 0.8 };                ///< 0..1

    // ---- gate -----------------------------------------------------------
    /// Lit: a note-off RELEASES the hit from wherever its envelopes are,
    /// over `releaseSeconds`; dark: a one-shot that ignores note-off. The
    /// hold and decay are the hit's shape either way -- the gate only adds
    /// the early exit a fill needs.
    bool   gate { false };
    double releaseSeconds { 0.0 };       ///< 0..2; 0 is a 1 ms cut, the shortest that does not click

    // ---- velocity amounts, all of the form x * ((1 - a) + a * v) --------
    double velocityLevel { 1.0 };
    double velocityClick { 0.6 };
    double velocityDrop { 0.3 };
    double velocityDecay { 0.0 };
};

class KickEngine
{
public:
    /// The click pair's constants, kept under their old names.
    static constexpr double kClickT60Seconds = ClickPair::kT60Seconds;
    static constexpr double kClickTailSeconds = ClickPair::kTailSeconds;
    static constexpr double kNoiseFloor = ClickPair::kNoiseFloor;

    static constexpr double kToneResonance = 0.15;

    /// Shaper gain at Harmonics = 1 (it is 1 + 3h, so a full-scale body sits
    /// well into both curves at the top of the knob).
    static constexpr double kShaperGainAtFull = 4.0;

    /// The shortest release: a note-off with Release at 0 ramps out over
    /// this rather than stepping to zero, which would click. Inaudible as a
    /// tail, exact as a stop.
    static constexpr double kMinimumReleaseSeconds = 0.001;

    static constexpr double kTwoPi = 2.0 * std::numbers::pi;

    /// The knock is cut exactly this many of its own T60s after the strike
    /// (-240 dB), as the click is at ClickPair::kTailSeconds / kT60Seconds.
    static constexpr double kKnockTailT60s = 4.0;

    void prepare (double internalRate) noexcept
    {
        rate_ = internalRate > 0.0 ? internalRate : 48000.0;

        drop_.prepare (rate_);
        sigh_.prepare (rate_);
        amp_.prepare (rate_);
        tail_.prepare (rate_);

        tone_.prepare (rate_);
        tone_.setMode (dsp::SvfMode::lowpass);
        tone_.setResonance (kToneResonance);

        click_.prepare (rate_);
        knock_.prepare (rate_);
        knock_.setModeCount (1);
        underEnv_.prepare (rate_);

        blocker_.prepare (rate_, 10.0);

        reset();
    }

    /// Silent and idle. Filters and shapers are cleared, so the next hit
    /// starts from nothing -- which is what a drum does.
    void reset() noexcept
    {
        active_ = false;

        drop_.reset();
        sigh_.reset();
        amp_.kill();
        tail_.kill();

        tone_.reset();
        click_.reset();
        knock_.reset();
        knockSamplesLeft_ = 0;
        underEnv_.kill();
        underOn_ = false;
        blocker_.reset();
        adaaEven_.reset();
        adaaOdd_.reset();

        phase_ = 0.0;
        underPhase_ = 0.0;
        inc_ = 0.0;
        incTarget_ = 0.0;
        incStep_ = 0.0;
    }

    /// Strikes. `endHz` is the landed pitch (the engine's caller resolves
    /// Follow key); `velocity` 0..1; `seed` feeds the noise burst;
    /// `samplesToBoundary` is how many internal samples remain before the
    /// engine's next control tick -- 0 when the tick is due -- so the first
    /// partial chunk gets its own exact pitch endpoints.
    void start (const KickSettings& s, double endHz, double velocity,
                std::uint64_t seed, int samplesToBoundary) noexcept
    {
        reset();

        const double v = std::clamp (velocity, 0.0, 1.0);

        // `x * ((1 - a) + a * v)`: exactly x at amount 0, exactly x at
        // velocity 1 whatever the amount (Malleus's form, for the same
        // reason -- the neutral case is a multiply by 1.0, not by 0.999...).
        const auto scaled = [v] (double x, double amount) noexcept
        {
            const double a = std::clamp (amount, 0.0, 1.0);
            return x * ((1.0 - a) + a * v);
        };

        // ---- pitch ----
        endHz_ = std::clamp (endHz, 10.0, rate_ * 0.25);

        const double start = scaled (std::clamp (s.startSemitones, 0.0, 60.0), s.velocityDrop);
        drop_.trigger (start, std::clamp (s.dropSeconds, 0.002, 0.2), std::clamp (s.dropCurve, -1.0, 1.0));
        sigh_.trigger (std::clamp (s.sighSemitones, -12.0, 12.0), std::clamp (s.sighSeconds, 0.1, 2.0));

        phase_ = std::clamp (s.phaseDegrees, 0.0, 90.0) / 360.0;
        incTarget_ = currentHz() / rate_;
        inc_ = incTarget_;
        incStep_ = 0.0;

        // ---- amplitude ----
        amp_.setAttackSeconds (std::clamp (s.attackSeconds, 0.0, 0.02));
        amp_.setAttackTension (0.0);
        amp_.setHoldSeconds (std::clamp (s.holdSeconds, 0.0, 0.05));
        amp_.setDecaySeconds (scaled (std::clamp (s.decaySeconds, 0.02, 2.0), s.velocityDecay));
        amp_.setDecayTension (1.0 - std::clamp (s.shape, 0.0, 1.0));
        amp_.setSustain (0.0);

        gate_ = s.gate;
        release_ = std::max (kMinimumReleaseSeconds, std::clamp (s.releaseSeconds, 0.0, 2.0));
        amp_.setReleaseSeconds (release_);
        amp_.setReleaseTension (1.0 - std::clamp (s.shape, 0.0, 1.0));
        amp_.noteOn();

        tailMix_ = std::clamp (s.tailMix, 0.0, 1.0);
        tailOn_ = ! dsp::isExactlyZero (tailMix_);

        if (tailOn_)
        {
            // Its attack is the drop's landing time, so the tail carries only
            // the landed pitch and never the sweep.
            tail_.setAttackSeconds (std::clamp (s.dropSeconds, 0.002, 0.2));
            tail_.setAttackTension (0.0);
            tail_.setHoldSeconds (0.0);
            tail_.setDecaySeconds (std::clamp (s.tailSeconds, 0.1, 4.0));
            tail_.setDecayTension (1.0 - std::clamp (s.shape, 0.0, 1.0));
            tail_.setSustain (0.0);
            tail_.setReleaseSeconds (release_);
            tail_.setReleaseTension (1.0 - std::clamp (s.shape, 0.0, 1.0));
            tail_.noteOn();
        }

        // ---- harmonics ----
        const double h = std::clamp (s.harmonics, 0.0, 1.0);
        harmonicsOn_ = ! dsp::isExactlyZero (h);

        if (harmonicsOn_)
        {
            mix_ = h;
            evenMix_ = std::clamp (s.even, 0.0, 1.0);

            const double gain = 1.0 + (kShaperGainAtFull - 1.0) * h;
            even_.setGain (gain);
            odd_.setGain (gain);

            blockerOn_ = evenMix_ > 0.0;

            if (blockerOn_)
                blocker_.retune (rate_, std::clamp (s.dcBlockerHz, 5.0, 40.0));
        }
        else
        {
            blockerOn_ = false;
        }

        // ---- tone ----
        toneOn_ = s.toneEnabled;
        toneRatio_ = std::clamp (s.toneRatio, 1.0, 64.0);

        if (toneOn_)
            tone_.setCutoffHz (toneRatio_ * currentHz());

        // ---- click ----
        click_.start (scaled (std::clamp (s.click, 0.0, 1.0), s.velocityClick), s.clickToneHz,
                      scaled (std::clamp (s.clickNoise, 0.0, 1.0), s.velocityClick),
                      s.clickNoiseSeconds, seed);

        // ---- knock: the beater's low contact, velocity's as the click is ----
        const double knock = scaled (std::clamp (s.knock, 0.0, 1.0), s.velocityClick);

        if (! dsp::isExactlyZero (knock))
        {
            const double t60 = std::clamp (s.knockSeconds, 0.005, 0.08);
            knock_.setMode (0, std::clamp (s.knockHz, 150.0, std::min (800.0, rate_ * 0.4)), t60, 1.0);
            knock_.excite (0, knock);
            knockSamplesLeft_ = static_cast<int> (std::ceil (kKnockTailT60s * t60 * rate_));
        }

        // ---- under: the sub, locked to the body ----
        under_ = std::clamp (s.under, 0.0, 1.0);
        underOn_ = ! dsp::isExactlyZero (under_);

        if (underOn_)
        {
            underRatio_ = std::exp2 (-std::clamp (s.underSemitones, 0.0, 24.0) / 12.0);
            underPhase_ = phase_ * underRatio_;

            underEnv_.setAttackSeconds (std::clamp (s.underAttackSeconds, 0.0, 0.2));
            underEnv_.setAttackTension (0.0);
            underEnv_.setHoldSeconds (std::clamp (s.holdSeconds, 0.0, 0.05));
            underEnv_.setDecaySeconds (scaled (std::clamp (s.decaySeconds, 0.02, 2.0), s.velocityDecay)
                                       * std::clamp (s.underDecay, 0.25, 4.0));
            underEnv_.setDecayTension (1.0 - std::clamp (s.shape, 0.0, 1.0));
            underEnv_.setSustain (0.0);
            underEnv_.setReleaseSeconds (release_);
            underEnv_.setReleaseTension (1.0 - std::clamp (s.shape, 0.0, 1.0));
            underEnv_.noteOn();
        }

        gain_ = scaled (std::clamp (s.level, 0.0, 1.0), s.velocityLevel);

        active_ = true;

        if (samplesToBoundary > 0)
            advanceControl (samplesToBoundary);
    }

    /// The control tick: the pitch endpoints for the next `numSamples`
    /// samples, and the tone filter's cutoff for them.
    void advanceControl (int numSamples) noexcept
    {
        if (! active_ || numSamples <= 0)
            return;

        // The chunk starts exactly where the last one aimed.
        inc_ = incTarget_;

        if (drop_.isActive())
            drop_.advance (numSamples);

        if (sigh_.isActive())
            sigh_.advance (numSamples);

        const double hz = currentHz();

        incTarget_ = hz / rate_;
        incStep_ = (incTarget_ - inc_) / static_cast<double> (numSamples);   // exactly 0.0 once landed

        if (toneOn_)
            tone_.setCutoffHz (toneRatio_ * hz);   // guarded: nothing to do once landed
    }

    /// Note-off. A gated hit releases both envelopes from wherever they are
    /// over the release time -- an envelope that has already landed is
    /// idle and unchanged, so a gate that opens late changes nothing. A
    /// one-shot hit ignores this entirely, which is what a drum pad does.
    void release() noexcept
    {
        if (! active_ || ! gate_)
            return;

        if (amp_.isActive())
            amp_.noteOff();

        if (tailOn_ && tail_.isActive())
            tail_.noteOff();

        if (underOn_ && underEnv_.isActive())
            underEnv_.noteOff();
    }

    [[nodiscard]] bool isGated() const noexcept { return gate_; }

    /// One internal sample with a side signal alongside: the kick has none
    /// of its own -- it belongs in the centre -- so the side is exactly 0.0
    /// and the mid is process().
    [[nodiscard]] double process (double& side) noexcept
    {
        side = 0.0;
        return process();
    }

    /// One internal sample. Exactly 0.0 when the hit is over.
    [[nodiscard]] double process() noexcept
    {
        if (! active_)
            return 0.0;

        // ---- body ----
        double x = std::sin (kTwoPi * phase_);

        const double bodyInc = inc_;

        phase_ += inc_;
        inc_ += incStep_;

        if (phase_ >= 1.0)
            phase_ -= 1.0;

        // ---- envelope ----
        const double amp = amp_.process();

        // Sustain is 0: arriving there IS the end. Left alone the envelope
        // would sit in its sustain stage, "active" forever at exactly zero
        // (the Sonitus zombie lesson, CLAUDE.md section 7).
        if (amp_.getStage() == dsp::AdsrStage::sustain)
            amp_.kill();

        double gain = amp;

        if (tailOn_)
        {
            const double t = tail_.process();

            if (tail_.getStage() == dsp::AdsrStage::sustain)
                tail_.kill();

            gain = amp * (1.0 - tailMix_) + tailMix_ * t;
        }

        x *= gain;

        // ---- harmonics ----
        if (harmonicsOn_)
        {
            double shaped = 0.0;

            if (evenMix_ > 0.0)
                shaped += evenMix_ * adaaEven_.process (x, even_);

            if (evenMix_ < 1.0)
                shaped += (1.0 - evenMix_) * adaaOdd_.process (x, odd_);

            x += mix_ * shaped;

            if (blockerOn_)
                x = blocker_.process (x);
        }

        // ---- tone ----
        if (toneOn_)
            x = tone_.process (x);

        // ---- under: the clean sub, after the dirt ----
        if (underOn_)
        {
            double env = 0.0;

            if (underEnv_.isActive())
            {
                env = underEnv_.process();

                if (underEnv_.getStage() == dsp::AdsrStage::sustain)
                    underEnv_.kill();
            }

            x += under_ * env * std::sin (kTwoPi * underPhase_);

            // The body's increment this sample, scaled: locked to it through
            // the drop, so the two can never beat.
            underPhase_ += bodyInc * underRatio_;

            if (underPhase_ >= 1.0)
                underPhase_ -= 1.0;
        }

        // ---- click and noise ----
        click_.addTo (x);

        // ---- knock ----
        if (knockSamplesLeft_ > 0)
        {
            x += knock_.process();

            if (--knockSamplesLeft_ == 0)
                knock_.reset();
        }

        if (! amp_.isActive() && ! (tailOn_ && tail_.isActive()) && ! click_.isActive()
            && ! (underOn_ && underEnv_.isActive()) && knockSamplesLeft_ == 0)
            active_ = false;

        return x * gain_;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// The pitch at the last control boundary: the landed pitch times both
    /// drops' multipliers, and exactly the landed pitch once they have
    /// snapped.
    [[nodiscard]] double currentHz() const noexcept
    {
        return endHz_ * drop_.multiplier() * sigh_.multiplier();
    }

    /// The drop's curve for the hit that is sounding.
    [[nodiscard]] double getDropCurve() const noexcept { return drop_.getCurve(); }

    /// Under's pitch at the last control boundary: the body's times the
    /// interval's ratio, so it drops and sighs with it. 0 with Under off.
    [[nodiscard]] double getUnderHz() const noexcept { return underOn_ ? currentHz() * underRatio_ : 0.0; }

    /// Whether the knock is still ringing -- its own countdown, cut exactly.
    [[nodiscard]] bool isKnockActive() const noexcept { return knockSamplesLeft_ > 0; }

    [[nodiscard]] double getSampleRate() const noexcept { return rate_; }

private:
    double rate_ { 48000.0 };
    bool active_ { false };

    // pitch
    double endHz_ { 50.0 };
    dsp::TensionDrop drop_;
    dsp::TensionDrop sigh_;
    double phase_ { 0.0 };
    double inc_ { 0.0 };
    double incTarget_ { 0.0 };
    double incStep_ { 0.0 };

    // amplitude
    dsp::Adsr amp_;
    dsp::Adsr tail_;
    bool tailOn_ { false };
    double tailMix_ { 0.0 };
    double gain_ { 1.0 };
    bool gate_ { false };
    double release_ { kMinimumReleaseSeconds };

    // harmonics
    bool harmonicsOn_ { false };
    bool blockerOn_ { false };
    double mix_ { 0.0 };
    double evenMix_ { 0.5 };
    dsp::SoftEven even_;
    dsp::SoftOdd odd_;
    dsp::Adaa1<dsp::SoftEven> adaaEven_;
    dsp::Adaa1<dsp::SoftOdd> adaaOdd_;
    dsp::DcBlocker<double> blocker_;

    // tone
    bool toneOn_ { false };
    double toneRatio_ { 8.0 };
    dsp::SvfFilter tone_;

    // click
    ClickPair click_;

    // knock
    dsp::ModalResonator knock_;
    int knockSamplesLeft_ { 0 };

    // under
    bool underOn_ { false };
    double under_ { 0.0 };
    double underRatio_ { 0.5 };
    double underPhase_ { 0.0 };
    dsp::Adsr underEnv_;
};

} // namespace tezla::ictus
