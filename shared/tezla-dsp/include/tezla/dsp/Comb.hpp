// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A flanger built as an instrument control rather than an effect.
//
// ---------------------------------------------------------------------------
// What this is for
// ---------------------------------------------------------------------------
//
// The brief for Sonitus came from a workflow assembled by hand in FL Studio: a
// Fruity Flanger with its **rate pinned at 0 Hz**, so the depth knob stopped
// being a sweep and became a direct, automatable control of where the comb
// notches sit. Feedback at 72%, feedback inverted, wet inverted. That is the
// growl, and every part of it is a thing a plugin can do better than an
// automation lane can.
//
// So there is no built-in sweep here at all. `setDelaySeconds` is the control,
// it is a modulation destination like any other, and whatever draws it -- an
// envelope, the step sequencer, an LFO at any rate including zero -- is the
// caller's business. An LFO wired to it reproduces an ordinary flanger; a
// slow envelope wired to it is the thing the automation lane was for.
//
// ---------------------------------------------------------------------------
// A comb is a reese from the other direction
// ---------------------------------------------------------------------------
//
// Two detuned saws beat against each other and produce sweeping notches: that
// is a reese, and it is a comb whose delay you cannot see or reach. This is
// the same comb with a handle on it. Which is why the delay can also be set as
// a **pitch** -- `setKeyTrack` blends the manual time towards `1/f0` of the
// played note, so the comb's peaks land on the note's own harmonic series and
// the growl comes out tuned instead of clangy. That is a large part of why a
// Noisia-style growl sits in a mix rather than fighting it.
//
// The blend is geometric, because delay is a frequency in disguise: half way
// between 2 ms and 8 ms is 4 ms, not 5 ms.
//
// ---------------------------------------------------------------------------
// Where the notches are
// ---------------------------------------------------------------------------
//
// With the wet added in phase, the feedforward comb `x + wet * x[n-D]` nulls
// wherever the delayed copy is half a cycle late:
//
//     notches at (2k+1) / (2D)          peaks at k / D
//
// Inverting the wet swaps those two, which is what "invert wet" does and why
// it is a switch rather than a trim -- it moves the entire pattern by half a
// notch spacing, and no amount of turning the mix knob gets there.
//
// Feedback turns the same structure into a resonator: the peaks sharpen and
// the notches deepen, and negative feedback shifts the whole set by half a
// spacing again. Feedback and wet-invert are therefore *not* redundant: one
// changes where the pattern sits, the other changes how sharp it is, and the
// two together reach all four combinations. Measured in tests/test_Comb.cpp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Exact.hpp"

namespace tezla::dsp {

/// A delay line read at a fractional position, with 4-point Lagrange
/// interpolation.
///
/// **Lagrange rather than linear**, and the reason is measurable: linear
/// interpolation between two samples is a lowpass whose corner depends on the
/// fractional part, so a swept delay modulates the treble at the sweep rate.
/// On a flanger -- where sweeping the delay *is* the effect -- that turns up as
/// the comb changing brightness as it moves. Measured as the spread in gain
/// across a fraction swept over a whole sample, at 48 kHz:
///
///        freq      linear    Lagrange-3
///       100 Hz    0.000 dB    0.000 dB
///         1 kHz   0.018 dB    0.000 dB
///         4 kHz   0.302 dB    0.016 dB
///         8 kHz   1.248 dB    0.225 dB
///        16 kHz   6.021 dB    3.255 dB
///
/// So it is nineteen times better where a flanger lives and only twice as good
/// at 16 kHz, which is honest rather than encouraging: **a four-point
/// interpolator cannot hold a tone flat near Nyquist**, and neither of these
/// is the answer up there. What makes it a non-problem is that Sonitus's
/// mangle path is oversampled, so 16 kHz at the host rate is 4 kHz internally
/// at x4 -- where the figure is 0.016 dB.
///
/// At an exactly integer delay the Lagrange weights are (0, 1, 0, 0), so the
/// read is the stored sample bit for bit rather than a weighted sum that
/// happens to be close. That is what lets the whole comb have a bit-exact
/// neutral.
///
/// The design is standard: Laakso, Valimaki, Karjalainen & Laine, "Splitting
/// the Unit Delay", IEEE Signal Processing Magazine 13(1), 1996 -- recorded in
/// docs/DSP-REFERENCES.md.
class FractionalDelay
{
public:
    /// Sizes the line for the longest delay that will be asked for. Allocates,
    /// so it is a prepare-time call and never an audio-thread one.
    void prepare (int maximumDelaySamples)
    {
        // Three samples of margin: the interpolator reads one before and two
        // after the integer position.
        const std::size_t wanted = static_cast<std::size_t> (std::max (maximumDelaySamples, 1)) + 4;

        buffer_.assign (wanted, 0.0);
        writeIndex_ = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer_.begin(), buffer_.end(), 0.0);
        writeIndex_ = 0;
    }

