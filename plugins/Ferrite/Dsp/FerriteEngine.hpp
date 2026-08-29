// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The tape machine, end to end:
//
//   in -> input gain -> [ oversample: HYSTERESIS ] -> makeup -> DC blocker
//      -> LOSS + HEAD BUMP -> WOW/FLUTTER -> HISS -> auto-trim -> output
//                 (dry path latency-matched for MIX and for bypass)
//
// The hysteresis stage is the only nonlinearity, so it is the only stage
// inside the oversampler (Auto lands it near 192 kHz, CLAUDE.md section 6).
// Its makeup gain is linear, and a linear gain commutes with the
// downsampling filter -- so it is applied at base rate, where it can be
// smoothed per sample without costing the oversampled loop anything.
// Losses and the bump are linear with corners far below Fs/8 and run at
// base rate; the wow/flutter delay's one-millisecond centre joins the
// oversampler's filter delay in the reported latency.
//
// CONTROL runs on a 32-sample grid counted in samples, cut at the timer's
// boundary rather than the callback's (the Emberdrive block-size lesson).
// Drive, saturation and bias are smoothed here at that rate -- the
// hysteresis curve moves in inaudible 32-sample steps regardless of how
// the host slices its blocks -- and every gain is a per-sample curve
// computed once per chunk, so both channels see identical values on
// identical samples.
//
// AUTO-TRIM is measured, not guessed: when drive, saturation, bias, input
// gain or the oversampling rate change, a probe (512 samples of -20 dBFS
// sine through a scratch hysteresis stage at the oversampled rate)
// measures the small-signal gain of the exact nonlinearity the audio is
// about to meet, and the compensator ramps to its reciprocal. Probes are
// rate-limited to one per 2048 samples, so an automation ramp costs a
// bounded slice of CPU rather than a probe per control tick.
//
// HISS is the one intentional noise source, so it obeys the section 7
// contract: defeatable, and OFF contributes bit-exact nothing. The level
// is calibrated: the noise generator's filtered RMS is 1/sqrt(21) of full
// scale, so the gain carries a sqrt(21) correction and the hiss parameter
// reads as the actual output floor in dBFS. Per-channel generators are
// seeded differently -- tape hiss is per-track noise -- while the two
// channels' WOW/FLUTTER share one seed, because both tracks sit on the
// same physical tape and drift together (linked stereo, the section 7
// default).
//
// The DRY path exists twice, deliberately: the BypassMixer owns a
// latency-matched copy internally (its contract takes the input as it
// arrived), and the MIX control reads a second ring delayed by the same
// reported latency, so wet/dry blends stay phase-honest. Both are written
// per chunk, before the input gain touches the buffer.

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <tezla/dsp/BypassMixer.hpp>
#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/VuMeter.hpp>

#include "Hysteresis.hpp"
#include "TapeLoss.hpp"
#include "WowFlutter.hpp"

namespace tezla::ferrite {

namespace dsp = tezla::dsp;

struct Parameters
{
    double inputDb { 0.0 };          ///< -24 .. +24: drive into the tape
    double drive { 0.5 };            ///< 0..1 -> anhysteretic steepness (a)
    double saturation { 0.5 };       ///< 0..1 -> ceiling (Ms)
    double bias { 0.5 };             ///< 0..1 -> reversibility (c); high = clean

    int speedChoice { 2 };           ///< 0: 3.75, 1: 7.5, 2: 15, 3: 30 ips
    double bumpAmount { 1.0 };       ///< 0..2 on the head bump's decibels

    double wowDepth { 0.15 };        ///< 0..1
    double flutterDepth { 0.15 };    ///< 0..1
    double wowRateHz { 0.9 };        ///< expert
    double flutterRateHz { 12.0 };   ///< expert

    double spacingUm { 5.0 };        ///< expert: head-tape spacing
    double thicknessUm { 35.0 };     ///< expert: coating thickness
    double gapUm { 2.5 };            ///< expert: play-head gap

    double hissDb { -200.0 };        ///< output floor in dBFS; <= -119.5 is OFF
    double mix { 1.0 };              ///< dry/wet, dry latency-matched
    double outputDb { 0.0 };         ///< -24 .. +24
    bool autoTrim { true };
    bool bypassed { false };

    dsp::OversamplingMode oversampling { dsp::OversamplingMode::Auto };
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kControlIntervalSamples = 32;

