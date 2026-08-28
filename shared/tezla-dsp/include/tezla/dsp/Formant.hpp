// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Three tracked resonances, morphable across vowels -- a comb shaped like a
// mouth.
//
// ---------------------------------------------------------------------------
// The same idea again, at a third time constant
// ---------------------------------------------------------------------------
//
// Sonitus is built on one observation: a reese, a flanger and a vowel filter
// are the same thing at different time constants. Detuned saws beat and make a
// comb you cannot reach; a delay line makes a comb you can drive directly; and
// a vocal tract makes a comb with three fixed lumps in it. This is the third.
//
// It is what turns a growl into a *talking* growl, which is the sound the brief
// was reaching for through an effects chain. Sweeping the morph across the
// vowels while the filter and the comb do their own thing is the whole trick.
//
// ---------------------------------------------------------------------------
// The numbers, and where they come from
// ---------------------------------------------------------------------------
//
// Vowel formant frequencies are measured data, not something to derive:
// CLAUDE.md section 9 says to take a published table rather than reinvent it,
// because a subtle reimplementation is strictly worse than a faithful copy and
// no measurement of ours could tell us we had got it wrong.
//
// **These are Peterson & Barney (1952), Table II, the adult-male row** -- read
// from the paper, which is saved under `technical references/sonitus/`. The
// fifteen frequencies were already right when they were quoted from general
// reference; the amplitudes were not, and that is the interesting half.
//
// The paper gives a relative amplitude *per vowel per formant*, referred to the
// first formant of [a], and they range over thirty decibels. This used to hold
// one set of three constants for every vowel, which made every vowel have the
// same formant balance -- and the balance is a large part of what tells one
// vowel from another. An "ee" wants its second formant 24 dB down; a constant
// -7 dB leaves it seventeen decibels too loud, which is most of the way to not
// being an "ee" at all.
//
// Bandwidths are held constant per formant rather than per vowel, because a
// formant's bandwidth is set by how lossy the tract is rather than by where the
// resonance sits. So Q changes as the morph moves, which is what happens in a
// real mouth.
//
// ---------------------------------------------------------------------------
// Two things a vowel filter usually cannot do
// ---------------------------------------------------------------------------
//
// **Harmonic lock.** Overtone singing -- sygyt, khoomei -- is not a second
// voice. It is one source with a very sharp tract resonance selecting a single
// *harmonic of the drone* and making it audible as a melody. Two things follow:
// the resonance has to be far sharper than a speech formant, and it has to sit
// on a harmonic rather than at a fixed frequency, or the melody is out of tune
// with the note underneath it.
//
// So `setHarmonicLock` points the three resonances at harmonics N, N+1 and N+2
// of the played note instead of at a vowel. **This is the comb's key tracking,
// applied to the formant** -- the comb locks its notches to the note's period,
// this locks the resonances to the note's harmonics. Same thesis, third time
// constant. Sweep N from the sequencer and the overtone line can only land in
// tune, because there is nowhere else for it to land.
//
// **An anti-formant.** A nasal is not a vowel with different peaks; it is a
// vowel with a *zero*. The nasal cavity is a side branch, and a side branch
// cancels rather than resonates. Every synth vowel filter I know of has only
// poles, which is why none of them can say "m" or "ng" -- or the ending of a
// chanted "AUM". One movable notch alongside the three peaks fixes that, and
// independently it is a useful control on a growl: a hole you can put anywhere.

#include <algorithm>
#include <array>
#include <cmath>

#include "Exact.hpp"

namespace tezla::dsp {

/// The vowels the morph walks through, in the order it walks them.
///
/// **Append-only** -- a choice parameter stores an index, and the morph stores
/// a position along this list. Inserting one silently repoints every saved
/// setting. CLAUDE.md section 8.
///
/// The order is the standard vowel circle rather than alphabetical: it runs
/// front to back so that sweeping the morph is a continuous mouth movement
/// instead of a series of jumps.
enum class Vowel
{
    ee = 0,   ///< as in "beet"   -- F1 low, F2 high: the widest split
    eh,       ///< as in "bed"
    ah,       ///< as in "father" -- F1 high, F2 middling
    oh,       ///< as in "bought"
    oo,       ///< as in "boot"   -- both low, closest together