    [[nodiscard]] int capacity() const noexcept
    {
        return static_cast<int> (buffer_.size()) - 4;
    }

    void write (double value) noexcept
    {
        buffer_[writeIndex_] = value;

        if (++writeIndex_ >= buffer_.size())
            writeIndex_ = 0;
    }

    /// The shortest delay the four-point kernel can be asked for.
    ///
    /// Two, not one: the interpolator needs a tap *one sample less delayed*
    /// than the integer position, and at a delay of one that tap is the sample
    /// about to be written. At 48 kHz two samples is a first notch at 12 kHz,
    /// which is already above where a flange is a flange.
    static constexpr double kMinimumDelaySamples = 2.0;

    /// Reads `delaySamples` back, **evaluated before the current write**.
    ///
    /// Which is the only ordering a feedback loop can use -- read, feed back,
    /// write -- and so it is the one the interval is defined against: after
    /// `read(D)` and then `write()`, the loop delay is exactly D samples.
    ///
    /// Getting that wrong is invisible except as a small error that grows with
    /// frequency. Indexing from the most recent *written* sample instead makes
    /// the true delay D+1, which reads as a 6.02 dB comb peak measuring
    /// 5.946 dB at the first harmonic and 5.333 dB at the third -- the phase
    /// error of one sample, scaled by frequency, and nothing that looks like an
    /// off-by-one.
    [[nodiscard]] double read (double delaySamples) const noexcept
    {
        const int size = static_cast<int> (buffer_.size());
        const double clamped = std::clamp (delaySamples, kMinimumDelaySamples,
                                           static_cast<double> (size - 3));

        const double whole = std::floor (clamped);
        const double fraction = clamped - whole;

        // y1 sits at the integer delay counted back from the next write slot.
        // y0 is one sample *less* delayed and y2, y3 one and two more -- and
        // more delay is a lower index, so the four taps run backwards.
        const int base = static_cast<int> (writeIndex_) - static_cast<int> (whole);

        const double y0 = at (base + 1, size);
        const double y1 = at (base, size);
        const double y2 = at (base - 1, size);
        const double y3 = at (base - 2, size);

        return lagrange (y0, y1, y2, y3, fraction);
    }

    /// The 4-point Lagrange kernel, exposed so a test can check the delay line
    /// against it rather than against itself.
    [[nodiscard]] static double lagrange (double y0, double y1, double y2, double y3,
                                          double d) noexcept
    {
        const double dm1 = d - 1.0;
        const double dm2 = d - 2.0;
        const double dp1 = d + 1.0;

        const double h0 = -d * dm1 * dm2 / 6.0;
        const double h1 = dp1 * dm1 * dm2 * 0.5;
        const double h2 = -dp1 * d * dm2 * 0.5;
        const double h3 = dp1 * d * dm1 / 6.0;

        return h0 * y0 + h1 * y1 + h2 * y2 + h3 * y3;
    }

private:
    /// Index into the ring, in signed arithmetic so a negative index wraps
    /// rather than becoming an enormous positive one. Doing this in
    /// `std::size_t` is how a delay line reads out of bounds.
    [[nodiscard]] double at (int index, int size) const noexcept
    {
        index %= size;

        if (index < 0)
            index += size;

        return buffer_[static_cast<std::size_t> (index)];
    }

    std::vector<double> buffer_;
    std::size_t writeIndex_ { 0 };
};

class Comb
{
public:
    /// The manual delay control's range, as seconds. 20 microseconds is a notch
    /// at 25 kHz -- off the top -- and 20 ms is a notch at 25 Hz, which is a
    /// slapback rather than a flange. The whole usable span of a flanger, and
    /// then some at both ends.
    static constexpr double kMinimumSeconds = 0.00002;
    static constexpr double kMaximumSeconds = 0.020;

    /// How far **key tracking** may take the delay, which is further.
    ///
    /// One period of the played note is the whole point of tracking, and E0 is
    /// 20.6 Hz -- a period of 48.5 ms. Capping tracking at the manual control's
    /// 20 ms would silently land the comb on the *second* harmonic for every
    /// note below 50 Hz, which is most of what this instrument plays: the growl
    /// would change character across the bottom octave for no reason a player
    /// could see. Measured before it was fixed: a 41.2 Hz note wants 1165
    /// samples at 48 kHz and got 960.
    ///
    /// The two ranges are separate rather than one widened range because a
    /// manual control that reaches 50 ms is a slapback echo, and the flanger is
    /// what the manual control is for.
    static constexpr double kMaximumTrackedSeconds = 0.055;