    /// At most one auto-trim probe per this many samples, however fast the
    /// tape parameters are automated. Counted in samples (block-size
    /// independent); the compensation smoother's 50 ms bridges the steps.
    static constexpr int kProbeIntervalSamples = 2048;

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sampleRate_ = sampleRate;
        channels_ = numChannels < 1 ? 1
                  : numChannels > kMaxChannels ? kMaxChannels : numChannels;

        // Builds every stage whatever the factor; setFactor() inside
        // applyParameters() switches without touching memory.
        oversampler_.prepare (maxBlockSize, channels_, 1);
        factor_ = 0;   // impossible, so the forced apply below must set it

        for (int c = 0; c < channels_; ++c)
        {
            blocker_[c].prepare (sampleRate, 10.0);
            loss_[c].prepare (sampleRate);

            // One seed for both channels' wobble: the two tracks sit on the
            // same tape. The hiss seeds differ: separate track noise.
            wowFlutter_[c].setSeed (0x7E21A5F00D5EED01ULL);
            wowFlutter_[c].prepare (sampleRate);
            hissState_[c] = 0x9E3779B97F4A7C15ULL
                              * static_cast<std::uint64_t> (c + 1);
            hissLp_[c] = 0.0;

            inputVu_[c].prepare (sampleRate);
            outputVu_[c].prepare (sampleRate);
        }

        dryDelay_.assign (static_cast<std::size_t> (channels_)
                            * static_cast<std::size_t> (kMaxDryDelay), 0.0);
        dryWrite_ = 0;

        bypass_.prepare (sampleRate, kMaxDryDelay, channels_);

        inputGain_.prepare (sampleRate, 0.02);
        outputGain_.prepare (sampleRate, 0.02);
        mixSmoothed_.prepare (sampleRate, 0.02);
        hissGain_.prepare (sampleRate, 0.05);
        compensation_.prepare (sampleRate, 0.05);
        makeupSmoothed_.prepare (sampleRate, 0.03);
        driveSmoothed_.prepare (sampleRate, 0.03);
        satSmoothed_.prepare (sampleRate, 0.03);
        biasSmoothed_.prepare (sampleRate, 0.03);

        applyParameters (true);

        controlCountdown_ = 0;
        probeCountdown_ = 0;
        prepared_ = true;
    }

    /// Queued; applied on the next 32-sample control boundary. Safe from
    /// any thread that never races itself.
    void setParameters (const Parameters& parameters) noexcept
    {
        pending_ = parameters;
        parametersDirty_ = true;
    }

    /// Clears every sample of audio state and snaps every smoother to its
    /// target, so a transport restart begins at the stated settings rather
    /// than fading toward them. Allocation-free; prepare() must have run.
    void reset() noexcept
    {
        for (int c = 0; c < channels_; ++c)
        {
            hysteresis_[c].reset();
            blocker_[c].reset();
            loss_[c].reset();
            wowFlutter_[c].reset();
            hissState_[c] = 0x9E3779B97F4A7C15ULL
                              * static_cast<std::uint64_t> (c + 1);
            hissLp_[c] = 0.0;
            inputVu_[c].reset();
            outputVu_[c].reset();
        }

        oversampler_.reset();
        std::fill (dryDelay_.begin(), dryDelay_.end(), 0.0);
        dryWrite_ = 0;

        applyParameters (true);
        controlCountdown_ = 0;
        probeCountdown_ = 0;
    }

    [[nodiscard]] int getLatencySamples() const noexcept { return latency_; }

    [[nodiscard]] const dsp::VuMeter& inputVu (int channel) const noexcept
    {
        return inputVu_[channel == 1 ? 1 : 0];
    }

    [[nodiscard]] const dsp::VuMeter& outputVu (int channel) const noexcept
    {
        return outputVu_[channel == 1 ? 1 : 0];
    }

    void process (double* const* channels, int numChannels, int numSamples) noexcept
    {
        if (! prepared_ || numSamples <= 0)
            return;

        const int active = numChannels < channels_ ? numChannels : channels_;

        if (active <= 0)
            return;

        int at = 0;

        while (at < numSamples)
        {
            if (controlCountdown_ <= 0)
            {
                applyParameters (false);
                advanceControl();
                controlCountdown_ = kControlIntervalSamples;
            }

            const int chunk = controlCountdown_ < numSamples - at
                                ? controlCountdown_
                                : numSamples - at;

            processChunk (channels, active, at, chunk);

            controlCountdown_ -= chunk;
            at += chunk;
        }

        // Meters read the final output -- the bypass mixer has had its say,
        // so what the needle shows is what leaves the plugin.
        for (int c = 0; c < active; ++c)
            outputVu_[c].processBlock (channels[c], numSamples);
    }

private:
    static constexpr int kMaxDryDelay = 1 << 12;   // 4096 >> worst latency

