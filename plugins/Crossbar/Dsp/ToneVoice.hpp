// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// One voice: up to four sines, a cadence clock, a click generator and an
// envelope. Plus the dialler, which is a sequencer over a phone number.
//
// ---------------------------------------------------------------------------
// Why plain sines, and why that settles the aliasing question
// ---------------------------------------------------------------------------
//
// The library has a band-limited oscillator with polyBLEP corners, hard sync
// and unison. None of it is wanted here: a telephone tone *is* a sine, and a
// sine has no harmonics to alias. `std::sin` of an accumulated phase is exact,
// costs about 20 ns, and drifts nowhere -- the phase is kept in turns and
// wrapped, so a tone held for an hour is still on frequency.
//
// That is what lets this instrument oversample nowhere. The only thing in it
// that generates images at all is the LINE section, and there the images are
// the point (CLAUDE.md section 7's documented exception).
//
// ---------------------------------------------------------------------------
// The gate, which is not free
// ---------------------------------------------------------------------------
//
// A cadence is a gate on a continuously running tone, and gating a sine
// abruptly is a click. Real exchanges did click; a plugin that clicks 120
// times a minute on a reorder tone is a plugin nobody uses. So each step
// boundary gets a short raised-cosine gate -- 3 ms, or a quarter of the step
// if the step is shorter than 12 ms. Against a 375 ms engaged-tone burst that
// is inaudible; against the howler's 100 ms it is still a twentieth of the
// burst.
//
// **The oscillators keep running through the gaps.** A real tone generator is
// not restarted by the cadence, it is interrupted by it, so the phase carries
// on and the next burst begins wherever the tone had got to. That also makes
// the two-tone beats -- the UK dial tone's 100 Hz, the ringing tone's 50 --
// continuous across a cadence rather than restarting each burst.
//
// **And the step is not applied until the gate has closed**, which is the part
// that was wrong first time and measured as a click. Setting a step's
// frequencies at the boundary silences the tone instantly when the new step
// has none, so the gate has nothing left to fade and does nothing at all: the
// busy tone stepped 0.0404 of full scale in one sample, twice what the sines
// themselves can do. The new step is now queued, the gate closes over the
// ramp, and only then do the oscillators change -- so a transition between two
// *different* tones (the SIT's three segments) is also fade-out-fade-in rather
// than a jump.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Exact.hpp>

#include "ToneTables.hpp"

namespace tezla::crossbar {

/// How a cadence relates to the key press.
///
/// **APPEND-ONLY forever** (CLAUDE.md section 8) -- this backs a choice
/// parameter.
enum class CadenceMode
{
    fromKey = 0,   ///< the cadence starts when the key does
    freeRunning,   ///< as an exchange does it: the tone was already running
    steady         ///< no cadence at all -- the first step, held
};

/// A telephone tone with an envelope on it.
class ToneVoice
{
public:
    /// How long a step boundary's gate takes, and the shortest step it will
    /// not swallow more than a quarter of.
    static constexpr double kGateSeconds = 0.003;

    /// A loop-break click's decay. Short and broadband: what reaches an
    /// earpiece when a rotary dial interrupts the line is a transient, not a
    /// tone, so it is a decaying noise burst rather than a decaying sine.
    static constexpr double kClickDecaySeconds = 0.0015;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        envelope_.prepare (sampleRate_);
        gateSamples_ = std::max (1, static_cast<int> (kGateSeconds * sampleRate_));
        clickCoefficient_ = std::exp (-1.0 / (kClickDecaySeconds * sampleRate_));
        reset();
    }

    void reset() noexcept
    {
        envelope_.reset();
        program_ = ToneProgram {};
        stepIndex_ = 0;
        stepRemaining_ = 0;
        stepLength_ = 0;
        partials_ = 0;
        pending_ = false;
        pendingStep_ = ToneStep {};
        gate_ = 0.0;
        gateTarget_ = 0.0;
        clickLevel_ = 0.0;
        note_ = -1;
        velocity_ = 1.0;

        for (int i = 0; i < kMaxPartials; ++i)
        {
            phase_[i] = 0.0;
            increment_[i] = 0.0;
            gain_[i] = 0.0;
        }
    }

    [[nodiscard]] dsp::Adsr& envelope() noexcept { return envelope_; }
    [[nodiscard]] const dsp::Adsr& envelope() const noexcept { return envelope_; }

