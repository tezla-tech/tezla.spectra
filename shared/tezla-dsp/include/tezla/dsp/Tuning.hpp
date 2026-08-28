// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Scale to frequency: what a MIDI note number is worth in hertz.
//
// ---------------------------------------------------------------------------
// Why microtuning belongs in *this* instrument rather than being a bolt-on
// ---------------------------------------------------------------------------
//
// Sonitus's comb key-tracks onto harmonics of the played note. So if the tuning
// is also harmonic, the whole instrument agrees with itself: intervals beat at
// the rate the comb is combing at, and a chord locks instead of churning.
//
// In twelve-tone equal temperament a "major third" is 386.31 cents wide in the
// harmonic series and 400 in the scale -- 13.69 cents sharp, which at 55 Hz is
// a beat every 1.4 seconds against the comb's own peak. In 5-limit just
// intonation the same third is exactly 5/4 and there is nothing to beat. On a
// sustained bass that difference is not subtle.
//
// ---------------------------------------------------------------------------
// Degrees are ratios, not cents
// ---------------------------------------------------------------------------
//
// A Scala file may give either, and the obvious move is to convert everything
// to cents and work there. The reason not to is subtler than it first looks,
// and worth stating accurately rather than dramatically.
//
// **The round trip is usually exact, and that is the problem.** Taking twenty
// one ratios from the scales in Scales.hpp through `1200*log2(r)` and back
// through `pow(2, c/1200)` on this toolchain, twenty come back bit-identical --
// 3/2, 5/4, 256/243, 243/224, all of them. The one that does not is 5/1, two
// octaves up, which comes back 4.9999999999999991.
//
// So it *works here*. It works because glibc's `log2` and `pow` happen to be
// correctly rounded over this range, which is a property of one library on one
// architecture. CLAUDE.md section 10 keeps an ARM64 job and a Windows job
// precisely because that class of assumption does not travel, and this project
// has already found one case where it did not.
//
// A ratio line stored as a ratio has nothing to depend on: 3/2 is 1.5 on every
// machine that has doubles. A cents line is converted once, at load, where one
// rounding is all there is. `cents()` exists for display and for tests, and is
// the only place the transcendental appears.
//
// ---------------------------------------------------------------------------
// The repeat interval is not necessarily an octave
// ---------------------------------------------------------------------------
//
// Scala calls the last entry of a scale the "formal octave" and it is under no
// obligation to be 2/1. Bohlen-Pierce repeats at 3/1; the Carlos scales do not
// repeat at all within any small ratio. Those are the genuinely strange ones
// and most of the reason to have this: a repeat that is not 2/1 puts the comb's
// key tracking somewhere no twelve-tone instrument can reach.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "Exact.hpp"

namespace tezla::dsp {

/// Floor division, which `/` is not for negative numerators: -9 / 12 is 0 in
/// C++ and -1 here.
///
/// Getting this wrong puts every note below the root on the wrong degree *and*
/// in the wrong octave, and the two errors partly cancel -- so it sounds nearly
/// right, which is the worst way to be wrong. A free function rather than a
/// member because the `.kbm` parser needs the same arithmetic to work out which
/// key the reference note lands on.
[[nodiscard]] inline int floorDivide (int numerator, int denominator) noexcept
{
    const int quotient = numerator / denominator;

    return (numerator % denominator != 0 && ((numerator < 0) != (denominator < 0)))
             ? quotient - 1
             : quotient;
}

/// A scale: the degrees within one repeat, and the interval it repeats at.
///
/// `ratios[0]` is the tonic and is always exactly 1.0. `ratios.size()` is how
/// many notes there are per repeat -- twelve for 12-TET, thirteen for
/// Bohlen-Pierce, seven for a diatonic just scale.
struct Scale
{
    std::string name;
    std::vector<double> ratios { 1.0 };
    double repeat { 2.0 };