    /// The filtered hiss's RMS is sqrt(1/3 * alpha / (2 - alpha)) of full
    /// scale -- 1/sqrt(21) at alpha = 0.25 -- so this correction makes the
    /// hiss parameter read as the actual noise floor in dBFS.
    static constexpr double kHissAlpha = 0.25;
    static constexpr double kHissCalibration = 4.58257569495584;   // sqrt(21)

    [[nodiscard]] double* dryHistory (int channel) noexcept
    {
        return dryDelay_.data()
                 + static_cast<std::size_t> (channel)
                     * static_cast<std::size_t> (kMaxDryDelay);
    }

    [[nodiscard]] static double speedIpsFor (int choice) noexcept
    {
        return choice <= 0 ? 3.75 : choice == 1 ? 7.5 : choice == 2 ? 15.0 : 30.0;
    }

    /// The reference's makeup law (attributed in Hysteresis.hpp):
    /// saturation lowered the ceiling, so the makeup lifts it back; wider
    /// loops (lower bias) read slightly hotter.
    [[nodiscard]] double hysteresisMakeup() const noexcept
    {
        const double width = 1.0 - parameters_.bias;
        return (1.0 + 0.6 * width) / (0.5 + 1.5 * (1.0 - parameters_.saturation));
    }

    /// Small-signal gain of the exact nonlinearity the audio is about to
    /// meet: 512 samples of -20 dBFS-scaled sine at 1 kHz through a scratch
    /// stage at the oversampled rate, makeup included. Bounded work, run at
    /// most once per kProbeIntervalSamples.
    [[nodiscard]] double probeTapeGain() const noexcept
    {
        const double rate = sampleRate_ * factor_;
        const double amplitude = 0.1 * std::pow (10.0, parameters_.inputDb / 20.0);
        const double makeup = hysteresisMakeup();

        Hysteresis probe;
        probe.prepare (rate);
        probe.setParameters (parameters_.drive, parameters_.saturation,
                             parameters_.bias);

        double sumIn = 0.0, sumOut = 0.0;

        for (int i = 0; i < 512; ++i)
        {
            const double input = amplitude
                                   * std::sin (6.283185307179586 * 1000.0 * i / rate);
            const double output = probe.process (input) * makeup;

            if (i >= 128)   // let the loop settle off the virgin curve
            {
                sumIn += input * input;
                sumOut += output * output;
            }
        }

        return sumOut > 0.0 ? std::sqrt (sumOut / sumIn) : 1.0;
    }

    [[nodiscard]] double compensationTargetNow() const noexcept
    {
        if (! parameters_.autoTrim)
            return 1.0;

        return 1.0 / (probeTapeGain()
                        * std::pow (10.0, parameters_.inputDb / 20.0));
    }