    /// How far the feedback can go. Strictly below unity, because a comb at
    /// unity feedback is an oscillator -- and unlike the filter's rail, there
    /// is no nonlinearity in this loop to bound it.
    ///
    /// 0.95 gives a peak of 1/(1-0.95) = 26 dB, which is as resonant as a
    /// flanger is ever wanted, and leaves the loop provably stable rather than
    /// stable-if-nothing-goes-wrong.
    static constexpr double kMaximumFeedback = 0.95;

    /// The stereo spread's widest, as a ratio on the delay.
    ///
    /// A **ratio** rather than a fixed offset in milliseconds, so the two
    /// channels stay the same musical interval apart as the delay sweeps. A
    /// fixed offset is a huge detune at 0.1 ms and nothing at 20 ms.
    static constexpr double kMaximumSpread = 0.35;

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        const int longest = static_cast<int> (std::ceil (kMaximumTrackedSeconds * sampleRate_
                                                           * (1.0 + kMaximumSpread))) + 8;

        for (auto& channel : channels_)
        {
            channel.line.prepare (longest);
            channel.damp = 0.0;
        }

        updateDamping();
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : channels_)
        {
            channel.line.reset();
            channel.damp = 0.0;
        }
    }

    // -----------------------------------------------------------------------
    // Controls
    // -----------------------------------------------------------------------

    /// Where the comb sits, as a delay. The first notch is at `1 / (2 * time)`.
    void setDelaySeconds (double seconds) noexcept
    {
        delaySeconds_ = std::clamp (seconds, kMinimumSeconds, kMaximumSeconds);
    }

    [[nodiscard]] double getDelaySeconds() const noexcept { return delaySeconds_; }

    /// The note the comb should track, in Hz. 0 disables tracking regardless of
    /// the amount, which is what a caller with no note playing passes.
    void setNoteHz (double hz) noexcept { noteHz_ = std::max (hz, 0.0); }

    /// 0 leaves the delay exactly where `setDelaySeconds` put it; 1 locks it to
    /// `1 / noteHz` so the comb's peaks are the note's harmonics. In between is
    /// a **geometric** blend, because the delay is a frequency in disguise.
    void setKeyTrack (double amount) noexcept { keyTrack_ = std::clamp (amount, 0.0, 1.0); }

    [[nodiscard]] double getKeyTrack() const noexcept { return keyTrack_; }

    /// -1 to +1. Negative is the "invert feedback" switch, as a continuous
    /// control -- it moves the whole notch pattern by half a spacing.
    void setFeedback (double feedback) noexcept
    {
        feedback_ = std::clamp (feedback, -1.0, 1.0) * kMaximumFeedback;
    }

    [[nodiscard]] double getFeedback() const noexcept { return feedback_; }

    /// A lowpass inside the feedback loop, 0 to 1. At 0 the loop is not
    /// filtered at all and is bit-exactly the undamped comb.
    void setDamping (double damping) noexcept
    {
        damping_ = std::clamp (damping, 0.0, 1.0);
        updateDamping();
    }

    [[nodiscard]] double getDamping() const noexcept { return damping_; }

    /// Turns the notches into peaks and back. Not reachable with the mix
    /// control -- see the header.
    void setWetInverted (bool inverted) noexcept { wetInverted_ = inverted; }
    [[nodiscard]] bool isWetInverted() const noexcept { return wetInverted_; }

    /// How much delayed signal to add, 0 to 1. At 0 the comb is bit-exactly
    /// transparent whatever else is set.
    void setMix (double mix) noexcept { mix_ = std::clamp (mix, 0.0, 1.0); }
    [[nodiscard]] double getMix() const noexcept { return mix_; }

    /// L/R delay offset, 0 to 1 of `kMaximumSpread`. This is what makes it wide.
    /// Multiplies the delay after key tracking, for a caller that knows
    /// something about pitch the comb does not.
    ///
    /// Sonitus's scale lock is the user: it works out where the comb currently
    /// resonates, asks the loaded tuning for the nearest pitch *in the scale*,
    /// and hands back the ratio between the two. The comb stays framework-free
    /// and knows nothing about scales -- and it stays one multiplication,
    /// which at exactly 1.0 (the default, and the off state) is bit-exact by
    /// IEEE rather than by a branch.
    ///
    /// Applied after the key-track blend rather than to the manual delay,
    /// which is the only place it can be: the blend is geometric, so undoing
    /// it to pre-compensate would divide by zero at full tracking.
    void setTuningRatio (double ratio) noexcept
    {
        tuningRatio_ = ratio > 0.0 ? ratio : 1.0;
    }

    [[nodiscard]] double getTuningRatio() const noexcept { return tuningRatio_; }

    void setSpread (double spread) noexcept { spread_ = std::clamp (spread, 0.0, 1.0); }
    [[nodiscard]] double getSpread() const noexcept { return spread_; }

    // -----------------------------------------------------------------------
    // Running
    // -----------------------------------------------------------------------

    /// One stereo sample, in place.
    void process (double& left, double& right) noexcept
    {
        const double base = currentDelaySamples();
        const double offset = spread_ * kMaximumSpread;

        left = processChannel (channels_[0], left, base * (1.0 - offset));
        right = processChannel (channels_[1], right, base * (1.0 + offset));
    }

    /// The delay actually in use, in samples, after key tracking. For a
    /// display, and for a test that wants to predict where the notches are.
    [[nodiscard]] double currentDelaySamples() const noexcept
    {
        const double manual = delaySeconds_ * sampleRate_;

        if (keyTrack_ <= 0.0 || noteHz_ <= 0.0)
            return manual * tuningRatio_;

        // Geometric blend: exp(lerp(log a, log b)) written as a power, which is
        // the same thing and one call cheaper.
        const double tracked = std::clamp (sampleRate_ / noteHz_,
                                           kMinimumSeconds * sampleRate_,
                                           kMaximumTrackedSeconds * sampleRate_);

        return manual * std::pow (tracked / manual, keyTrack_) * tuningRatio_;
    }

    /// Where the first notch of the feedforward comb currently sits, in Hz.
    /// `1 / (2D)`, or `1 / D` when the wet is inverted and the pattern has
    /// moved by half a spacing.
    [[nodiscard]] double firstNotchHz() const noexcept
    {
        const double samples = currentDelaySamples();

        return wetInverted_ ? sampleRate_ / samples : sampleRate_ / (2.0 * samples);
    }