    /// The construction, in one sentence -- the theorem the scale is generated
    /// from ("a chain of pure 3/2s, -5 to +6 fifths"). Display text: the panel
    /// shows it beside the degree table, so the player can see *why* the
    /// numbers are what they are. Empty for a scale loaded from a file.
    std::string construction;

    /// Where it comes from and why it matters, a few sentences. Same audience
    /// as `construction`; same emptiness for file-loaded scales.
    std::string story;

    /// What the tradition actually tuned *to*, when that is known -- shown
    /// bold on the panel. Mesopotamia left no absolute pitch, Partch fixed
    /// his 1/1 at G-392, the baroque settles on A415 in modern practice.
    /// Empty means the panel shows the honest generic line: intervals only,
    /// no inherent frequency.
    std::string pitchStandard;

    /// The pitch standard as a number the panel can *apply*, expressed as an
    /// A4 value -- 415 for the baroque temperaments, 440 for 12-TET's ISO
    /// standard. Zero means there is no single number to apply, which is the
    /// truthful state for Babylon, Greece, Persia and the lu: their stories
    /// say why, and no button pretends otherwise. Partch's G-392 is a *root*
    /// standard rather than an A-standard, so it stays prose too.
    double suggestedConcertHz { 0.0 };

    [[nodiscard]] int size() const noexcept { return static_cast<int> (ratios.size()); }

    /// The degree's size in cents above the tonic. For display and for tests;
    /// the audio path uses the ratio.
    [[nodiscard]] double cents (int degree) const noexcept
    {
        if (degree < 0 || degree >= size())
            return 0.0;

        return 1200.0 * std::log2 (ratios[static_cast<std::size_t> (degree)]);
    }

    [[nodiscard]] double repeatCents() const noexcept { return 1200.0 * std::log2 (repeat); }

    /// Whether this is a scale a `Tuning` can actually use.
    ///
    /// Checked rather than assumed, because a scale can arrive from a file and
    /// a zero-size or non-ascending one produces silence or nonsense rather
    /// than an error the player can act on.
    [[nodiscard]] bool isUsable() const noexcept
    {
        if (ratios.empty() || ! (repeat > 1.0))
            return false;

        if (! isExactly (ratios[0], 1.0))
            return false;

        for (std::size_t i = 1; i < ratios.size(); ++i)
            if (! (ratios[i] > ratios[i - 1]) || ! (ratios[i] < repeat))
                return false;

        return true;
    }
};

/// A whole-number ratio recovered from a double, when the double *is* one.
///
/// For the tuning panel's degree table: a just degree stored as the double
/// nearest 27/22 should read "27/22", and an equal-tempered degree should
/// *not* read as whatever fraction happens to pass nearby. The test is
/// therefore **near-exactness, not closeness**: the built-in scales compute
/// their rational degrees as one division of exact integers, so the double
/// sits within an ulp or two of p/q -- while the best fraction under any
/// sane denominator cap misses a tempered degree by a factor of a thousand
/// or more. A cents tolerance cannot make that distinction: convergents get
/// inside a hundredth of a cent of *anything* once denominators reach a few
/// hundred, which is exactly the lie this must not tell.
///
/// The denominator cap covers the deep 3-limit ratios -- the twelve lu run
/// up to 177147/131072 and deserve to be printed as what they are.
struct Fraction
{
    long long numerator { 0 };
    long long denominator { 1 };
    bool found { false };
};

/// Recovers p/q by continued fractions, accepted only if the input matches
/// the fraction to within `relativeTolerance` -- a few ulps, not a musical
/// distance. Continued fractions rather than a search because their
/// convergents are provably the best approximations at each denominator
/// size; the loop is a couple of dozen iterations at most.
[[nodiscard]] inline Fraction nearestFraction (double ratio,
                                               long long maximumDenominator = 200000,
                                               double relativeTolerance = 1.0e-13) noexcept
{
    Fraction result;

    if (! (ratio > 0.0))
        return result;

    // Convergents p/q of the continued-fraction expansion.
    long long p0 = 0, q0 = 1;
    long long p1 = 1, q1 = 0;
    double remainder = ratio;

    for (int term = 0; term < 40; ++term)
    {
        const double floored = std::floor (remainder);

        if (floored > 1.0e15)
            break;

        const long long a = static_cast<long long> (floored);
        const long long p2 = a * p1 + p0;
        const long long q2 = a * q1 + q0;

        if (q2 > maximumDenominator)
            break;

        p0 = p1; q0 = q1;
        p1 = p2; q1 = q2;

        const double approximation = static_cast<double> (p1) / static_cast<double> (q1);

        if (std::abs (approximation / ratio - 1.0) < relativeTolerance)
        {
            result.numerator = p1;
            result.denominator = q1;
            result.found = true;
            return result;
        }

        const double fractional = remainder - floored;

        if (fractional < 1.0e-12)
            break;

        remainder = 1.0 / fractional;
    }

    return result;
}

/// A Scala keyboard map: which scale degree each key plays.
///
/// The default is the identity -- key `middleNote` plays degree 0, the next key
/// plays degree 1, and the pattern repeats every `scale.size()` keys. That is
/// what you want for 19-TET or Bohlen-Pierce, where the point is to get at all
/// the degrees. A map matters when a scale has some other number of notes than
/// the keyboard has keys per octave.
struct KeyboardMap
{
    /// 0 means "no map": linear, one key per degree.
    int size { 0 };

