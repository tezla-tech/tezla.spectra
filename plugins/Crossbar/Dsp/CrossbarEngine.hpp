// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The whole instrument: sixteen voices, a dialler, and the line they go down.
//
// ---------------------------------------------------------------------------
// The line, and why it is in this order
// ---------------------------------------------------------------------------
//
//     voices -> level -> + noise -> BAND -> RATE -> CODEC -> out
//
// Which is the order the network does it. The local loop is what limits the
// bandwidth and what picks up the hiss; the channel bank samples whatever is
// left of the signal after that; the codec quantises the samples. Putting the
// band limit before the rate reduction is not a detail -- it *is* the
// anti-alias filter, and it is why real telephone audio is grubby rather than
// crunchy. Turn BAND off and leave RATE at 8 kHz and the images come back,
// which is the setting to reach for when the crunch is wanted.
//
// **This section is CLAUDE.md section 7's documented aliasing exception.** It
// runs at the host rate with no oversampling and no antialiasing, because the
// folded images are the sound. Everything upstream of it is pure sine tones,
// which have no harmonics to fold in the first place, so the instrument
// oversamples nowhere and a test says so.
//
// ---------------------------------------------------------------------------
// Discrete switches crossfade
// ---------------------------------------------------------------------------
//
// Changing the band mid-signal steps the output by whatever the new filter's
// empty state cannot yet supply, which is a click. Section 7 says discrete
// switches crossfade, so the line is kept in two: the new configuration is
// built in one copy while the old one keeps running in the other, and 20 ms
// of equal-gain fade moves between them. Two lines cost about as much as one
// biquad chain, for a fiftieth of a second.
//
// The fade is skipped entirely once it has finished, so a settled line is a
// single multiply-free path and the identity really is the identity.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Bitcrusher.hpp>
#include <tezla/dsp/Companding.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/SmoothedValue.hpp>

#include "ToneTables.hpp"
#include "ToneVoice.hpp"

namespace tezla::crossbar {

/// The channel's bandwidth.
///
/// **APPEND-ONLY forever** -- a choice parameter stores an index.
enum class BandMode
{
    off = 0,
    toll,       ///< 300-3400 Hz: ITU-T G.712, the sound everyone means
    wideband,   ///< 50-7000 Hz: G.722, what "HD voice" widened it to
    handset,    ///< 500-2800 Hz: a cheap earpiece on top of the channel
    speaker     ///< 700-2200 Hz: a speakerphone, or a phone held at arm's length
};

struct BandEdges
{
    double lowHz;
    double highHz;
};

/// The band edges, in the enum's order.
inline constexpr BandEdges kBandEdges[] {
    { 0.0, 0.0 },        // off
    { 300.0, 3400.0 },   // toll -- G.712
    { 50.0, 7000.0 },    // wideband -- G.722
    { 500.0, 2800.0 },   // handset
    { 700.0, 2200.0 },   // speaker
};

inline constexpr int kBandModeCount = 5;

/// The sampling rates on the RATE control, in its order.
///
/// **APPEND-ONLY forever.** Index 0 is off and the rest descend, with the two
/// that are actually standards -- G.711's 8 kHz and G.722's 16 kHz -- in
/// their right places rather than at the ends.
inline constexpr double kRateHz[] {
    0.0,       // off
    32000.0,
    24000.0,
    16000.0,   // G.722 wideband
    11000.0,
    8000.0,    // G.711 -- the network's own rate, and the default
    6000.0,
    4000.0,
    3000.0,
    2000.0,
    1000.0,
};

inline constexpr int kRateCount = 11;

/// The default RATE index: 8 kHz, because that is the answer to the question.
inline constexpr int kDefaultRateIndex = 5;

/// The two Butterworth pole pairs of a fourth-order section.
inline constexpr double kButterworthQ[2] { 0.541196100146197, 1.306562964876377 };

// ---------------------------------------------------------------------------

/// Band limit, rate reduction and the codec, in that order.
///
/// Copyable on purpose: the crossfade above works by keeping the outgoing
/// configuration alive, filter state and all, while the incoming one settles.
class Line
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        designBand();
        applyRate();
        reset();
    }

    void reset() noexcept
    {
        for (auto& section : highpass_)
            section.reset();

        for (auto& section : lowpass_)
            section.reset();

        downsampler_.reset();
    }

    /// Everything discrete about the line, applied together.
    ///
    /// Returns true if anything actually changed, which is what the engine
    /// uses to decide whether a crossfade is needed. **A no-op returns false
    /// and touches nothing** -- CLAUDE.md section 7's general rule, and here
    /// it is what stops a host pushing settings every block from restarting
    /// a crossfade every block and never finishing one.
    bool configure (BandMode band, int rateIndex, dsp::CompandingLaw codec, int bits) noexcept
    {
        const int clampedRate = std::clamp (rateIndex, 0, kRateCount - 1);
        const int clampedBits = std::clamp (bits, 1, 16);

        const bool changed = band != band_
                               || clampedRate != rateIndex_
                               || codec != compander_.getLaw()
                               || clampedBits != compander_.getBits();

        if (! changed)
            return false;

        if (band != band_)
        {
            band_ = band;
            designBand();
        }

        if (clampedRate != rateIndex_)
        {
            rateIndex_ = clampedRate;
            applyRate();
        }

        compander_.setLaw (codec);
        compander_.setBits (clampedBits);

        return true;
    }

    /// True when the whole line is the identity, bit for bit.
    [[nodiscard]] bool isIdentity() const noexcept
    {
        return band_ == BandMode::off
                 && downsampler_.getRatio() <= 1.0
                 && compander_.isBypassed();
    }

    [[nodiscard]] BandMode getBand() const noexcept { return band_; }
    [[nodiscard]] int getRateIndex() const noexcept { return rateIndex_; }

    /// What the rate control is actually doing, which is not always what it
    /// says: asking for 32 kHz in a 44.1 kHz session is a ratio of 1.38, and
    /// asking for it in a 192 kHz session is a ratio of 6. The tooltip reads
    /// this rather than the label.
    [[nodiscard]] double getEffectiveRateHz() const noexcept
    {
        return downsampler_.getRatio() <= 1.0 ? sampleRate_
                                              : sampleRate_ / downsampler_.getRatio();
    }

    [[nodiscard]] double process (double x) noexcept
    {
        double y = x;

        if (band_ != BandMode::off)
        {
            for (auto& section : highpass_)
                y = section.process (y);

            for (auto& section : lowpass_)
                y = section.process (y);

            y = dsp::snapToZero (y);
        }

        y = downsampler_.process (y);

        return compander_.process (y);
    }

