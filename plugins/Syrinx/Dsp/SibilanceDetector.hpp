// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// How sibilant the voice is right now -- as a RATIO, which is the whole idea.
//
// Every de-esser that thresholds the absolute level of a high band fails the
// same two ways. Push the singer 10 dB and it starts ducking everything,
// because the high band got louder along with the rest. Give it a bright
// vowel and it ducks that too, because a vowel with energy at 6 kHz looks
// exactly like an /s/ to a level meter. The first failure is why de-essers
// get ridden with automation; the second is why they lisp.
//
// An /s/ is not loud high end. It is high end that is loud RELATIVE TO THE
// BODY OF THE VOICE. Sibilant fricatives put most of their energy above about
// 4 kHz while vowels put theirs in the formants below 3 kHz, so the thing that
// actually separates them is the balance between the two -- and a balance is
// invariant to how hard the singer is pushing, which is exactly the property
// the level-based version lacks.
//
// So:
//
//     sibilance_dB = 10 log10( meanSquare(above corner) / meanSquare(body) )
//
// measured on a detector-only path: a 200 Hz high-pass first, so proximity
// and rumble never enter the denominator and make a quiet voice look
// sibilant. The split is complementary by construction -- the high part IS
// the input minus the low part -- so no energy is counted twice or lost
// between them.
//
// THE TWO WINDOWS ARE DELIBERATELY DIFFERENT LENGTHS, and that asymmetry was
// found by measurement rather than designed in. With one short window on both
// bands, a 150 Hz voice made the ratio wobble at the fundamental -- the window
// was shorter than the pitch period, so it saw individual glottal pulses
// rather than the balance between the bands, and the de-esser chattered on
// every sustained vowel.
//
// The two bands are not the same kind of quantity, which is the reason:
//
//   * the BODY is a REFERENCE. It has to be stable, and it has to span at
//     least one period of the lowest voice it will meet -- 12.5 ms at 80 Hz,
//     so 20 ms by default.
//   * the SIBILANT band is an EVENT. It has to be quick, because an /s/ is
//     over in tens of milliseconds and a slow reading would miss its start.
//
// The sibilant window is 8 ms rather than the 2 ms first tried, and the
// choice was made by measuring what it costs. Sweeping it against a sustained
// 150 Hz vowel and a band-limited /s/, both at the same level:
//
//     window   vowel PEAK   /s/ reading   separation
//      2 ms     -7.69 dB      12.64 dB      20.3 dB
//      3 ms     -8.98         12.61         21.6
//      5 ms    -10.17         12.56         22.7
//      8 ms    -10.88         12.50         23.4
//     12 ms    -11.28         12.45         23.8
//
// The /s/ reading barely moves across the whole sweep while the vowel's peak
// falls by 3.6 dB, so the separation is bought almost for nothing: what the
// longer window removes is the vowel's per-glottal-pulse wobble, not the
// sibilance. 8 ms keeps most of that and is still far faster than the 50-150
// ms an /s/ lasts. It is the vowel's PEAK that matters, not its average --
// the peaks are what poke through a threshold and duck a sustained note.
//
// Level independence survives the asymmetry untouched: scaling the input by k
// scales both mean squares by k^2 whatever their window lengths, so the ratio
// is unchanged. That is the property the whole design rests on, and it does
// not depend on the windows matching.
//
// Both followers are mean-square one-poles with their time constants in
// milliseconds and their filters in Hz, so the reading is the same at every
// host rate.
//
// Silence: with no signal the ratio is 0/0. Below a floor the detector
// reports kSilentDb rather than a number, because "no sibilance" is the
// truthful answer for silence and a NaN reaching a gain computer is not.

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Denormals.hpp>

namespace tezla::syrinx {

namespace dsp = tezla::dsp;

class SibilanceDetector
{
public:
    /// What the detector reports when there is nothing to measure. Far below
    /// any usable threshold, so a de-esser in front of silence does nothing.
    static constexpr double kSilentDb = -120.0;