    count
};

class Formant
{
public:
    static constexpr int kFormants = 3;
    static constexpr int kVowels = static_cast<int> (Vowel::count);

    /// Peterson & Barney (1952) Table II, adult-male row, in Hz.
    ///
    /// The paper's ten vowels in its own order are /i ɪ ɛ æ ɑ ɔ ʊ u ʌ ɜ˞/; these
    /// five are its columns 1, 3, 5, 6 and 8 -- heed, head, hod, hawed, who'd.
    static constexpr double kFrequencies[kVowels][kFormants] = {
        { 270.0, 2290.0, 3010.0 },   // ee -- /i/
        { 530.0, 1840.0, 2480.0 },   // eh -- /ɛ/
        { 730.0, 1090.0, 2440.0 },   // ah -- /ɑ/
        { 570.0,  840.0, 2410.0 },   // oh -- /ɔ/
        { 300.0,  870.0, 2240.0 },   // oo -- /u/
    };

    /// Nominal bandwidths, in Hz -- constant per formant, not per vowel.
    ///
    /// The paper does not give bandwidths, so these are not from it: a
    /// formant's bandwidth is set by how lossy the tract is rather than by
    /// where the resonance sits, and these are the conventional figures. Q
    /// therefore changes as the morph moves, which is what happens in a mouth.
    static constexpr double kBandwidths[kFormants] = { 80.0, 90.0, 120.0 };

    /// Relative formant amplitudes in dB, from the same table.
    ///
    /// **Per vowel, and that matters.** The paper refers them all to the first
    /// formant of [ɑ] and they span thirty decibels: /u/'s third formant is
    /// 43 dB down where /ɛ/'s is 24. Holding them constant -- which is what
    /// this did before the paper was read -- gives every vowel the same
    /// spectral balance, and the balance is most of what distinguishes one
    /// vowel from another.
    ///
    /// The amplitudes were averaged across men, women and children in the
    /// original: the paper says the measurements "did not show decided
    /// differences between classes of speakers, and so have been averaged all
    /// together". So these are not the male row specifically, unlike the
    /// frequencies above.
    static constexpr double kAmplitudesDb[kVowels][kFormants] = {
        {  -4.0, -24.0, -28.0 },     // ee -- /i/
        {  -2.0, -17.0, -24.0 },     // eh -- /ɛ/
        {  -1.0,  -5.0, -28.0 },     // ah -- /ɑ/
        {   0.0,  -7.0, -34.0 },     // oh -- /ɔ/
        {  -3.0, -19.0, -43.0 },     // oo -- /u/
    };

    /// How far the sharpness control can narrow or widen the bandwidths.
    ///
    /// At the sharp end the resonances ring and the filter sings the vowel; at
    /// the wide end they blur into a broad tilt and it is barely vocal at all.
    /// A factor rather than a Q, because the bandwidths differ per formant and
    /// they should scale together.
    ///
    static constexpr double kNarrowest = 0.25;
    static constexpr double kWidest = 4.0;

    /// How much further the harmonic lock narrows the resonances, at full lock.
    ///
    /// **The extra sharpness belongs to the lock rather than to the sharpness
    /// control, and that is not a workaround.** Selecting one partial out of a
    /// drone is a different job from shaping a vowel's broad region, and it
    /// takes a bandwidth of a few hertz where a spoken formant has eighty. A
    /// tract doing sygyt is arranged differently from one saying "ah".
    ///
    /// It is also the safe way round. Widening `kNarrowest` instead would have
    /// silently re-mapped the whole sharpness control -- at 0.02 a stored
    /// sharpness of 0.5 gives a bandwidth 0.283 times nominal where it used to
    /// give 1.0, so every saved patch with a vowel filter would have changed
    /// character on update. Not a parameter rename, but the same class of
    /// breakage: a stored value that quietly means something else. CLAUDE.md
    /// section 8.
    ///
    /// At full lock and full sharpness this reaches 0.25 * 0.08 = 0.02 -- a
    /// 1.6 Hz first formant, Q in the hundreds, far past anything a mouth does
    /// and exactly what selecting a single partial needs.
    static constexpr double kLockedNarrowing = 0.08;