private:
    struct Channel
    {
        FractionalDelay line;
        double damp { 0.0 };
    };

    [[nodiscard]] double processChannel (Channel& channel, double input, double delaySamples) noexcept
    {
        const double delayed = channel.line.read (delaySamples);

        // The damping is a one-pole inside the loop. At zero it is skipped
        // rather than run with a coefficient of 1, so an undamped comb is
        // bit-exactly an undamped comb.
        double fedBack = delayed;

        if (! isExactlyZero (damping_))
        {
            channel.damp += dampingCoefficient_ * (delayed - channel.damp);
            fedBack = channel.damp;
        }

        channel.line.write (input + feedback_ * fedBack);

        // A fast path, **not** the mechanism -- worth being exact about,
        // because it looks like the mechanism. `input + 0.0 * delayed` is
        // already `input` bit for bit in IEEE arithmetic, so the bit-exact
        // bypass the test asserts holds with or without this branch, and
        // deleting it breaks nothing. What it buys is a multiply-add skipped
        // per channel per sample when the comb is off, which for a stage that
        // is permanently in the path of eight voices is worth a predictable
        // branch. It also stops a non-finite delay line leaking through a
        // multiplication by zero, which the feedback cap makes unreachable but
        // costs nothing to rule out.
        if (isExactlyZero (mix_))
            return input;

        return input + (wetInverted_ ? -mix_ : mix_) * delayed;
    }

    void updateDamping() noexcept
    {
        // The corner runs from 18 kHz down to 700 Hz across the control, at the
        // actual sample rate. Geometric, so the travel is even in octaves --
        // which is how a damping control is heard.
        const double corner = 18000.0 * std::pow (700.0 / 18000.0, damping_);
        const double limited = std::min (corner, sampleRate_ * 0.45);

        dampingCoefficient_ = 1.0 - std::exp (-2.0 * 3.141592653589793 * limited / sampleRate_);
    }

    double sampleRate_ { 48000.0 };

    double delaySeconds_ { 0.003 };
    double noteHz_ { 0.0 };
    double keyTrack_ { 0.0 };
    double feedback_ { 0.0 };
    double damping_ { 0.0 };
    double mix_ { 0.0 };
    double spread_ { 0.0 };
    double tuningRatio_ { 1.0 };

    bool wetInverted_ { false };

    double dampingCoefficient_ { 1.0 };

    Channel channels_[2];
};

} // namespace tezla::dsp