    /// Below this mean square, the signal is not worth a ratio. -100 dBFS.
    static constexpr double kSilenceFloor = 1.0e-10;

    /// The detector's own high-pass: rumble and proximity are not body.
    static constexpr double kBodyFloorHz = 200.0;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        rumble_.setCoefficients (dsp::design::highpass (kBodyFloorHz, 0.707, sampleRate_));

        setCornerHz (cornerHz_);
        setBodyWindowMs (bodyWindowMs_);
        setSibilantWindowMs (sibilantWindowMs_);
        reset();
    }

    void reset() noexcept
    {
        rumble_.reset();
        split_.reset();
        bodyPower_ = 0.0;
        sibilantPower_ = 0.0;
    }

    /// Where the voice stops and the sibilance starts. 3 kHz is a lisping
    /// low; 6 kHz suits most rap; 9 kHz and up catches only the sharpest
    /// edge of an /s/.
    void setCornerHz (double hz) noexcept
    {
        cornerHz_ = std::clamp (hz, 1000.0, std::min (16000.0, sampleRate_ * 0.45));
        split_.setCoefficients (dsp::design::lowpass (cornerHz_, 0.707, sampleRate_));
    }

    /// The BODY's mean-square window -- the reference. Must span at least one
    /// period of the lowest voice it will meet, or the ratio wobbles at the
    /// fundamental and the de-esser chatters on sustained vowels.
    void setBodyWindowMs (double milliseconds) noexcept
    {
        bodyWindowMs_ = std::max (milliseconds, 0.1);
        bodyCoefficient_ = 1.0 - std::exp (-1.0 / (bodyWindowMs_ * 0.001 * sampleRate_));
    }

    /// The SIBILANT band's window -- the event. Short, because an /s/ is over
    /// in tens of milliseconds and its start is what matters.
    void setSibilantWindowMs (double milliseconds) noexcept
    {
        sibilantWindowMs_ = std::max (milliseconds, 0.1);
        sibilantCoefficient_ = 1.0 - std::exp (-1.0 / (sibilantWindowMs_ * 0.001 * sampleRate_));
    }

    [[nodiscard]] double getCornerHz() const noexcept { return cornerHz_; }

    /// One sample in; the current sibilance ratio in dB out.
    [[nodiscard]] double process (double input) noexcept
    {
        const double detector = rumble_.process (input);

        // Complementary by construction: body + sibilant is the detector
        // signal exactly, so nothing is counted twice or lost between them.
        const double body = split_.process (detector);
        const double sibilant = detector - body;

        bodyPower_ = dsp::snapToZero (bodyPower_
                       + bodyCoefficient_ * (body * body - bodyPower_));
        sibilantPower_ = dsp::snapToZero (sibilantPower_
                       + sibilantCoefficient_ * (sibilant * sibilant - sibilantPower_));

        if (bodyPower_ + sibilantPower_ < kSilenceFloor)
            return kSilentDb;

        // The floor in the denominator is what keeps a whispered /s/ over
        // near-silent body from reading as +infinity.
        return 10.0 * std::log10 ((sibilantPower_ + kSilenceFloor)
                                    / (bodyPower_ + kSilenceFloor));
    }

    [[nodiscard]] double getBodyPower() const noexcept { return bodyPower_; }
    [[nodiscard]] double getSibilantPower() const noexcept { return sibilantPower_; }

private:
    double sampleRate_ { 44100.0 };
    double cornerHz_ { 6000.0 };
    double bodyWindowMs_ { 20.0 };
    double sibilantWindowMs_ { 8.0 };
    double bodyCoefficient_ { 0.01 };
    double sibilantCoefficient_ { 0.1 };

    dsp::Biquad<double> rumble_;
    dsp::Biquad<double> split_;

    double bodyPower_ { 0.0 };
    double sibilantPower_ { 0.0 };
};

} // namespace tezla::syrinx