private:
    void designBand() noexcept
    {
        if (band_ == BandMode::off)
            return;

        const BandEdges edges = kBandEdges[static_cast<int> (band_)];

        // The top edge is clamped below Nyquist so a wideband setting in a
        // low-rate session degrades rather than producing nonsense
        // coefficients. It cannot bite at any rate this rig runs.
        const double highHz = std::min (edges.highHz, 0.45 * sampleRate_);
        const double lowHz = std::min (edges.lowHz, 0.45 * sampleRate_);

        for (int i = 0; i < 2; ++i)
        {
            highpass_[i].setCoefficients (
                dsp::design::highpass (lowHz, kButterworthQ[i], sampleRate_));
            lowpass_[i].setCoefficients (
                dsp::design::lowpass (highHz, kButterworthQ[i], sampleRate_));
        }
    }

    void applyRate() noexcept
    {
        const double targetHz = kRateHz[rateIndex_];

        // Index 0 is off, and so is any target at or above the host rate --
        // a ratio of 1 is bypassed exactly by `Downsampler`, which is what
        // makes "32 kHz in a 44.1 kHz session" mean something sensible
        // instead of nothing.
        downsampler_.setRatio (targetHz <= 0.0 ? 1.0
                                               : std::max (1.0, sampleRate_ / targetHz));
    }

    double sampleRate_ { 48000.0 };

    BandMode band_ { BandMode::off };
    int rateIndex_ { 0 };

    dsp::Biquad<double> highpass_[2] {};
    dsp::Biquad<double> lowpass_[2] {};
    dsp::Downsampler downsampler_;
    dsp::Compander compander_;
};

// ---------------------------------------------------------------------------

class CrossbarEngine
{
public:
    static constexpr int kMaxVoices = 16;

    /// How long a discrete line change takes to cross over.
    static constexpr double kCrossfadeSeconds = 0.02;

    /// The pseudo-note the dialler's voice carries, so a real note-off can
    /// never release it and it can never be stolen by name.
    static constexpr int kDialVoiceNote = -2;

    /// The loudest the line hiss gets, at the top of the Noise control.
    static constexpr double kNoiseCeilingDb = -30.0;

    struct Parameters
    {
        Region region { Region::northAmerica };
        CadenceMode cadence { CadenceMode::fromKey };
        double twistDb { 2.0 };
        int mapRoot { kDefaultMapRoot };

        double attackSeconds { 0.002 };
        double decaySeconds { 0.100 };
        double sustain { 1.0 };
        double releaseSeconds { 0.020 };

        double levelDb { 0.0 };
        double noise { 0.0 };

        BandMode band { BandMode::off };
        int rateIndex { 0 };
        dsp::CompandingLaw codec { dsp::CompandingLaw::off };
        int bits { 8 };

