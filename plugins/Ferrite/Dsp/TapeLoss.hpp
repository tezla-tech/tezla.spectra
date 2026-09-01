// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Playback losses and the head bump: what the play head cannot read.
//
// ---------------------------------------------------------------------------
// The physics -- one closed-form magnitude
// ---------------------------------------------------------------------------
//
// Everything the play head loses is a function of the recorded wavelength,
// so of f/v (Chowdhury DAFx-19 after Kadis; k = 2*pi*f / v):
//
//     spacing loss     e^(-k d)          the tape is d metres off the head
//     thickness loss   (1-e^(-k s))/(k s)  deep layers read fainter
//     gap loss         sinc(k g / 2)     the gap averages half a wavelength
//
// Halve the tape speed and every loss lands an octave lower: THAT is why
// tape speed is a tone control, and why this filter is redesigned per speed.
//
// ---------------------------------------------------------------------------
// Minimum phase, by construction, and the departure it implies
// ---------------------------------------------------------------------------
//
// The reference builds a LINEAR-phase FIR from this magnitude (inverse DFT
// of the sampled curve): order/2 samples of latency and pre-ringing inside
// the audible band. CLAUDE.md section 6 makes tone shaping minimum-phase by
// default, so this implementation takes the same analytic magnitude through
// the real-cepstrum transform instead: log-magnitude -> cepstrum -> fold the
// anticausal half onto the causal -> exponentiate -> a minimum-phase
// impulse. Zero latency, no pre-ring, and the tests hold the result against
// the analytic curve at four sample rates -- the magnitude match is the
// licence for the fancier construction, and the linear-phase form remains
// the documented fallback if that match ever degrades.
//
// The cepstrum machinery was written here and later lifted, unchanged, to
// tezla::dsp::MinimumPhaseFir so Membrana's capsule EQ could use it; the
// byte-identity regression in tests/test_MinimumPhaseFir.cpp pins this
// file's designed taps to the exact pre-lift bit patterns, so the lift can
// never quietly re-voice a tape machine already used in a project.
//
// The design runs on a 1024-point FFT rebuilt only when speed or geometry
// moves -- bounded work with no allocation, the same budget as Emberdrive's
// voicing probe, and every change lands through an equal-power crossfade
// between two complete filter states so a speed switch never clicks.
//
// ---------------------------------------------------------------------------
// The head bump is not in the formula
// ---------------------------------------------------------------------------
//
// The famous low-frequency bump comes from the head's finite contact length
// resonating with the recorded wavelength -- fringing at the ends of the
// contact, not gap averaging -- and the loss product above simply does not
// contain it. It is modelled the way the reference models it, as a peaking
// biquad, but placed from the physics: f_bump = v / l_head with the contact
// length l_head ~ 9 mm, which lands the classic figures (15 ips -> ~42 Hz,
// 30 ips -> ~85 Hz, 7.5 ips -> ~21 Hz). Amount 0..2 scales its decibels;
// the starting 2.5 dB at Q 1.3 sits inside published machine tolerances and
// waits on the listening pass -- the user's ears are the acceptance test.

#include <array>
#include <cmath>
#include <cstddef>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/MinimumPhaseFir.hpp>

namespace tezla::ferrite {

class TapeLoss
{
public:
    /// Tap count scales with the sample rate (the reference does the same:
    /// a fixed tap count is a fixed TIME only at one rate, and the low end
    /// of the loss curve needs ~2.7 ms of impulse to render within 0.1 dB).
    static constexpr int kMaxTaps = 512;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        activeTaps_ = static_cast<int> (128.0 * sampleRate / 48000.0);
        activeTaps_ = activeTaps_ < 128 ? 128 : activeTaps_ > kMaxTaps ? kMaxTaps : activeTaps_;
        designInto (active_);
        states_[0].clear();
        states_[1].clear();
        fadeRemaining_ = 0;
        pendingRetarget_ = false;
    }

    void reset() noexcept
    {
        states_[0].clear();
        states_[1].clear();
        fadeRemaining_ = 0;
    }

    /// Tape speed in inches per second. A change redesigns the filter into
    /// the spare state and crossfades to it -- click-free by construction.
    void setSpeedIps (double ips) noexcept
    {
        if (tezla::dsp::isExactly (ips, speedIps_))
            return;

        speedIps_ = ips;
        retarget();
    }

