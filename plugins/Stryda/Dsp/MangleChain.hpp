// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Everything that happens to the sound after the operators, and the crossover
// that keeps it away from the bottom octave.
//
// ---------------------------------------------------------------------------
// Why this exists at all, on a synthesiser
// ---------------------------------------------------------------------------
//
// The production sources read for this plugin agree on one thing about the
// genre: its basses are made by **resampling and reprocessing**, not in one
// synth pass. A growl is rendered, mangled, filtered, rendered again. Putting a
// mangle chain inside the instrument does not replace that -- it removes the
// three most common round trips, which is the difference between trying an idea
// and committing to it.
//
// ---------------------------------------------------------------------------
// The one rule every stage here obeys
// ---------------------------------------------------------------------------
//
// **Bit-exact at neutral, not merely transparent.** Each stage is skipped
// outright at its neutral setting -- not run with a coefficient that happens to
// be the identity. "Almost identity" means every existing project changes the
// day the plugin updates (CLAUDE.md section 7), and a chain of six almost-
// identities is six chances at it.
//
// ---------------------------------------------------------------------------
// And why SPLIT is here rather than in F5, where it was planned
// ---------------------------------------------------------------------------
//
// A Linkwitz-Riley crossover summed straight back together is an **allpass**,
// not an identity: the two bands are each two poles of phase shift and they
// reconstruct flat in magnitude only. So a Split control with nothing between
// its bands costs phase and buys nothing, which is why F5 deferred it and F7 is
// where it belongs -- the first phase in which there is something to keep out
// of the low end.
//
// Split is still bit-exact off: at zero the crossover is not run at all.

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Bitcrusher.hpp>
#include <tezla/dsp/Comb.hpp>
#include <tezla/dsp/CompressorCore.hpp>
#include <tezla/dsp/Crossover.hpp>
#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Formant.hpp>
#include <tezla/dsp/Phaser.hpp>
#include <tezla/dsp/Waveshapers.hpp>

namespace tezla::stryda {

namespace dsp = tezla::dsp;

/// Everything the chain is told, once per control chunk.
struct MangleParameters
{
    // ---- the split ----------------------------------------------------------

    /// Below this the signal goes round the whole chain untouched. Zero is off,
    /// and off is bit-exact rather than merely gentle.
    double splitHz { 0.0 };

    // ---- the vowel lane -----------------------------------------------------

    double vowelMix { 0.0 };        ///< 0 is skipped outright
    double vowelMorph { 0.0 };      ///< across the five vowels
    double vowelTract { 0.5 };      ///< vocal-tract length
    double vowelSharpness { 0.5 };

    // ---- the mangle chain ---------------------------------------------------

    double fold { 0.0 };            ///< sine wavefolder, ADAA
    double crushBits { 16.0 };      ///< 16 is skipped
    double crushAmount { 0.0 };
    double downsample { 1.0 };      ///< 1 is skipped

    double combMix { 0.0 };
    double combHz { 220.0 };
    double combFeedback { 0.0 };

    double phaserMix { 0.0 };
    double phaserHz { 600.0 };
    double phaserFeedback { 0.0 };

    double drive { 0.0 };           ///< biased tanh, ADAA

    double compressThresholdDb { 0.0 };
    double compressRatio { 1.0 };   ///< 1 is the identity, and skipped
    double compressAttackMs { 10.0 };
    double compressReleaseMs { 120.0 };
    double compressMakeupDb { 0.0 };
};

/// The post chain, stereo, at the host rate.
///
/// **At the host rate deliberately**, not inside the oversampled section. Crush
/// and downsample are the documented aliasing exception (CLAUDE.md section 7):
/// their whole character comes from folded-back images, so oversampling them
/// would remove the effect. The fold and the drive are ADAA, which band-limits
/// them where they are.
class MangleChain
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        vowel_.prepare (sampleRate_);
        comb_.prepare (sampleRate_);
        phaser_.prepare (sampleRate_);

        for (auto& compressor : compressors_)
            compressor.prepare (sampleRate_);

        for (auto& splitter : splitters_)
            splitter.prepare (sampleRate_);

        // 12 Hz, first order: enough to stop the biased shaper's DC reaching a
        // sub without thinning the fundamental the lane exists to protect.
        for (auto& blocker : blockers_)
            blocker.prepare (sampleRate_, 12.0);