    int firstNote { 0 };
    int lastNote { 127 };

    /// The key that plays degree 0.
    int middleNote { 60 };

    /// The key whose frequency is pinned, and to what.
    int referenceNote { 69 };
    double referenceHz { 440.0 };

    /// Which scale degree counts as the formal octave for the map's own
    /// repeat. 0 means "the scale's own repeat interval".
    int formalOctaveDegree { 0 };

    /// What marks a key that plays nothing -- the `x` in a `.kbm` file.
    ///
    /// **Not a negative number, and that was a bug.** The Scala specification
    /// says of the mapping entries: "There is no restriction to the degree
    /// numbers in the mapping ... they can be any number, also negative, also
    /// lie outside the scale range." So `-1` is a legal degree, one step below
    /// the scale's root, and using it as the unmapped marker silenced a key
    /// that a valid file asked to be tuned.
    static constexpr int kUnmapped = std::numeric_limits<int>::min();

    /// One entry per key in the pattern; `kUnmapped` for a key that plays
    /// nothing. Degrees outside 0..size-1 are legal and are resolved by octave
    /// extension, which is what the specification means by "pitches are always
    /// calculated based on octave extension".
    std::vector<int> degrees;
};

class Tuning
{
public:
    /// Notes outside this are refused rather than clamped -- a MIDI note is
    /// 0 to 127 and anything else is a caller's bug, not a tuning question.
    static constexpr int kLowestNote = 0;
    static constexpr int kHighestNote = 127;

    Tuning() { setScale (twelveToneEqual()); }

    /// The one scale that is built in here rather than in Scales.hpp, because
    /// it is the fallback: a Tuning with nothing loaded has to be usable, and
    /// a plugin whose tuning failed to load must still play in tune.
    [[nodiscard]] static Scale twelveToneEqual()
    {
        Scale scale;

        scale.name = "12-TET";
        scale.ratios.clear();
        scale.ratios.reserve (12);

        for (int degree = 0; degree < 12; ++degree)
            scale.ratios.push_back (std::pow (2.0, degree / 12.0));

        scale.repeat = 2.0;

        return scale;
    }

    /// Refuses an unusable scale rather than installing it, and says so.
    bool setScale (const Scale& scale)
    {
        if (! scale.isUsable())
            return false;

        scale_ = scale;

        return true;
    }

    [[nodiscard]] const Scale& getScale() const noexcept { return scale_; }