    [[nodiscard]] bool isActive() const noexcept { return envelope_.isActive(); }
    [[nodiscard]] int getNote() const noexcept { return note_; }

    /// Where in the program a free-running cadence would be, given how many
    /// samples the exchange has been running. Zero for a program that never
    /// repeats, which is the honest answer: a SIT is not a cadence, it is an
    /// announcement, and one starts when it starts.
    void start (const ToneProgram& program, int note, double velocity,
                CadenceMode cadence, std::int64_t exchangeSamples) noexcept
    {
        program_ = program;
        note_ = note;
        velocity_ = velocity;
        steady_ = cadence == CadenceMode::steady;

        // Nothing is sounding yet, so the first step applies immediately and
        // the gate opens from zero -- a struck key ramps rather than steps.
        partials_ = 0;
        pending_ = false;
        gate_ = 0.0;

        stepIndex_ = 0;
        int offset = 0;

        if (cadence == CadenceMode::freeRunning && program_.loops)
            offset = offsetIntoProgram (exchangeSamples);

        enterStep (stepIndex_, offset);

        envelope_.noteOn();
    }

    void stop() noexcept { envelope_.noteOff(); }

    void kill() noexcept { reset(); }

    /// One sample.
    [[nodiscard]] double process() noexcept
    {
        if (! envelope_.isActive())
            return 0.0;

        advanceCadence();

        double sum = 0.0;

        for (int i = 0; i < partials_; ++i)
        {
            sum += gain_[i] * std::sin (2.0 * std::numbers::pi * phase_[i]);

            phase_[i] += increment_[i];

            if (phase_[i] >= 1.0)
                phase_[i] -= 1.0;
        }

        sum *= gate_;

        // The click rides outside the gate: it is an edge, not a tone, and
        // fading it in would remove the only thing it is.
        sum += clickLevel_;
        clickLevel_ *= clickCoefficient_;

        return sum * velocity_ * envelope_.process();
    }

private:
    /// How many samples a step lasts. Rounded rather than truncated, so a
    /// cadence stated in seconds lands on the same millisecond at every
    /// sample rate instead of drifting a sample earlier each time.
    [[nodiscard]] int samplesFor (double seconds) const noexcept
    {
        if (seconds < 0.0)
            return -1;   // forever

        return std::max (1, static_cast<int> (std::lround (seconds * sampleRate_)));
    }

    /// Where a free-running cadence has got to, in samples into the program.
    [[nodiscard]] int offsetIntoProgram (std::int64_t exchangeSamples) const noexcept
    {
        const double period = periodSeconds (program_);

        if (period <= 0.0)
            return 0;

        const auto periodSamples = static_cast<std::int64_t> (std::llround (period * sampleRate_));

        if (periodSamples <= 0)
            return 0;

        return static_cast<int> (((exchangeSamples % periodSamples) + periodSamples) % periodSamples);
    }

    /// Begins a step, `offset` samples into it. The offset is how a
    /// free-running cadence joins a tone already in progress -- it walks
    /// forward through the steps until the offset is used up.
    void enterStep (int index, int offset) noexcept
    {
        for (int guard = 0; guard < kMaxSteps + 1; ++guard)
        {
            stepIndex_ = index;

            const ToneStep& step = program_.steps[stepIndex_];
            stepLength_ = samplesFor (step.seconds);

            if (stepLength_ < 0 || offset < stepLength_)
            {
                stepRemaining_ = stepLength_ < 0 ? -1 : stepLength_ - offset;
                queueStep (step);
                return;
            }

            offset -= stepLength_;
            index = (index + 1) % std::max (1, program_.stepCount);
        }

        // Cannot happen: the offset is taken modulo the program's period, so
        // one pass always consumes it. Landing here means step 0, unshifted.
        stepIndex_ = 0;
        stepLength_ = samplesFor (program_.steps[0].seconds);
        stepRemaining_ = stepLength_;
        queueStep (program_.steps[0]);
    }

    /// Takes a step now if nothing is sounding, or queues it behind the gate
    /// if something is. See the header: applying it immediately is what made
    /// the cadence click, because a step with no partials silences the tone
    /// before the gate can fade it.
    void queueStep (const ToneStep& step) noexcept
    {
        if (partials_ == 0)
        {
            applyStep (step);
            return;
        }

        pendingStep_ = step;
        pending_ = true;
        gateTarget_ = 0.0;
    }