        bool pulseDial { false };
        double dialDigitSeconds { 0.1 };
        double dialGapSeconds { 0.1 };
    };

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (auto& voice : voices_)
            voice.prepare (sampleRate_);

        dialler_.prepare (sampleRate_);

        line_.prepare (sampleRate_);
        fading_ = line_;

        level_.prepare (sampleRate_, 0.02);
        level_.setCurrentAndTarget (1.0);

        noise_.prepare (sampleRate_, 0.05);
        noise_.setCurrentAndTarget (0.0);

        crossfadeStep_ = 1.0 / std::max (1.0, kCrossfadeSeconds * sampleRate_);

        reset();
    }

    void reset() noexcept
    {
        for (auto& voice : voices_)
            voice.reset();

        dialler_.reset();
        line_.reset();
        fading_.reset();

        crossfade_ = 1.0;
        exchangeSamples_ = 0;
        nextAge_ = 0;

        for (auto& age : ages_)
            age = 0;
    }

    void setParameters (const Parameters& p) noexcept
    {
        region_ = p.region;
        cadence_ = p.cadence;
        twistDb_ = p.twistDb;
        mapRoot_ = p.mapRoot;

        for (auto& voice : voices_)
        {
            auto& envelope = voice.envelope();
            envelope.setAttackSeconds (p.attackSeconds);
            envelope.setDecaySeconds (p.decaySeconds);
            envelope.setSustain (p.sustain);
            envelope.setReleaseSeconds (p.releaseSeconds);
        }

        level_.setTarget (dsp::dbToGain (p.levelDb));

        // Squared, so the bottom of the control is where the useful settings
        // are: a line you can just hear rather than one you cannot ignore.
        const double amount = std::clamp (p.noise, 0.0, 1.0);
        noise_.setTarget (amount * amount * dsp::dbToGain (kNoiseCeilingDb));

        dialler_.setPulseMode (p.pulseDial);
        dialler_.setTiming (p.dialDigitSeconds, p.dialGapSeconds);

        // The line's discrete settings move by crossfade, and only when they
        // actually move -- `configure` refuses a no-op, so a host pushing
        // parameters every block does not restart the fade every block.
        Line pending = line_;

        if (pending.configure (p.band, p.rateIndex, p.codec, p.bits))
        {
            fading_ = line_;
            line_ = pending;
            crossfade_ = 0.0;
        }
    }

    void setDialString (const char* text) noexcept { dialler_.setDigits (text); }

    [[nodiscard]] const Dialler& getDialler() const noexcept { return dialler_; }

    // -----------------------------------------------------------------------
    // Playing
    // -----------------------------------------------------------------------

    void noteOn (int midiNote, double velocity) noexcept
    {
        const Tone tone = toneForNote (midiNote, mapRoot_);

        if (tone == Tone::count)
            return;

        if (tone == Tone::dialNumber)
        {
            startDialling();
            return;
        }

        startVoice (programFor (tone, region_, twistDb_), midiNote, velocity);
    }

    void noteOff (int midiNote) noexcept
    {
        const Tone tone = toneForNote (midiNote, mapRoot_);

        if (tone == Tone::dialNumber)
        {
            stopDialling();
            return;
        }

        for (auto& voice : voices_)
            if (voice.isActive() && voice.getNote() == midiNote)
                voice.stop();
    }

    void allNotesOff() noexcept
    {
        dialler_.stop();

        for (auto& voice : voices_)
            if (voice.isActive())
                voice.stop();
    }

    /// Both channels get the same signal, which is not laziness: a telephone
    /// is a mono medium and a stereo dial tone would be a lie. Anything that
    /// wants width can have it from a plugin whose job that is.
    void process (double* left, double* right, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            serviceDialler();

            double sum = 0.0;

            for (auto& voice : voices_)
                if (voice.isActive())
                    sum += voice.process();

            sum *= level_.next();

            const double noiseGain = noise_.next();

            if (noiseGain > 0.0)
                sum += noiseGain * whiteNoise();

            double y = line_.process (sum);

            if (crossfade_ < 1.0)
            {
                // `previous + c * (y - previous)` rather than the symmetrical
                // `c * y + (1 - c) * previous`, and the form matters for the
                // same reason section 7's shelf did: when the two lines agree
                // -- switching between two settings that are both the
                // identity, say -- this is exactly their common value, where
                // the other form is a unit in the last place off. Measured:
                // 1.11e-16 of difference on a render that should have been
                // bit-identical. The c == 1 case never reaches here, because
                // a finished fade takes the branch above.
                const double previous = fading_.process (sum);
                y = previous + crossfade_ * (y - previous);
                crossfade_ = std::min (1.0, crossfade_ + crossfadeStep_);
            }

            left[n] = y;

            if (right != nullptr)
                right[n] = y;

            ++exchangeSamples_;
        }
    }

    // -----------------------------------------------------------------------
    // What the editor and the tests read
    // -----------------------------------------------------------------------

    [[nodiscard]] int getActiveVoiceCount() const noexcept
    {
        int count = 0;

        for (const auto& voice : voices_)
            if (voice.isActive())
                ++count;

        return count;
    }

    [[nodiscard]] bool isLineIdentity() const noexcept
    {
        return line_.isIdentity() && crossfade_ >= 1.0 && noise_.getCurrent() <= 0.0;
    }

    [[nodiscard]] double getEffectiveRateHz() const noexcept
    {
        return line_.getEffectiveRateHz();
    }

    /// Every frequency currently sounding, for the keypad lights. Returns how
    /// many were written.
    int getSoundingFrequencies (double* out, int capacity) const noexcept
    {
        int written = 0;

        for (const auto& voice : voices_)
        {
            if (! voice.isActive() || written >= capacity)
                continue;

            double local[kMaxPartials];
            const int count = voice.getSoundingFrequencies (local, kMaxPartials);

            for (int i = 0; i < count && written < capacity; ++i)
                out[written++] = local[i];
        }

        return written;
    }