    void applyParameters (bool force) noexcept
    {
        if (! parametersDirty_ && ! force)
            return;

        const Parameters& p = pending_;

        const bool tapeChanged = force
            || p.drive != parameters_.drive
            || p.saturation != parameters_.saturation
            || p.bias != parameters_.bias
            || p.inputDb != parameters_.inputDb
            || p.autoTrim != parameters_.autoTrim
            || p.oversampling != parameters_.oversampling;

        const bool factorChanged = force
            || p.oversampling != parameters_.oversampling;

        parameters_ = p;
        parametersDirty_ = false;

        if (factorChanged)
        {
            const int wanted = dsp::oversamplingFactor (parameters_.oversampling,
                                                        sampleRate_);

            if (wanted != factor_)
            {
                factor_ = wanted;
                oversampler_.setFactor (factor_);

                // A rate change rebuilds the hysteresis integrator; its
                // state resets, which a discrete mode switch may do.
                for (int c = 0; c < channels_; ++c)
                    hysteresis_[c].prepare (sampleRate_ * factor_);

                latency_ = oversampler_.getLatencySamples()
                             + wowFlutter_[0].latencySamples();
                bypass_.setLatency (latency_);
            }
        }

        inputGain_.setTarget (std::pow (10.0, parameters_.inputDb / 20.0));
        outputGain_.setTarget (std::pow (10.0, parameters_.outputDb / 20.0));
        mixSmoothed_.setTarget (parameters_.mix);
        makeupSmoothed_.setTarget (hysteresisMakeup());
        driveSmoothed_.setTarget (parameters_.drive);
        satSmoothed_.setTarget (parameters_.saturation);
        biasSmoothed_.setTarget (parameters_.bias);

        const bool hissOn = parameters_.hissDb > -119.5;
        hissGain_.setTarget (hissOn ? kHissCalibration
                                        * std::pow (10.0, parameters_.hissDb / 20.0)
                                    : 0.0);

        for (int c = 0; c < channels_; ++c)
        {
            loss_[c].setSpeedIps (speedIpsFor (parameters_.speedChoice));
            loss_[c].setGeometry (parameters_.spacingUm, parameters_.thicknessUm,
                                  parameters_.gapUm);
            loss_[c].setBumpAmount (parameters_.bumpAmount);
            wowFlutter_[c].setWowDepth (parameters_.wowDepth);
            wowFlutter_[c].setFlutterDepth (parameters_.flutterDepth);
            wowFlutter_[c].setWowRateHz (parameters_.wowRateHz);
            wowFlutter_[c].setFlutterRateHz (parameters_.flutterRateHz);
        }

        if (tapeChanged)
        {
            // Deferred to the probe timer; switching auto-trim OFF is
            // immediate, because there is nothing to measure.
            probePending_ = parameters_.autoTrim;

            if (! parameters_.autoTrim)
                compensation_.setTarget (1.0);
        }

        bypass_.setBypassed (parameters_.bypassed);

        if (force)
        {
            // prepareToPlay must hand back a settled engine: every smoother
            // jumps to its target (a default-zero smoother would fade the
            // first 20 ms in -- the Svarayantra lesson), the trim is probed
            // synchronously, and the bypass crossfade sits at its end state.
            compensation_.setCurrentAndTarget (compensationTargetNow());
            probePending_ = false;

            inputGain_.setCurrentAndTarget (inputGain_.getTarget());
            outputGain_.setCurrentAndTarget (outputGain_.getTarget());
            mixSmoothed_.setCurrentAndTarget (mixSmoothed_.getTarget());
            makeupSmoothed_.setCurrentAndTarget (makeupSmoothed_.getTarget());
            hissGain_.setCurrentAndTarget (hissGain_.getTarget());
            driveSmoothed_.setCurrentAndTarget (parameters_.drive);
            satSmoothed_.setCurrentAndTarget (parameters_.saturation);
            biasSmoothed_.setCurrentAndTarget (parameters_.bias);

            for (int c = 0; c < channels_; ++c)
                hysteresis_[c].setParameters (parameters_.drive,
                                              parameters_.saturation,
                                              parameters_.bias);

            bypass_.reset (parameters_.bypassed);
        }
    }

    /// Everything that advances on the 32-sample control grid, boundary
    /// aligned to the sample count rather than the callback.
    void advanceControl() noexcept
    {
        driveSmoothed_.skip (kControlIntervalSamples);
        satSmoothed_.skip (kControlIntervalSamples);
        biasSmoothed_.skip (kControlIntervalSamples);

        // The setter's no-op guard makes the settled case free.
        for (int c = 0; c < channels_; ++c)
            hysteresis_[c].setParameters (driveSmoothed_.getCurrent(),
                                          satSmoothed_.getCurrent(),
                                          biasSmoothed_.getCurrent());

        probeCountdown_ = probeCountdown_ > kControlIntervalSamples
                            ? probeCountdown_ - kControlIntervalSamples
                            : 0;

        if (probePending_ && probeCountdown_ <= 0)
        {
            compensation_.setTarget (compensationTargetNow());
            probePending_ = false;
            probeCountdown_ = kProbeIntervalSamples;
        }
    }