    /// Points the oscillators at a step's frequencies **without touching their
    /// phase**, which is what makes a cadence a gate on a running generator
    /// rather than a restart -- and what keeps a two-tone beat continuous
    /// across the gaps.
    void applyStep (const ToneStep& step) noexcept
    {
        partials_ = step.partials;

        for (int i = 0; i < partials_; ++i)
        {
            increment_[i] = step.frequency[i] / sampleRate_;
            gain_[i] = step.gain[i];
        }

        gateTarget_ = partials_ > 0 ? 1.0 : 0.0;

        if (step.click)
            fireClick();
    }

    void fireClick() noexcept
    {
        // A dial that has been let go has stopped. Without this the click that
        // lands on the exact sample a pulsed digit ends still fires, and a '0'
        // rattles twenty-one times for its ten pulses.
        if (envelope_.getStage() == dsp::AdsrStage::release)
            return;

        // A deterministic burst: the same dial, the same click. xorshift64,
        // seeded from the step so a phrase replays exactly.
        clickSeed_ ^= clickSeed_ << 13;
        clickSeed_ ^= clickSeed_ >> 7;
        clickSeed_ ^= clickSeed_ << 17;

        const double noise = static_cast<double> (clickSeed_ >> 11)
                               / static_cast<double> (1ULL << 53);

        clickLevel_ = 0.5 + 0.5 * noise;
    }

    void advanceCadence() noexcept
    {
        // A steady tone holds its first step and never advances. So does a
        // step that lasts forever, which is what a dial tone is.
        if (! steady_ && stepRemaining_ > 0)
        {
            if (--stepRemaining_ == 0)
                nextStep();
        }

        // The gate ramps rather than steps. `gateSamples_` is capped at a
        // quarter of the step so a short burst is still mostly a burst.
        const int ramp = std::max (1, stepLength_ > 0
                                        ? std::min (gateSamples_, stepLength_ / 4)
                                        : gateSamples_);
        const double increment = 1.0 / static_cast<double> (ramp);

        if (gate_ < gateTarget_)
            gate_ = std::min (gateTarget_, gate_ + increment);
        else if (gate_ > gateTarget_)
            gate_ = std::max (gateTarget_, gate_ - increment);

        // The queued step lands the moment the gate is shut, so nothing ever
        // jumps: the outgoing tone fades out, the oscillators change in
        // silence, and the incoming one fades in.
        if (pending_ && gate_ <= 0.0)
        {
            applyStep (pendingStep_);
            pending_ = false;
        }
    }

    void nextStep() noexcept
    {
        const int next = stepIndex_ + 1;

        if (next >= program_.stepCount)
        {
            if (! program_.loops)
            {
                // A programme that has finished goes quiet and stays quiet
                // while the key is held -- which for a SIT is exactly right:
                // on a real line the recorded announcement follows, and this
                // is where it would have been. Through the gate, so it fades
                // rather than stopping dead.
                stepRemaining_ = -1;
                stepLength_ = -1;
                queueStep (ToneStep {});
                return;
            }

            enterStep (0, 0);
            return;
        }

        enterStep (next, 0);
    }

    double sampleRate_ { 48000.0 };

    ToneProgram program_ {};
    int stepIndex_ { 0 };
    int stepRemaining_ { 0 };
    int stepLength_ { 0 };
    bool steady_ { false };

    /// The step waiting for the gate to shut. See `queueStep`.
    ToneStep pendingStep_ {};
    bool pending_ { false };

    double phase_[kMaxPartials] {};
    double increment_[kMaxPartials] {};
    double gain_[kMaxPartials] {};
    int partials_ { 0 };

    double gate_ { 0.0 };
    double gateTarget_ { 0.0 };
    int gateSamples_ { 144 };

    double clickLevel_ { 0.0 };
    double clickCoefficient_ { 0.0 };
    std::uint64_t clickSeed_ { 0x9E3779B97F4A7C15ULL };

    dsp::Adsr envelope_;
    double velocity_ { 1.0 };
    int note_ { -1 };
};

// ---------------------------------------------------------------------------
// The dialler
// ---------------------------------------------------------------------------