        reset();
    }

    void reset() noexcept
    {
        vowel_.reset();
        comb_.reset();
        phaser_.reset();

        for (auto& compressor : compressors_)
            compressor.reset();

        for (auto& splitter : splitters_)
            splitter.reset();

        for (auto& folder : folders_)
            folder.reset();

        for (auto& saturator : saturators_)
            saturator.reset();

        for (auto& sampler : samplers_)
            sampler.reset();

        for (auto& blocker : blockers_)
            blocker.reset();
    }

    void setParameters (const MangleParameters& parameters) noexcept
    {
        parameters_ = parameters;

        splitting_ = parameters.splitHz > 0.0;

        if (splitting_)
            for (auto& splitter : splitters_)
                splitter.setCrossover (parameters.splitHz);

        vowelling_ = parameters.vowelMix > 0.0;

        if (vowelling_)
        {
            vowel_.setMix (parameters.vowelMix);
            vowel_.setMorph (parameters.vowelMorph);
            vowel_.setTract (parameters.vowelTract);
            vowel_.setSharpness (parameters.vowelSharpness);
        }

        folding_ = parameters.fold > 0.0;

        if (folding_)
        {
            // The fold's own gain, mapped so the knob's top is well past the
            // first fold rather than merely at it.
            folder_.setGain (parameters.fold * 6.0);
        }

        crushing_ = parameters.crushBits < 15.999 || parameters.crushAmount > 0.0;

        if (crushing_)
        {
            crusher_.setBits (parameters.crushBits);
            crusher_.setAmount (parameters.crushAmount);
        }

        sampling_ = parameters.downsample > 1.0;

        if (sampling_)
            for (auto& sampler : samplers_)
                sampler.setRatio (parameters.downsample);

        combing_ = parameters.combMix > 0.0;

        if (combing_)
        {
            comb_.setMix (parameters.combMix);
            comb_.setDelaySeconds (1.0 / std::max (20.0, parameters.combHz));
            comb_.setFeedback (parameters.combFeedback);
        }

        phasing_ = parameters.phaserMix > 0.0;

        if (phasing_)
        {
            phaser_.setMix (parameters.phaserMix);
            phaser_.setFrequencyHz (parameters.phaserHz);
            phaser_.setFeedback (parameters.phaserFeedback);
        }

        driving_ = parameters.drive > 0.0;

        if (driving_)
        {
            // `BiasedTanh` takes a bias, not a drive: the drive is the input
            // gain and the bias is what makes the curve asymmetric, which is
            // where the even harmonics come from. A little bias at full drive,
            // none at all at zero -- so the stage arrives rather than switching
            // on with a character already chosen.
            driveGain_ = 1.0 + parameters.drive * 11.0;
            saturator_.setBias (parameters.drive * 0.35);

            // Trim so the knob changes tone rather than loudness, which is what
            // CLAUDE.md section 7's auto-output-trim rule asks of a drive.
            driveTrim_ = 1.0 / (1.0 + parameters.drive * 3.0);
        }

        for (auto& compressor : compressors_)
        {
            compressor.setThresholdDb (parameters.compressThresholdDb);
            compressor.setRatio (parameters.compressRatio);
            compressor.setAttackMs (parameters.compressAttackMs);
            compressor.setReleaseMs (parameters.compressReleaseMs);
            compressor.setMakeupDb (parameters.compressMakeupDb);
        }

        compressing_ = ! compressors_[0].isIdentity();

        // The whole chain, in one flag. A patch that asks for none of it pays
        // for a branch and nothing else, and the samples come out untouched.
        engaged_ = vowelling_ || folding_ || crushing_ || sampling_
                     || combing_ || phasing_ || driving_ || compressing_;
    }

    /// True when a single stage is doing something. For a test, and for the
    /// panel's own honesty.
    [[nodiscard]] bool isEngaged() const noexcept { return engaged_; }
    [[nodiscard]] bool isSplitting() const noexcept { return splitting_; }

    void process (double& left, double& right) noexcept
    {
        // Nothing asked for: the samples go straight through, bit for bit.
        if (! engaged_)
            return;

        double lowLeft = 0.0;
        double lowRight = 0.0;

        if (splitting_)
        {
            // The low band goes round everything, which is the point: the
            // fundamental cannot be eaten by the distortion above it.
            double highLeft = 0.0;
            double highRight = 0.0;

            splitters_[0].process (left, lowLeft, highLeft);
            splitters_[1].process (right, lowRight, highRight);

            left = highLeft;
            right = highRight;
        }

        if (vowelling_)
            vowel_.process (left, right);

        if (folding_)
        {
            left = folders_[0].process (left, folder_);
            right = folders_[1].process (right, folder_);
        }

        if (crushing_)
        {
            left = crusher_.process (left);
            right = crusher_.process (right);
        }

        if (sampling_)
        {
            left = samplers_[0].process (left);
            right = samplers_[1].process (right);
        }

        if (combing_)
            comb_.process (left, right);

        if (phasing_)
            phaser_.process (left, right);

        if (driving_)
        {
            left = driveTrim_ * saturators_[0].process (left * driveGain_, saturator_);
            right = driveTrim_ * saturators_[1].process (right * driveGain_, saturator_);

            // A biased shaper makes DC, and DC under a sub is the one thing a
            // club system turns into cone excursion. First order, 12 Hz, so it
            // cannot thin the fundamental (CLAUDE.md section 7).
            left = blockers_[0].process (left);
            right = blockers_[1].process (right);
        }

        if (compressing_)
        {
            left = compressors_[0].process (left);
            right = compressors_[1].process (right);
        }

        if (splitting_)
        {
            left += lowLeft;
            right += lowRight;
        }
    }

private:
    double sampleRate_ { 48000.0 };
    MangleParameters parameters_ {};

    std::array<dsp::LinkwitzRiley4<double>, 2> splitters_ {};

    dsp::Formant vowel_;

    dsp::SineFolder folder_ { 0.0 };
    std::array<dsp::Adaa1<dsp::SineFolder>, 2> folders_ {};

    dsp::Bitcrusher crusher_;
    std::array<dsp::Downsampler, 2> samplers_ {};

    dsp::Comb comb_;
    dsp::Phaser phaser_;

    dsp::BiasedTanh saturator_ { 0.0 };
    double driveGain_ { 1.0 };
    double driveTrim_ { 1.0 };
    std::array<dsp::Adaa1<dsp::BiasedTanh>, 2> saturators_ {};
    std::array<dsp::DcBlocker<double>, 2> blockers_ {};

    std::array<dsp::CompressorCore, 2> compressors_ {};

    bool engaged_ { false };
    bool splitting_ { false };
    bool vowelling_ { false };
    bool folding_ { false };
    bool crushing_ { false };
    bool sampling_ { false };
    bool combing_ { false };
    bool phasing_ { false };
    bool driving_ { false };
    bool compressing_ { false };
};

} // namespace tezla::stryda