    /// Head-to-tape spacing, coating thickness and play-head gap, in
    /// microns. The expert controls; each is a physical length.
    void setGeometry (double spacingMicrons, double thicknessMicrons,
                      double gapMicrons) noexcept
    {
        if (tezla::dsp::isExactly (spacingMicrons, spacingUm_)
            && tezla::dsp::isExactly (thicknessMicrons, thicknessUm_)
            && tezla::dsp::isExactly (gapMicrons, gapUm_))
            return;

        spacingUm_ = spacingMicrons;
        thicknessUm_ = thicknessMicrons;
        gapUm_ = gapMicrons;
        retarget();
    }

    /// 0 removes the bump entirely, 1 is the tuned 2.5 dB, 2 doubles it.
    void setBumpAmount (double amount) noexcept
    {
        if (tezla::dsp::isExactly (amount, bumpAmount_))
            return;

        bumpAmount_ = amount;
        retarget();
    }

    [[nodiscard]] double getSpeedIps() const noexcept { return speedIps_; }

    /// Minimum-phase: nothing to report.
    [[nodiscard]] static int latencySamples() noexcept { return 0; }

    [[nodiscard]] double process (double x) noexcept
    {
        const double throughActive = states_[active_].process (x, activeTaps_);

        if (fadeRemaining_ <= 0)
            return throughActive;

        // Equal-power crossfade toward the freshly designed state, counted
        // in samples so the host's block size cannot bend it.
        const double throughFading = states_[1 - active_].process (x, activeTaps_);
        const double position = static_cast<double> (fadeRemaining_)
                                  / static_cast<double> (kFadeSamples);

        // `active_` is the NEW filter; position falls 1 -> 0 as its gain
        // rises. The old state drains and is cleared at the end.
        const double newGain = std::cos (position * kHalfPi);
        const double oldGain = std::sin (position * kHalfPi);

        --fadeRemaining_;

        if (fadeRemaining_ == 0)
        {
            states_[1 - active_].clear();

            if (pendingRetarget_)
            {
                pendingRetarget_ = false;
                retarget();
            }
        }

        return throughActive * newGain + throughFading * oldGain;
    }

    // ---- introspection for the tests and the measure tool -----------------

    /// The closed-form loss product this filter is asked to match.
    [[nodiscard]] static double analyticMagnitude (double hz, double ips,
                                                   double spacingMicrons,
                                                   double thicknessMicrons,
                                                   double gapMicrons) noexcept
    {
        const double v = ips * 0.0254;
        const double k = 2.0 * kPi * hz / v;

        const double d = spacingMicrons * 1.0e-6;
        const double s = thicknessMicrons * 1.0e-6;
        const double g = gapMicrons * 1.0e-6;

        const double spacing = std::exp (-k * d);

        const double ks = k * s;
        const double thickness = ks < 1.0e-9 ? 1.0 : (1.0 - std::exp (-ks)) / ks;

        const double kg = 0.5 * k * g;
        const double gap = kg < 1.0e-9 ? 1.0 : std::sin (kg) / kg;

        return spacing * thickness * (gap < 0.0 ? -gap : gap);
    }

    /// Where the bump sits for a speed: contact length ~9 mm of wavelength.
    [[nodiscard]] static double bumpFrequencyFor (double ips) noexcept
    {
        return ips * 0.0254 / kHeadContactMetres;
    }

    /// The designed FIR's own magnitude (bump excluded), by direct DFT of
    /// the active taps -- for the magnitude-match tests.
    [[nodiscard]] double designedMagnitudeAt (double hz) const noexcept
    {
        const auto& taps = states_[active_].taps;
        double re = 0.0, im = 0.0;

        for (int n = 0; n < activeTaps_; ++n)
        {
            const double angle = -2.0 * kPi * hz * n / sampleRate_;
            re += taps[static_cast<std::size_t> (n)] * std::cos (angle);
            im += taps[static_cast<std::size_t> (n)] * std::sin (angle);
        }

        return std::sqrt (re * re + im * im);
    }

    /// The active designed taps, raw. For the byte-identity regression that
    /// pins the shared MinimumPhaseFir against the design this file carried
    /// before the machinery was lifted (bit patterns, not tolerances).
    [[nodiscard]] const double* activeTapData() const noexcept
    {
        return states_[active_].taps.data();
    }

    [[nodiscard]] int activeTapCount() const noexcept { return activeTaps_; }

