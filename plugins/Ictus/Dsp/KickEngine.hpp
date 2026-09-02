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
//     knob, and the thing velocity is most about.
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
// Harmonics 0, Tail 0, Tone off, Click 0, Noise 0 leaves
// sin(2 pi phase) * envelope * level and not one more operation.

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
#include <tezla/dsp/UnisonBank.hpp>
#include <tezla/dsp/Waveshapers.hpp>

namespace tezla::ictus {

/// Every kick control, in the units the user thinks in. Copied into the hit
/// at note-on; see the header comment.
struct KickSettings
{
    // ---- pitch ----------------------------------------------------------
    double tuneHz { 50.0 };              ///< the landed pitch, 20..400 Hz
    bool   followKey { false };          ///< take the landed pitch from the MIDI note instead
    double startSemitones { 30.0 };      ///< the drop starts this far above the landed pitch, 0..60
    double dropSeconds { 0.03 };         ///< landing time of the drop, 0.002..0.2
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

    // ---- amplitude ------------------------------------------------------
    double attackSeconds { 0.0 };        ///< 0..0.02
    double holdSeconds { 0.0 };          ///< 0..0.05
    double decaySeconds { 0.35 };        ///< 0.02..2
    double shape { 0.0 };                ///< 0 exponential .. 1 linear
    double tailMix { 0.0 };              ///< 0..1; exactly nothing at 0
    double tailSeconds { 1.0 };          ///< the tail's decay, 0.1..4
    double level { 0.8 };                ///< 0..1

    // ---- velocity amounts, all of the form x * ((1 - a) + a * v) --------
    double velocityLevel { 1.0 };
    double velocityClick { 0.6 };
    double velocityDrop { 0.3 };
    double velocityDecay { 0.0 };
};

class KickEngine
{
public:
    /// The click resonator's ring-down.
    static constexpr double kClickT60Seconds = 0.003;

    /// After four T60s (-240 dB) the resonator is cut exactly rather than
    /// left ringing below anything a double can express.
    static constexpr double kClickTailSeconds = 0.012;

    /// Below this the noise burst is over, exactly.
    static constexpr double kNoiseFloor = 1.0e-5;

    static constexpr double kToneResonance = 0.15;

    /// Shaper gain at Harmonics = 1 (it is 1 + 3h, so a full-scale body sits
    /// well into both curves at the top of the knob).
    static constexpr double kShaperGainAtFull = 4.0;

    static constexpr double kTwoPi = 2.0 * std::numbers::pi;

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
        click_.setModeCount (1);

        blocker_.prepare (rate_, 10.0);
        noiseHighpass_.prepare (rate_, 3000.0);

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
        blocker_.reset();
        noiseHighpass_.reset();
        adaaEven_.reset();
        adaaOdd_.reset();

        phase_ = 0.0;
        inc_ = 0.0;
        incTarget_ = 0.0;
        incStep_ = 0.0;

        clickSamplesLeft_ = 0;
        noiseOn_ = false;
        noiseEnv_ = 0.0;
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
        drop_.trigger (start, std::clamp (s.dropSeconds, 0.002, 0.2));
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
        amp_.setReleaseSeconds (0.0);
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
            tail_.setReleaseSeconds (0.0);
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
        const double clickHz = std::clamp (s.clickToneHz, 200.0, std::min (8000.0, rate_ * 0.4));
        const double clickLevel = scaled (std::clamp (s.click, 0.0, 1.0), s.velocityClick);

        if (! dsp::isExactlyZero (clickLevel))
        {
            click_.setMode (0, clickHz, kClickT60Seconds, 1.0);
            click_.excite (0, clickLevel);
            clickSamplesLeft_ = static_cast<int> (std::ceil (kClickTailSeconds * rate_));
        }

        noiseLevel_ = scaled (std::clamp (s.clickNoise, 0.0, 1.0), s.velocityClick);

        if (! dsp::isExactlyZero (noiseLevel_))
        {
            random_.seed (seed);
            noiseEnv_ = 1.0;
            noiseCoefficient_ = std::exp (-std::log (1000.0)
                                          / (std::clamp (s.clickNoiseSeconds, 0.0005, 0.008) * rate_));
            noiseHighpass_.retune (rate_, clickHz);
            noiseOn_ = true;
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

    /// One internal sample. Exactly 0.0 when the hit is over.
    [[nodiscard]] double process() noexcept
    {
        if (! active_)
            return 0.0;

        // ---- body ----
        double x = std::sin (kTwoPi * phase_);

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

        // ---- click and noise ----
        if (clickSamplesLeft_ > 0)
        {
            x += click_.process();

            if (--clickSamplesLeft_ == 0)
                click_.reset();
        }

        if (noiseOn_)
        {
            x += noiseLevel_ * noiseEnv_ * noiseHighpass_.process (random_.bipolar());
            noiseEnv_ *= noiseCoefficient_;

            if (noiseEnv_ < kNoiseFloor)
            {
                noiseEnv_ = 0.0;
                noiseOn_ = false;
            }
        }

        if (! amp_.isActive() && ! (tailOn_ && tail_.isActive())
            && clickSamplesLeft_ == 0 && ! noiseOn_)
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
    dsp::ModalResonator click_;
    int clickSamplesLeft_ { 0 };
    bool noiseOn_ { false };
    double noiseLevel_ { 0.0 };
    double noiseEnv_ { 0.0 };
    double noiseCoefficient_ { 0.0 };
    dsp::SmallRandom random_;
    dsp::DcBlocker<double> noiseHighpass_;
};

} // namespace tezla::ictus