    /// The widest and narrowest the anti-formant's notch can be.
    ///
    /// A nasal zero is broad -- it is a cancellation, not a resonance -- so this
    /// tops out well below the peaks' sharpness.
    static constexpr double kNotchQ = 1.4;

    /// How many harmonics up the lock control reaches.
    ///
    /// Twenty-four is two octaves of the harmonic series, which covers the
    /// range sygyt actually uses (roughly partials 6 to 12) with room either
    /// side. Past that the partials are closer together than the resonance is
    /// wide and selecting one stops meaning anything.
    static constexpr double kMaximumHarmonic = 24.0;

    static constexpr double kMaximumCutoffFraction = 0.45;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : channels_)
        {
            for (auto& band : channel.bands)
            {
                band.s1 = 0.0;
                band.s2 = 0.0;
            }

            channel.notch.s1 = 0.0;
            channel.notch.s2 = 0.0;
        }
    }

    // -----------------------------------------------------------------------
    // Controls
    // -----------------------------------------------------------------------

    /// 0 is the first vowel in the list, 1 is the last, and everything between
    /// is a **geometric** blend of the two nearest -- because a formant is a
    /// frequency and the ear hears the ratio. Blending 270 and 530 Hz linearly
    /// puts the halfway point at 400 Hz; the mouth puts it at 378.
    void setMorph (double morph) noexcept
    {
        morph_ = std::clamp (morph, 0.0, 1.0);
        updateCoefficients();
    }

    [[nodiscard]] double getMorph() const noexcept { return morph_; }

    /// 0 is the widest, 1 the sharpest.
    void setSharpness (double sharpness) noexcept
    {
        sharpness_ = std::clamp (sharpness, 0.0, 1.0);
        updateCoefficients();
    }

    [[nodiscard]] double getSharpness() const noexcept { return sharpness_; }

    /// 0 is bit-exactly transparent, 1 is the vowel alone with no dry at all.
    void setMix (double mix) noexcept { mix_ = std::clamp (mix, 0.0, 1.0); }
    [[nodiscard]] double getMix() const noexcept { return mix_; }

    /// The note the harmonic lock tracks, in Hz. Zero disables the lock however
    /// the amount is set -- which is what a caller with no note playing passes.
    void setNoteHz (double hz) noexcept
    {
        noteHz_ = std::max (hz, 0.0);
        updateCoefficients();
    }

    [[nodiscard]] double getNoteHz() const noexcept { return noteHz_; }

    /// Which harmonic of the played note the first resonance sits on, counting
    /// from 1 for the fundamental. Continuous, because it is a modulation
    /// destination: swept, it walks the overtone series.
    void setHarmonic (double harmonic) noexcept
    {
        harmonic_ = std::clamp (harmonic, 1.0, kMaximumHarmonic);
        updateCoefficients();
    }

    [[nodiscard]] double getHarmonic() const noexcept { return harmonic_; }

    /// How far the resonances are pulled off the vowel and onto the note's
    /// harmonics. 0 is the vowel, 1 is locked, and between is a **geometric**
    /// blend -- a formant is a frequency and the ear hears the ratio.
    void setHarmonicLock (double amount) noexcept
    {
        lock_ = std::clamp (amount, 0.0, 1.0);
        updateCoefficients();
    }

    [[nodiscard]] double getHarmonicLock() const noexcept { return lock_; }

    /// Where the anti-formant sits, in Hz.
    void setNotchHz (double hz) noexcept
    {
        notchHz_ = std::clamp (hz, 20.0, 20000.0);
        updateCoefficients();
    }

    [[nodiscard]] double getNotchHz() const noexcept { return notchHz_; }

    /// How deep the anti-formant cuts. **0 is bit-exactly out of the path**,
    /// not merely shallow -- CLAUDE.md section 7, since this sits permanently
    /// in the wet signal.
    void setNotchDepth (double depth) noexcept
    {
        notchDepth_ = std::clamp (depth, 0.0, 1.0);
        updateCoefficients();
    }

    [[nodiscard]] double getNotchDepth() const noexcept { return notchDepth_; }

    /// Where formant `index` currently sits, in Hz. For a display, and for a
    /// test that wants to predict the response.
    [[nodiscard]] double formantHz (int index) const noexcept
    {
        if (index < 0 || index >= kFormants)
            return 0.0;

        return bands_[static_cast<std::size_t> (index)].frequency;
    }

    [[nodiscard]] double formantQ (int index) const noexcept
    {
        if (index < 0 || index >= kFormants)
            return 0.0;

        return bands_[static_cast<std::size_t> (index)].q;
    }

    /// The peak gain formant `index` currently has, in dB relative to the first
    /// formant of [ɑ] -- the reference the paper's table uses. Q is divided
    /// back out, so this is the amplitude the table states rather than what the
    /// resonator's sharpness happens to make of it.
    [[nodiscard]] double formantAmplitudeDb (int index) const noexcept
    {
        if (index < 0 || index >= kFormants)
            return 0.0;

        const auto& band = bands_[static_cast<std::size_t> (index)];

        return 20.0 * std::log10 (std::max (band.gain * band.q, 1.0e-12));
    }

    // -----------------------------------------------------------------------
    // Running
    // -----------------------------------------------------------------------

    void process (double& left, double& right) noexcept
    {
        left = processChannel (channels_[0], left);
        right = processChannel (channels_[1], right);
    }