    /// Exchanges the live scale with `other`, and **allocates nothing**.
    ///
    /// This is how a tuning reaches the audio thread. `setScale` copies, which
    /// means a vector assignment, which means a possible allocation and a
    /// window in which the pointer the audio thread is reading has been freed.
    /// A swap is a pointer exchange: the caller ends up holding the old scale
    /// and destroys it later, on whichever thread it likes.
    ///
    /// Refuses an unusable scale, like `setScale`, and leaves `other` alone
    /// when it does -- a caller that swapped in garbage and got nothing back
    /// would have no way to tell.
    bool swapScale (Scale& other) noexcept
    {
        if (! other.isUsable())
            return false;

        scale_.name.swap (other.name);
        scale_.ratios.swap (other.ratios);
        std::swap (scale_.repeat, other.repeat);

        return true;
    }

    /// The same for the keyboard map, for the same reason.
    void swapKeyboardMap (KeyboardMap& other) noexcept
    {
        other.degrees.swap (map_.degrees);
        std::swap (other.size, map_.size);
        std::swap (other.firstNote, map_.firstNote);
        std::swap (other.lastNote, map_.lastNote);
        std::swap (other.middleNote, map_.middleNote);
        std::swap (other.referenceNote, map_.referenceNote);
        std::swap (other.referenceHz, map_.referenceHz);
        std::swap (other.formalOctaveDegree, map_.formalOctaveDegree);
    }

    void setKeyboardMap (const KeyboardMap& map) { map_ = map; }
    [[nodiscard]] const KeyboardMap& getKeyboardMap() const noexcept { return map_; }

    /// Where degree 0 sits, when there is no keyboard map.
    void setRootNote (int note) noexcept
    {
        rootNote_ = std::clamp (note, kLowestNote, kHighestNote);
    }

    [[nodiscard]] int getRootNote() const noexcept { return rootNote_; }

    /// Pins one key to one frequency. Everything else follows from the scale.
    void setReference (int note, double hz) noexcept
    {
        referenceNote_ = std::clamp (note, kLowestNote, kHighestNote);
        referenceHz_ = hz > 0.0 ? hz : 440.0;
    }

    [[nodiscard]] int getReferenceNote() const noexcept { return referenceNote_; }
    [[nodiscard]] double getReferenceHz() const noexcept { return referenceHz_; }

    /// The pitch standard, expressed as what A440 is moved to. Everything the
    /// tuning produces is scaled by `hz / 440` -- the default reference and a
    /// keyboard map's own reference alike -- so 432 sits the whole instrument
    /// 31.8 cents low whatever note the tuning anchors on. Stated in terms of
    /// A because that is the lingua franca, not because the tuning needs to
    /// contain an A: it is one ratio applied to the lot.
    static constexpr double kMinimumConcertHz = 380.0;
    static constexpr double kMaximumConcertHz = 500.0;

    void setConcertPitch (double hz) noexcept
    {
        concertHz_ = std::clamp (hz > 0.0 ? hz : 440.0, kMinimumConcertHz, kMaximumConcertHz);
    }

    [[nodiscard]] double getConcertPitch() const noexcept { return concertHz_; }

    /// The frequency of a MIDI note, in Hz. **0 means the key is not mapped**
    /// and should play nothing -- a keyboard map is allowed to leave holes, and
    /// a silent key is the correct answer rather than an error.
    [[nodiscard]] double frequencyFor (int note) const noexcept
    {
        if (note < kLowestNote || note > kHighestNote)
            return 0.0;

        const double here = pitchOf (note);

        if (here <= 0.0)
            return 0.0;

        const double reference = pitchOf (usingMap() ? map_.referenceNote : referenceNote_);

        if (reference <= 0.0)
            return 0.0;

        const double hz = usingMap() ? map_.referenceHz : referenceHz_;

        return (concertHz_ / 440.0) * hz * here / reference;
    }