    /// Energy fraction inside the first `front` taps: the minimum-phase
    /// claim, measurable.
    [[nodiscard]] double frontLoadedEnergy (int front) const noexcept
    {
        const auto& taps = states_[active_].taps;
        double head = 0.0, total = 0.0;

        for (int n = 0; n < activeTaps_; ++n)
        {
            const double sq = taps[static_cast<std::size_t> (n)]
                                * taps[static_cast<std::size_t> (n)];
            total += sq;

            if (n < front)
                head += sq;
        }

        return total > 0.0 ? head / total : 0.0;
    }

private:
    static constexpr double kPi = 3.141592653589793;
    static constexpr double kHalfPi = 1.5707963267948966;
    static constexpr double kHeadContactMetres = 9.0e-3;
    static constexpr int kFadeSamples = 2048;
    static constexpr int kFft = 1024;
    static constexpr double kBaseBumpDb = 2.5;
    static constexpr double kBumpQ = 1.3;

    /// One complete filter: the FIR taps, their delay line, and the bump.
    struct State
    {
        std::array<double, kMaxTaps> taps {};
        std::array<double, kMaxTaps> delay {};
        int write { 0 };
        tezla::dsp::Biquad<double> bump;

        void clear() noexcept
        {
            delay.fill (0.0);
            write = 0;
            bump.reset();
        }

        [[nodiscard]] double process (double x, int tapCount) noexcept
        {
            delay[static_cast<std::size_t> (write)] = x;

            double acc = 0.0;
            int read = write;

            for (int n = 0; n < tapCount; ++n)
            {
                acc += taps[static_cast<std::size_t> (n)]
                         * delay[static_cast<std::size_t> (read)];
                read = read == 0 ? tapCount - 1 : read - 1;
            }

            write = write + 1 == tapCount ? 0 : write + 1;

            return bump.process (acc);
        }
    };

    /// Redesign into the spare state and start the crossfade toward it. A
    /// retarget that lands MID-fade is queued instead: swapping then would
    /// drop the draining old state at whatever gain it had -- a step. The
    /// queued design applies the moment the running fade completes, so a
    /// dragged expert knob settles through a chain of clean crossfades.
    void retarget() noexcept
    {
        if (fadeRemaining_ > 0)
        {
            pendingRetarget_ = true;
            return;
        }

        const int spare = 1 - active_;
        designInto (spare);
        states_[spare].clear();
        active_ = spare;
        fadeRemaining_ = kFadeSamples;
    }

    /// The whole design: sample the analytic magnitude, real-cepstrum it to
    /// minimum phase through the shared designer, truncate to the active
    /// taps. Bounded work, no allocation. The designer was THIS file's code
    /// before it was lifted to shared/ for Membrana; the byte-identity
    /// regression in tests/test_MinimumPhaseFir.cpp holds it to these exact
    /// bits.
    void designInto (int slot) noexcept
    {
        // Log-magnitude at the FFT bins up to Nyquist. The floor keeps the
        // gap null (a true zero) out of the logarithm; a minimum-phase
        // system cannot carry a zero on the unit circle anyway. -60 dB
        // rather than lower is a measured choice: at 192 kHz and 7.5 ips
        // the curve spends most of the band under any floor, and a deeper
        // one stretches the impulse past what 128 taps carry -- the match
        // at the -33 dB point read 1.1 dB off with -80 dB here, 0.2 with
        // -60.
        std::array<double, kFft / 2 + 1> halfLog {};

        for (int bin = 0; bin <= kFft / 2; ++bin)
        {
            const double hz = static_cast<double> (bin) * sampleRate_ / kFft;
            double magnitude = analyticMagnitude (hz, speedIps_, spacingUm_,
                                                  thicknessUm_, gapUm_);

            if (magnitude < 1.0e-3)
                magnitude = 1.0e-3;

            halfLog[static_cast<std::size_t> (bin)] = std::log (magnitude);
        }

        tezla::dsp::MinimumPhaseFir<kFft>::design (halfLog.data(),
                                                   states_[slot].taps.data(),
                                                   activeTaps_);

        // The bump, scaled by the amount control.
        const double bumpDb = kBaseBumpDb * bumpAmount_;
        states_[slot].bump.setCoefficients (tezla::dsp::design::peak (
            bumpFrequencyFor (speedIps_), kBumpQ, bumpDb, sampleRate_));
    }

    double sampleRate_ { 48000.0 };
    double speedIps_ { 15.0 };
    double spacingUm_ { 5.0 };
    double thicknessUm_ { 35.0 };
    double gapUm_ { 2.5 };
    double bumpAmount_ { 1.0 };

    State states_[2];
    int activeTaps_ { 128 };
    int active_ { 0 };
    int fadeRemaining_ { 0 };
    bool pendingRetarget_ { false };
};

} // namespace tezla::ferrite