private:
    struct BandSetup
    {
        double frequency { 0.0 };
        double q { 1.0 };
        double g { 0.0 };
        double k { 1.0 };
        double denominator { 1.0 };
        double gain { 1.0 };
    };

    struct BandState
    {
        double s1 { 0.0 };
        double s2 { 0.0 };
    };

    struct Channel
    {
        std::array<BandState, kFormants> bands {};
        BandState notch {};
    };

    [[nodiscard]] double processChannel (Channel& channel, double input) noexcept
    {
        // A fast path, not the mechanism -- see Comb.hpp. The resonators are
        // still run so that turning the mix up does not start from cold state.
        double wet = 0.0;

        for (int index = 0; index < kFormants; ++index)
        {
            const auto& setup = bands_[static_cast<std::size_t> (index)];
            auto& state = channel.bands[static_cast<std::size_t> (index)];

            // The same TPT state-variable as SvfFilter, without the rail: a
            // formant filter is a linear thing and its job is to shape, not to
            // saturate. Whatever drives it can be saturated on its own.
            const double highpass = (input - state.s1 * (setup.g + setup.k) - state.s2)
                                      * setup.denominator;

            const double bandpass = highpass * setup.g + state.s1;
            state.s1 = bandpass + highpass * setup.g;

            const double lowpass = bandpass * setup.g + state.s2;
            state.s2 = lowpass + bandpass * setup.g;

            wet += bandpass * setup.gain;
        }

        // **The anti-formant.** A notch rather than a peak: the nasal cavity is
        // a side branch and a side branch cancels. Applied to the wet sum, so
        // it cuts the vowel the three resonances just built rather than the dry
        // signal that bypasses them.
        //
        // At zero depth it is bit-exactly out of the path, which CLAUDE.md
        // section 7 asks of anything permanently in the signal path. The state
        // is still advanced, so raising the depth does not start from cold.
        //
        // **The branch is a fast path, not the mechanism** -- worth being exact
        // about, because it looks like the mechanism. `wet - 0.0 * anything` is
        // already `wet` bit for bit in IEEE arithmetic, so the bypass holds with
        // or without the test; removing it does not fail the bit-exactness
        // test, and that is the correct outcome rather than a gap in the test.
        // The same note as Comb.hpp's damping branch, for the same reason.
        {
            auto& state = channel.notch;

            const double highpass = (wet - state.s1 * (notch_.g + notch_.k) - state.s2)
                                      * notch_.denominator;

            const double bandpass = highpass * notch_.g + state.s1;
            state.s1 = bandpass + highpass * notch_.g;

            const double lowpass = bandpass * notch_.g + state.s2;
            state.s2 = lowpass + bandpass * notch_.g;

            if (! isExactlyZero (notchDepth_))
            {
                // The band-reject node is the input less the damped bandpass.
                // Scaling how much of it is removed makes the depth continuous
                // from transparent to a full null.
                wet -= notchDepth_ * notch_.k * bandpass;
            }
        }

        if (isExactlyZero (mix_))
            return input;

        return input + mix_ * (wet - input);
    }

    void updateCoefficients() noexcept
    {
        // Where along the vowel list the morph currently sits.
        const double position = morph_ * (kVowels - 1);
        const double lower = std::floor (position);
        const double blend = position - lower;

        const auto first = static_cast<std::size_t> (std::clamp (lower, 0.0, kVowels - 1.0));
        const auto second = static_cast<std::size_t> (std::clamp (lower + 1.0, 0.0, kVowels - 1.0));

        const double width = kWidest * std::pow (kNarrowest / kWidest, sharpness_);

        // Nothing is locked without a note to lock to, so the narrowing goes
        // with it -- otherwise releasing the last note would leave the filter
        // ringing at a bandwidth it has no reason to have.
        const double lockAmount = noteHz_ > 0.0 ? lock_ : 0.0;

        for (int index = 0; index < kFormants; ++index)
        {
            auto& band = bands_[static_cast<std::size_t> (index)];

            const double a = kFrequencies[first][static_cast<std::size_t> (index)];
            const double b = kFrequencies[second][static_cast<std::size_t> (index)];

            // Geometric, so half way between "ee" and "eh" is 378 Hz and not
            // 400 -- which is where a mouth puts it.
            double frequency = a * std::pow (b / a, blend);

            // **The harmonic lock.** The three resonances move onto harmonics
            // N, N+1 and N+2 of the played note. Consecutive rather than the
            // vowel's own ratios, because the point is to select *one* partial
            // and let the neighbours reinforce it -- which is what a tract
            // doing sygyt is arranged to do.
            //
            // Blended geometrically with the vowel position, like everything
            // else here, so partway is a real intermediate rather than a
            // crossfade between two filters.
            if (lockAmount > 0.0)
            {
                const double partial = noteHz_ * (harmonic_ + index);

                frequency *= std::pow (partial / frequency, lockAmount);
            }

            const double bandwidth = kBandwidths[static_cast<std::size_t> (index)] * width
                                       * std::pow (kLockedNarrowing, lockAmount);

            band.frequency = std::clamp (frequency, 20.0, sampleRate_ * kMaximumCutoffFraction);
            band.q = std::max (band.frequency / bandwidth, 0.5);

            band.g = std::tan (3.141592653589793 * band.frequency / sampleRate_);
            band.k = 1.0 / band.q;
            band.denominator = 1.0 / (1.0 + band.g * (band.g + band.k));

            // Blended **in decibels**, which is the linear interpolation of a
            // logarithm and so a geometric blend of the amplitudes -- the same
            // shape as the frequency blend above, and for the same reason: the
            // ear hears ratios. Interpolating the linear gains instead would
            // make the midpoint between a 0 dB and a -24 dB formant sit at
            // -6 dB rather than -12.
            const double decibels = kAmplitudesDb[first][static_cast<std::size_t> (index)]
                                  + blend * (kAmplitudesDb[second][static_cast<std::size_t> (index)]
                                             - kAmplitudesDb[first][static_cast<std::size_t> (index)]);

            // The bandpass node reads Q at its own corner, so dividing by Q
            // makes the stated amplitude the peak gain rather than the peak
            // gain times however sharp the resonance happens to be. Without it
            // the sharpness control is a volume control.
            band.gain = std::pow (10.0, decibels / 20.0) / band.q;
        }

        // The anti-formant. Broad, because a cancellation is broad -- a nasal
        // zero is nothing like a formant in shape.
        notch_.frequency = std::clamp (notchHz_, 20.0, sampleRate_ * kMaximumCutoffFraction);
        notch_.q = kNotchQ;
        notch_.g = std::tan (3.141592653589793 * notch_.frequency / sampleRate_);
        notch_.k = 1.0 / notch_.q;
        notch_.denominator = 1.0 / (1.0 + notch_.g * (notch_.g + notch_.k));
        notch_.gain = 1.0;
    }

    double sampleRate_ { 48000.0 };

    double morph_ { 0.0 };
    double sharpness_ { 0.5 };
    double mix_ { 0.0 };

    double noteHz_ { 0.0 };
    double harmonic_ { 1.0 };
    double lock_ { 0.0 };

    double notchHz_ { 1000.0 };
    double notchDepth_ { 0.0 };

    std::array<BandSetup, kFormants> bands_ {};
    BandSetup notch_ {};
    Channel channels_[2];
};

} // namespace tezla::dsp