private:
    void startVoice (const ToneProgram& program, int note, double velocity) noexcept
    {
        const int index = allocate();

        voices_[index].start (program, note, velocity, cadence_, exchangeSamples_);
        ages_[index] = nextAge_++;
    }

    /// A free voice if there is one, else the oldest.
    ///
    /// Oldest means the *smallest* age, because the age is the note-on
    /// counter and it only goes up. Getting that comparison backwards steals
    /// the newest voice, which sounds like the instrument dropping the note
    /// you just played -- the mistake Malleus made and its test caught.
    [[nodiscard]] int allocate() noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
            if (! voices_[i].isActive())
                return i;

        int oldest = 0;

        for (int i = 1; i < kMaxVoices; ++i)
            if (ages_[i] < ages_[oldest])
                oldest = i;

        voices_[oldest].kill();
        return oldest;
    }

    void startDialling() noexcept
    {
        dialler_.start();

        if (dialler_.isRunning())
            startDialDigit();
    }

    void stopDialling() noexcept
    {
        dialler_.stop();
        releaseDialVoice();
    }

    void startDialDigit() noexcept
    {
        const char digit = dialler_.getCurrentDigit();

        if (digit == '\0')
            return;

        if (dialler_.isPulseMode())
        {
            startVoice (programFor (Tone::rotaryPulse, region_, twistDb_),
                        kDialVoiceNote, 1.0);
            return;
        }

        int row = 0, column = 0;

        if (! dtmfForCharacter (digit, row, column))
            return;

        startVoice (programFor (toneForDtmf (row, column), region_, twistDb_),
                    kDialVoiceNote, 1.0);
    }

    void releaseDialVoice() noexcept
    {
        for (auto& voice : voices_)
            if (voice.isActive() && voice.getNote() == kDialVoiceNote)
                voice.stop();
    }

    void serviceDialler() noexcept
    {
        switch (dialler_.tick())
        {
            case Dialler::Event::beginDigit:
                startDialDigit();
                break;

            case Dialler::Event::endDigit:
            case Dialler::Event::finished:
                releaseDialVoice();
                break;

            case Dialler::Event::none:
            default:
                break;
        }
    }

    /// Line hiss. xorshift64 rather than `std::rand`, because the audio
    /// thread may not allocate, lock or touch global state -- and because a
    /// seeded generator makes a rendered take reproducible.
    [[nodiscard]] double whiteNoise() noexcept
    {
        noiseSeed_ ^= noiseSeed_ << 13;
        noiseSeed_ ^= noiseSeed_ >> 7;
        noiseSeed_ ^= noiseSeed_ << 17;

        return 2.0 * (static_cast<double> (noiseSeed_ >> 11)
                        / static_cast<double> (1ULL << 53)) - 1.0;
    }

    double sampleRate_ { 48000.0 };

    ToneVoice voices_[kMaxVoices] {};
    std::int64_t ages_[kMaxVoices] {};
    std::int64_t nextAge_ { 0 };

    Dialler dialler_;

    Line line_;
    Line fading_;
    double crossfade_ { 1.0 };
    double crossfadeStep_ { 1.0 };

    dsp::SmoothedValue<double> level_;
    dsp::SmoothedValue<double> noise_;
    std::uint64_t noiseSeed_ { 0xC0FFEE123456789ULL };

    Region region_ { Region::northAmerica };
    CadenceMode cadence_ { CadenceMode::fromKey };
    double twistDb_ { 2.0 };
    int mapRoot_ { kDefaultMapRoot };

    std::int64_t exchangeSamples_ { 0 };
};

} // namespace tezla::crossbar