/// Plays a written phone number.
///
/// A `ToneProgram` holds four steps, which is a cadence and nowhere near a
/// phone number, so the dialler is a sequencer rather than a program: it hands
/// out one digit at a time and the engine starts a voice for each.
///
/// In **pulse** mode a digit is not a tone at all. A rotary dial breaks the
/// loop once per unit, ten times a second, so a digit lasts as long as its own
/// value -- '1' takes a tenth of a second and '0' takes a whole one, which is
/// exactly why short emergency numbers were chosen and why 999 was quicker to
/// dial than 000 would have been.
class Dialler
{
public:
    static constexpr int kMaxDigits = 48;

    /// A rotary dial's ten breaks a second, which sets how long a pulsed
    /// digit lasts.
    static constexpr double kPulseSeconds = 0.1;

    enum class Event
    {
        none = 0,
        beginDigit,
        endDigit,
        finished
    };

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        running_ = false;
        inTone_ = false;
        index_ = 0;
        countdown_ = 0;
    }

    /// Copies a written number, keeping only what a keypad can send. Spaces,
    /// dashes, brackets and a leading plus are skipped rather than dialled.
    void setDigits (const char* text) noexcept
    {
        count_ = 0;

        if (text == nullptr)
            return;

        for (const char* p = text; *p != '\0' && count_ < kMaxDigits; ++p)
        {
            int row = 0, column = 0;

            if (dtmfForCharacter (*p, row, column))
                digits_[count_++] = *p;
        }
    }

    [[nodiscard]] int getDigitCount() const noexcept { return count_; }
    [[nodiscard]] char getDigit (int i) const noexcept { return digits_[i]; }

    void setPulseMode (bool pulse) noexcept { pulse_ = pulse; }
    [[nodiscard]] bool isPulseMode() const noexcept { return pulse_; }

    void setTiming (double digitSeconds, double gapSeconds) noexcept
    {
        digitSeconds_ = std::max (0.01, digitSeconds);
        gapSeconds_ = std::max (0.0, gapSeconds);
    }

    void start() noexcept
    {
        if (count_ == 0)
        {
            running_ = false;
            return;
        }

        running_ = true;
        inTone_ = true;
        index_ = 0;
        countdown_ = toneSamplesFor (digits_[0]);
    }

    void stop() noexcept { reset(); }

    [[nodiscard]] bool isRunning() const noexcept { return running_; }
    [[nodiscard]] char getCurrentDigit() const noexcept
    {
        return index_ < count_ ? digits_[index_] : '\0';
    }

    /// Advances one sample and reports what just happened.
    ///
    /// `beginDigit` and `finished` are edges the engine acts on; `endDigit`
    /// is where the gap starts and is what releases the voice, so a digit gets
    /// its own envelope rather than one long note with holes in it.
    Event tick() noexcept
    {
        if (! running_)
            return Event::none;

        if (--countdown_ > 0)
            return Event::none;

        if (inTone_)
        {
            inTone_ = false;
            countdown_ = std::max (1, static_cast<int> (std::lround (gapSeconds_ * sampleRate_)));
            return Event::endDigit;
        }

        if (++index_ >= count_)
        {
            running_ = false;
            return Event::finished;
        }

        inTone_ = true;
        countdown_ = toneSamplesFor (digits_[index_]);
        return Event::beginDigit;
    }

private:
    /// How long this digit sounds. In tone mode every digit is the same
    /// length; in pulse mode it is ten breaks a second for as many breaks as
    /// the digit is worth.
    [[nodiscard]] int toneSamplesFor (char digit) const noexcept
    {
        double seconds = digitSeconds_;

        if (pulse_)
        {
            const int pulses = pulsesForCharacter (digit);

            // '*', '#' and A-D cannot be pulse dialled at all -- a rotary dial
            // has ten holes. They get one break so the sequence stays audible
            // rather than silently swallowing part of the number.
            seconds = kPulseSeconds * (pulses > 0 ? pulses : 1);
        }

        return std::max (1, static_cast<int> (std::lround (seconds * sampleRate_)));
    }

    double sampleRate_ { 48000.0 };

    char digits_[kMaxDigits] {};
    int count_ { 0 };

    bool pulse_ { false };
    double digitSeconds_ { 0.1 };
    double gapSeconds_ { 0.1 };

    bool running_ { false };
    bool inTone_ { false };
    int index_ { 0 };
    int countdown_ { 0 };
};

} // namespace tezla::crossbar