    void processChunk (double* const* channels, int active, int from,
                       int count) noexcept
    {
        // --- per-sample control curves, computed once so both channels see
        // the same value on the same sample.
        double gainCurve[kControlIntervalSamples];
        double makeupCurve[kControlIntervalSamples];
        double compCurve[kControlIntervalSamples];
        double outCurve[kControlIntervalSamples];
        double mixCurve[kControlIntervalSamples];
        double hissCurve[kControlIntervalSamples];

        for (int i = 0; i < count; ++i)
        {
            gainCurve[i] = inputGain_.next();
            makeupCurve[i] = makeupSmoothed_.next();
            compCurve[i] = compensation_.next();
            outCurve[i] = outputGain_.next();
            mixCurve[i] = mixSmoothed_.next();
            hissCurve[i] = hissGain_.next();
        }

        // --- the input as it arrived, before anything touches it: the
        // bypass mixer wants it raw (it delays internally), and the mix
        // ring wants it written now to read back latency_ samples later.
        double rawChunk[kMaxChannels][kControlIntervalSamples];
        const double* rawPointers[kMaxChannels];

        for (int c = 0; c < active; ++c)
        {
            double* ring = dryHistory (c);

            for (int i = 0; i < count; ++i)
            {
                const double x = channels[c][from + i];
                rawChunk[c][i] = x;
                ring[static_cast<std::size_t> ((dryWrite_ + i) % kMaxDryDelay)] = x;
            }

            rawPointers[c] = rawChunk[c];
        }

        // --- input gain into the tape, metered where the needle sits.
        for (int c = 0; c < active; ++c)
        {
            double* data = channels[c] + from;

            for (int i = 0; i < count; ++i)
                data[i] *= gainCurve[i];

            inputVu_[c].processBlock (data, count);
        }

        // --- the tape stage, oversampled. Makeup waits for base rate:
        // a linear gain commutes with the downsampling filter.
        double* outputPointers[kMaxChannels];
        const double* inputPointers[kMaxChannels];

        for (int c = 0; c < active; ++c)
        {
            inputPointers[c] = channels[c] + from;
            outputPointers[c] = channels[c] + from;
        }

        double* const* oversampled = oversampler_.upsample (inputPointers, count);
        const int overCount = count * factor_;

        for (int c = 0; c < active; ++c)
        {
            double* data = oversampled[c];

            for (int i = 0; i < overCount; ++i)
                data[i] = hysteresis_[c].process (data[i]);
        }

        oversampler_.downsample (outputPointers, count);

        // --- base rate: makeup, DC, losses, wobble, hiss, trims, mix.
        for (int c = 0; c < active; ++c)
        {
            double* data = channels[c] + from;
            const double* ring = dryHistory (c);

            for (int i = 0; i < count; ++i)
            {
                double sample = data[i] * makeupCurve[i];
                sample = blocker_[c].process (sample);
                sample = loss_[c].process (sample);
                sample = wowFlutter_[c].process (sample);

                // Hiss sits before the trims, like noise upstream of an
                // output pot: hotter operating levels genuinely buy SNR.
                if (hissCurve[i] > 0.0)
                {
                    const double noise = nextHiss (c);
                    hissLp_[c] += kHissAlpha * (noise - hissLp_[c]);
                    sample += hissLp_[c] * hissCurve[i];
                }

                sample *= compCurve[i] * outCurve[i];

                // Mix against the latency-matched dry copy.
                const int index = (dryWrite_ + i - latency_ + 2 * kMaxDryDelay)
                                    % kMaxDryDelay;
                const double dry = ring[static_cast<std::size_t> (index)];
                const double m = mixCurve[i];

                data[i] = sample * m + dry * (1.0 - m);
            }
        }

        // --- the bypass crossfade, handed the raw input it asked for.
        bypass_.process (outputPointers, rawPointers, active, count);

        dryWrite_ = (dryWrite_ + count) % kMaxDryDelay;
    }

    /// xorshift64* white noise in [-1, 1), per channel.
    [[nodiscard]] double nextHiss (int channel) noexcept
    {
        auto& state = hissState_[channel];
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;

        const auto scaled = state * 0x2545F4914F6CDD1DULL;
        return static_cast<double> (scaled >> 11)
                 / static_cast<double> (1ULL << 52) - 1.0;
    }

    double sampleRate_ { 48000.0 };
    int channels_ { 2 };
    int factor_ { 1 };
    int latency_ { 0 };
    bool prepared_ { false };

    Parameters parameters_, pending_;
    bool parametersDirty_ { false };

    dsp::Oversampler oversampler_;
    Hysteresis hysteresis_[kMaxChannels];
    dsp::DcBlocker<double> blocker_[kMaxChannels];
    TapeLoss loss_[kMaxChannels];
    WowFlutter wowFlutter_[kMaxChannels];

    std::uint64_t hissState_[kMaxChannels] {};
    double hissLp_[kMaxChannels] {};

    std::vector<double> dryDelay_;
    int dryWrite_ { 0 };

    dsp::BypassMixer bypass_;
    dsp::SmoothedValue<double> inputGain_, outputGain_, mixSmoothed_,
                               hissGain_, compensation_, makeupSmoothed_,
                               driveSmoothed_, satSmoothed_, biasSmoothed_;

    dsp::VuMeter inputVu_[kMaxChannels], outputVu_[kMaxChannels];

    int controlCountdown_ { 0 };
    int probeCountdown_ { 0 };
    bool probePending_ { false };

    friend struct FerriteEngineTestAccess;
};

} // namespace tezla::ferrite
