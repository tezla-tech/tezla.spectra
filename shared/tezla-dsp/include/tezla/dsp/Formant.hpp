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
// **These are the widely reproduced Peterson & Barney (1952) adult-male
// averages, quoted from general reference rather than read from the paper** --
// the container's egress proxy refuses the journal. They are rounded to the
// nearest 10 Hz, which is well inside the spread of the original data. The
// citation and that caveat are both recorded in docs/DSP-REFERENCES.md, and
// the paper is on the list of things to fetch.
//
// Bandwidths are held constant per formant rather than per vowel, because a
// formant's bandwidth is set by how lossy the tract is rather than by where the
// resonance sits. So Q changes as the morph moves, which is what happens in a
// real mouth.

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

    /// Peterson & Barney adult-male averages, in Hz. See the header.
    static constexpr double kFrequencies[kVowels][kFormants] = {
        { 270.0, 2290.0, 3010.0 },   // ee
        { 530.0, 1840.0, 2480.0 },   // eh
        { 730.0, 1090.0, 2440.0 },   // ah
        { 570.0,  840.0, 2410.0 },   // oh
        { 300.0,  870.0, 2240.0 },   // oo
    };

    /// Nominal bandwidths, in Hz -- constant per formant, not per vowel.
    static constexpr double kBandwidths[kFormants] = { 80.0, 90.0, 120.0 };

    /// Relative peak amplitudes. F1 carries the vowel, F2 carries most of what
    /// distinguishes one from another, F3 is colour.
    static constexpr double kAmplitudesDb[kFormants] = { 0.0, -7.0, -12.0 };

    /// How far the sharpness control can narrow or widen the bandwidths.
    ///
    /// At the sharp end the resonances ring and the filter sings the vowel; at
    /// the wide end they blur into a broad tilt and it is barely vocal at all.
    /// A factor rather than a Q, because the bandwidths differ per formant and
    /// they should scale together.
    static constexpr double kNarrowest = 0.25;
    static constexpr double kWidest = 4.0;

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
            for (auto& band : channel.bands)
            {
                band.s1 = 0.0;
                band.s2 = 0.0;
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

        for (int index = 0; index < kFormants; ++index)
        {
            auto& band = bands_[static_cast<std::size_t> (index)];

            const double a = kFrequencies[first][static_cast<std::size_t> (index)];
            const double b = kFrequencies[second][static_cast<std::size_t> (index)];

            // Geometric, so half way between "ee" and "eh" is 378 Hz and not
            // 400 -- which is where a mouth puts it.
            const double frequency = a * std::pow (b / a, blend);

            const double bandwidth = kBandwidths[static_cast<std::size_t> (index)] * width;

            band.frequency = std::clamp (frequency, 20.0, sampleRate_ * kMaximumCutoffFraction);
            band.q = std::max (band.frequency / bandwidth, 0.5);

            band.g = std::tan (3.141592653589793 * band.frequency / sampleRate_);
            band.k = 1.0 / band.q;
            band.denominator = 1.0 / (1.0 + band.g * (band.g + band.k));

            // The bandpass node reads Q at its own corner, so dividing by Q
            // makes the stated amplitude the peak gain rather than the peak
            // gain times however sharp the resonance happens to be. Without it
            // the sharpness control is a volume control.
            band.gain = std::pow (10.0, kAmplitudesDb[static_cast<std::size_t> (index)] / 20.0)
                          / band.q;
        }
    }

    double sampleRate_ { 48000.0 };

    double morph_ { 0.0 };
    double sharpness_ { 0.5 };
    double mix_ { 0.0 };

    std::array<BandSetup, kFormants> bands_ {};
    Channel channels_[2];
};

} // namespace tezla::dsp