    /// The same thing in cents above the reference. For a display, and for
    /// tests that want to talk about intervals rather than frequencies.
    [[nodiscard]] double centsFor (int note) const noexcept
    {
        const double hz = frequencyFor (note);
        const double reference = usingMap() ? map_.referenceHz : referenceHz_;

        if (hz <= 0.0 || reference <= 0.0)
            return 0.0;

        return 1200.0 * std::log2 (hz / reference);
    }

private:
    [[nodiscard]] bool usingMap() const noexcept
    {
        return map_.size > 0 && ! map_.degrees.empty();
    }

    /// The note's pitch as a bare multiplier, before the reference is applied.
    ///
    /// Zero means unmapped.
    [[nodiscard]] double pitchOf (int note) const noexcept
    {
        const int size = scale_.size();

        if (size <= 0)
            return 0.0;

        if (! usingMap())
        {
            const int index = note - rootNote_;
            const int repeats = floorDivide (index, size);
            const int degree = index - repeats * size;

            return std::pow (scale_.repeat, repeats)
                     * scale_.ratios[static_cast<std::size_t> (degree)];
        }

        if (note < map_.firstNote || note > map_.lastNote)
            return 0.0;

        const int pattern = static_cast<int> (map_.degrees.size());
        const int index = note - map_.middleNote;
        const int repeats = floorDivide (index, pattern);
        const int position = index - repeats * pattern;

        const int degree = map_.degrees[static_cast<std::size_t> (position)];

        if (degree == KeyboardMap::kUnmapped)
            return 0.0;

        // The map's repeat is its own formal octave, which is a *scale degree*
        // rather than necessarily the scale's repeat interval. That is the
        // whole reason the field exists: a 12-note map over a 19-note scale
        // repeats every 12 keys at whatever interval degree 12 happens to be.
        const double octave = formalOctaveRatio();

        const int wholeRepeats = floorDivide (degree, size);
        const int within = degree - wholeRepeats * size;

        return std::pow (octave, repeats)
                 * std::pow (scale_.repeat, wholeRepeats)
                 * scale_.ratios[static_cast<std::size_t> (within)];
    }

    /// How far apart two adjacent repeats of the mapping pattern sit.
    ///
    /// The field is a **scale degree**, and the specification is explicit that
    /// it may lie outside the scale: "If you want a mapping for a double octave
    /// range ... make the scale degree to consider as formal octave parameter
    /// twice the size of the scale." So degree 24 on a 12-note scale has to
    /// give the repeat interval *squared*, and this used to return the repeat
    /// itself -- the exact case the specification calls out, an octave flat
    /// every pattern.
    ///
    /// Zero is the one value read as a convention rather than as a degree: it
    /// means "not set", and the scale's own repeat is what a `.kbm` without an
    /// opinion should get. Degree 0 taken literally is 1/1, which would stack
    /// every pattern on top of the last.
    [[nodiscard]] double formalOctaveRatio() const noexcept
    {
        const int degree = map_.formalOctaveDegree;
        const int size = scale_.size();

        if (degree == 0 || size <= 0)
            return scale_.repeat;

        const int wholeRepeats = floorDivide (degree, size);
        const int within = degree - wholeRepeats * size;

        return std::pow (scale_.repeat, wholeRepeats)
                 * scale_.ratios[static_cast<std::size_t> (within)];
    }

    /// Floor division, which `/` is not for negative numerators: -9 / 12 is 0
    /// in C++ and -1 here. Getting this wrong puts every note below the root on
    /// the wrong degree *and* in the wrong octave, and the two errors partly
    /// cancel -- so it sounds nearly right, which is the worst way to be wrong.


    Scale scale_;
    KeyboardMap map_;

    int rootNote_ { 60 };
    int referenceNote_ { 69 };
    double referenceHz_ { 440.0 };
    double concertHz_ { 440.0 };
};

} // namespace tezla::dsp
