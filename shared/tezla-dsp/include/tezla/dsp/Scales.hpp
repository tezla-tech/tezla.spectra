#pragma once

// The built-in scales, each **generated from its definition** rather than
// shipped as a table of numbers.
//
// ---------------------------------------------------------------------------
// Why generated
// ---------------------------------------------------------------------------
//
// A Pythagorean scale is a chain of 3/2s. Quarter-comma meantone is a chain of
// fifths each narrowed until four of them make a pure 5/4. Bohlen-Pierce is
// thirteen equal parts of 3/1. Those are arithmetic, and writing the arithmetic
// down is both correct and avoids shipping anybody's data files -- CLAUDE.md
// sections 2.1 and 9.
//
// It also makes them **checkable**. A table of cents can only be compared
// against another table; a construction can be tested against the property it
// was built for. So `tests/test_Scales.cpp` asserts that a Pythagorean fifth is
// 701.955 cents and not 700, that four meantone fifths make an exact 5/4, that
// Werckmeister III's C-E is 390.225 cents, and that Carlos Gamma's major third
// is a quarter of a cent from just. Those are the definitions, not
// measurements of an implementation.
//
// ---------------------------------------------------------------------------
// What is deliberately not here
// ---------------------------------------------------------------------------
//
// Javanese slendro and pelog, the maqam sets and the 22 shruti are **not
// built in**, and that is not an oversight. They have no canonical tuning: a
// slendro is whatever a particular gamelan was tuned to, and any specific set
// of numbers would be one instrument's measurements presented as a standard.
// Shipping a made-up one would be worse than shipping none.
//
// They are exactly what `.scl` loading is for, and the Scala archive has
// hundreds of them measured from real instruments. `ScalaFile.hpp` loads them.
//
// The three historical temperaments below are a middle case and are marked as
// such in docs/DSP-REFERENCES.md: their *constructions* are written out here
// and tested against their defining intervals, but which fifths get tempered
// comes from general reference rather than from a source that could be read.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Tuning.hpp"

namespace tezla::dsp::scales {

/// The Pythagorean comma: twelve fifths against seven octaves.
///
///     3^12 / 2^19 = 531441 / 524288 = 23.460 cents
///
/// The amount by which a chain of pure fifths overshoots, and the thing every
/// keyboard temperament exists to hide somewhere.
inline constexpr double kPythagoreanComma = 531441.0 / 524288.0;

/// The syntonic comma: four fifths against a pure major third.
///
///     81 / 80 = 21.506 cents
inline constexpr double kSyntonicComma = 81.0 / 80.0;

/// Folds a ratio into `[1, repeat)`.
[[nodiscard]] inline double fold (double ratio, double repeat = 2.0) noexcept
{
    if (! (ratio > 0.0) || ! (repeat > 1.0))
        return 1.0;

    while (ratio >= repeat)
        ratio /= repeat;

    while (ratio < 1.0)
        ratio *= repeat;

    return ratio;
}

/// `(3/2)^k` folded into an octave, in exact integer arithmetic before the one
/// division that rounds.
///
/// Going **up** the chain, a running `x1.5` is already exact: both 1.5 and 0.5
/// are exact in binary and the mantissa does not run out over twelve fifths.
/// Going down is not, because dividing by 1.5 rounds. Measured over the twelve
/// degrees of the Pythagorean scale, the running product differs from the
/// integer route exactly once -- at -5 fifths, the limma:
///
///     integers      1.0534979423868314      = 256/243, correctly rounded
///     running x1.5  1.0534979423868311      three in the last place low
///
/// One ulp on one degree, which is worth saying plainly rather than dressing
/// up: nobody hears it. It is here because the integer form is no harder to
/// write, and because a scale is the one place in the instrument where the
/// numbers are supposed to be the numbers -- `tests/test_Tuning.cpp` asserts
/// that degree bit-exactly, which is what makes the difference visible at all.
[[nodiscard]] inline double pythagoreanFifths (int fifths) noexcept
{
    long long numerator = 1;
    long long denominator = 1;

    for (int i = 0; i < fifths; ++i)
        numerator *= 3, denominator *= 2;

    for (int i = 0; i < -fifths; ++i)
        numerator *= 2, denominator *= 3;

    // Fold in the integers, so nothing rounds until the end.
    while (numerator >= 2 * denominator)
        denominator *= 2;

    while (numerator < denominator)
        numerator *= 2;

    return static_cast<double> (numerator) / static_cast<double> (denominator);
}

// ---------------------------------------------------------------------------
// Equal divisions
// ---------------------------------------------------------------------------

/// `divisions` equal parts of `repeat`. The whole equal-temperament family,
/// octave-repeating or not, from one line of arithmetic.
[[nodiscard]] inline Scale equalDivisions (std::string name, int divisions, double repeat = 2.0)
{
    Scale scale;

    scale.name = std::move (name);
    scale.ratios.clear();
    scale.repeat = repeat;

    const int count = divisions > 0 ? divisions : 1;

    scale.ratios.reserve (static_cast<std::size_t> (count));

    for (int degree = 0; degree < count; ++degree)
        scale.ratios.push_back (std::pow (repeat, static_cast<double> (degree) / count));

    return scale;
}

[[nodiscard]] inline Scale twelveToneEqual() { return equalDivisions ("12-TET", 12); }
[[nodiscard]] inline Scale fiveToneEqual() { return equalDivisions ("5-TET", 5); }
[[nodiscard]] inline Scale sevenToneEqual() { return equalDivisions ("7-TET", 7); }
[[nodiscard]] inline Scale nineteenToneEqual() { return equalDivisions ("19-TET", 19); }
[[nodiscard]] inline Scale thirtyOneToneEqual() { return equalDivisions ("31-TET", 31); }
[[nodiscard]] inline Scale fiftyThreeToneEqual() { return equalDivisions ("53-TET", 53); }

/// Quarter tones. The scaffolding a maqam sits on, though not a maqam itself --
/// see the header on why those are `.scl` files rather than built-ins.
[[nodiscard]] inline Scale quarterTones() { return equalDivisions ("24-TET", 24); }

// ---------------------------------------------------------------------------
// Non-octave
// ---------------------------------------------------------------------------

/// Bohlen-Pierce: thirteen equal parts of **3/1**, the tritave.
///
/// The most useful strange scale in the set, because it is built on odd
/// harmonics only -- 3:5:7 rather than 4:5:6 -- so it consonates against
/// exactly the harmonics a square wave and an odd-order saturator produce.
/// A repeat interval of 3 also puts the comb's key tracking somewhere no
/// twelve-tone instrument can reach.
[[nodiscard]] inline Scale bohlenPierce() { return equalDivisions ("Bohlen-Pierce", 13, 3.0); }

/// The Carlos scales: equal divisions of the perfect **fifth**, so they never
/// repeat at the octave at all.
///
/// Each is derived by dividing 3/2 into the number of parts that best splits
/// the difference between a just minor third and a just major third:
///
///     Alpha    9 steps of 77.9950 cents   minor third 3.66 flat,  major 3.66 sharp
///     Beta    11 steps of 63.8141 cents   minor third 3.43 sharp, major 3.43 flat
///     Gamma   20 steps of 35.0978 cents   minor third 0.24 sharp, major 0.24 flat
///
/// Gamma is the famous one: its triads are fifteen times closer to just than
/// Alpha's, and more in tune than anything with an octave in it -- at the cost
/// of having no octave. Those error figures are the
/// derivation and are what tests/test_Scales.cpp checks.
[[nodiscard]] inline Scale carlosAlpha() { return equalDivisions ("Carlos Alpha", 9, 1.5); }
[[nodiscard]] inline Scale carlosBeta() { return equalDivisions ("Carlos Beta", 11, 1.5); }
[[nodiscard]] inline Scale carlosGamma() { return equalDivisions ("Carlos Gamma", 20, 1.5); }

// ---------------------------------------------------------------------------
// Pure
// ---------------------------------------------------------------------------

/// Pythagorean: twelve notes from a chain of pure 3/2s, from -5 to +6 fifths.
///
/// That range is what puts the wolf between F# and Db and leaves every other
/// fifth pure. Its major third is 81/64 -- 407.82 cents, 21.5 cents wide of a
/// just 5/4 -- which is why it sounds harsh in thirds and perfect in fifths,
/// and why everything after it is an attempt to trade one for the other.
[[nodiscard]] inline Scale pythagorean()
{
    Scale scale;

    scale.name = "Pythagorean";
    scale.ratios.clear();
    scale.repeat = 2.0;

    // Fifths -5..+6, sorted into pitch order.
    std::vector<double> pitches;

    for (int fifths = -5; fifths <= 6; ++fifths)
        pitches.push_back (pythagoreanFifths (fifths));

    std::sort (pitches.begin(), pitches.end());

    scale.ratios = pitches;
    scale.ratios[0] = 1.0;

    return scale;
}

/// Five-limit just intonation, major. Seven notes, all small whole numbers.
///
///     1/1  9/8  5/4  4/3  3/2  5/3  15/8
///
/// The one that agrees with the comb: every degree is a ratio of small
/// integers, so its harmonics coincide with the played note's rather than
/// beating against them.
[[nodiscard]] inline Scale justMajor()
{
    Scale scale;

    scale.name = "Just major (5-limit)";
    scale.ratios = { 1.0, 9.0 / 8.0, 5.0 / 4.0, 4.0 / 3.0, 3.0 / 2.0, 5.0 / 3.0, 15.0 / 8.0 };
    scale.repeat = 2.0;

    return scale;
}

/// Five-limit just intonation, minor.
///
///     1/1  9/8  6/5  4/3  3/2  8/5  9/5
[[nodiscard]] inline Scale justMinor()
{
    Scale scale;

    scale.name = "Just minor (5-limit)";
    scale.ratios = { 1.0, 9.0 / 8.0, 6.0 / 5.0, 4.0 / 3.0, 3.0 / 2.0, 8.0 / 5.0, 9.0 / 5.0 };
    scale.repeat = 2.0;

    return scale;
}

/// The harmonic series itself, 8:9:10:11:12:13:14:15, repeating at 16/8 = 2.
///
/// Eight notes that *are* harmonics of the tonic, so a comb key-tracked onto
/// the root lands on every one of them. The 11th and 13th harmonics are the
/// famously strange ones -- neither is anywhere near a twelve-tone degree --
/// and they are the reason this is worth having rather than being a curiosity.
[[nodiscard]] inline Scale harmonicSeries()
{
    Scale scale;

    scale.name = "Harmonic series 8-16";
    scale.ratios.clear();
    scale.repeat = 2.0;

    for (int harmonic = 8; harmonic < 16; ++harmonic)
        scale.ratios.push_back (harmonic / 8.0);

    return scale;
}

// ---------------------------------------------------------------------------
// Historical temperaments
// ---------------------------------------------------------------------------

/// Builds a twelve-note scale from a chain of fifths, each of which may be
/// narrowed by its own amount.
///
/// `narrowing[i]` is the factor the i-th fifth of the chain is divided by, with
/// the chain running C-G-D-A-E-B-F#-C#-G#-D#-A#-F. A factor of 1 is a pure
/// fifth. This is the shape every keyboard temperament has: a Pythagorean
/// comma's worth of error to hide, and a choice about where to hide it.
[[nodiscard]] inline Scale temperedChain (std::string name, const double (&narrowing)[12])
{
    Scale scale;

    scale.name = std::move (name);
    scale.repeat = 2.0;

    std::vector<double> pitches (12, 1.0);

    double running = 1.0;

    for (int step = 0; step < 11; ++step)
    {
        running = fold (running * 1.5 / narrowing[static_cast<std::size_t> (step)]);
        pitches[static_cast<std::size_t> (step + 1)] = running;
    }

    std::sort (pitches.begin(), pitches.end());

    scale.ratios = pitches;
    scale.ratios[0] = 1.0;

    return scale;
}

/// Quarter-comma meantone: every fifth in the chain narrowed by a quarter of a
/// syntonic comma, so that four of them make an **exactly pure 5/4**.
///
/// That is the definition and the test: `(3/2 / (81/80)^0.25)^4`, folded, is
/// 1.25 to the last digit a double holds. Eleven such fifths leave one
/// enormous wolf, which is the price and the reason it was eventually
/// abandoned.
[[nodiscard]] inline Scale quarterCommaMeantone()
{
    const double quarter = std::pow (kSyntonicComma, 0.25);
    const double narrowing[12] = { quarter, quarter, quarter, quarter, quarter, quarter,
                                   quarter, quarter, quarter, quarter, quarter, quarter };

    return temperedChain ("Quarter-comma meantone", narrowing);
}

/// Werckmeister III: four fifths narrowed by a quarter of a Pythagorean comma,
/// the other eight pure.
///
/// The tempered ones are C-G, G-D, D-A and B-F#. Every key is playable and
/// each has its own colour, which is the whole point of a well temperament as
/// against an equal one. Its C-E is 390.225 cents -- between Pythagorean's
/// 407.82 and just's 386.31, and much closer to just.
[[nodiscard]] inline Scale werckmeisterThree()
{
    const double quarter = std::pow (kPythagoreanComma, 0.25);

    // Chain order: C-G, G-D, D-A, A-E, E-B, B-F#, F#-C#, C#-G#, G#-D#, D#-A#, A#-F.
    const double narrowing[12] = { quarter, quarter, quarter, 1.0,
                                   1.0,     quarter, 1.0,     1.0,
                                   1.0,     1.0,     1.0,     1.0 };

    return temperedChain ("Werckmeister III", narrowing);
}

/// Vallotti: six consecutive fifths narrowed by a sixth of a Pythagorean comma,
/// the other six pure.
///
/// The tempered ones are F-C-G-D-A-E-B, which in the C-based chain above is the
/// first five plus the last. Smoother than Werckmeister and the most even of
/// the well temperaments -- the closest thing to equal temperament that still
/// has key colour.
[[nodiscard]] inline Scale vallotti()
{
    const double sixth = std::pow (kPythagoreanComma, 1.0 / 6.0);

    const double narrowing[12] = { sixth, sixth, sixth, sixth,
                                   sixth, 1.0,   1.0,   1.0,
                                   1.0,   1.0,   sixth, 1.0 };

    return temperedChain ("Vallotti", narrowing);
}

/// Kirnberger III: the four fifths C-G-D-A-E narrowed by a quarter of a
/// **syntonic** comma, so C-E is an exactly pure 5/4, and the rest pure but for
/// a schisma tucked into F#-C#.
///
/// A meantone home key with the remoter ones left Pythagorean, which is the
/// compromise its name is attached to.
[[nodiscard]] inline Scale kirnbergerThree()
{
    const double quarter = std::pow (kSyntonicComma, 0.25);

    // The schisma: what is left over once the syntonic comma has been taken
    // out of the Pythagorean one.
    const double schisma = kPythagoreanComma / kSyntonicComma;

    const double narrowing[12] = { quarter, quarter, quarter, quarter,
                                   1.0,     1.0,     schisma, 1.0,
                                   1.0,     1.0,     1.0,     1.0 };

    return temperedChain ("Kirnberger III", narrowing);
}

// ---------------------------------------------------------------------------
// Ancient
// ---------------------------------------------------------------------------

/// A Greek octave scale built from two identical tetrachords and a whole tone.
///
/// A tetrachord spans a pure 4/3 and is divided by three intervals; the octave
/// is two of them joined by a 9/8. That is the construction every ancient Greek
/// genus shares, and the three intervals are what distinguishes one from
/// another.
[[nodiscard]] inline Scale tetrachordScale (std::string name, double first, double second,
                                            double third)
{
    Scale scale;

    scale.name = std::move (name);
    scale.repeat = 2.0;

    const double a = first;
    const double b = first * second;
    const double c = first * second * third;   // should be 4/3

    scale.ratios = { 1.0, a, b, c, 1.5, 1.5 * a, 1.5 * b };

    return scale;
}

/// Archytas' enharmonic genus: 28/27, 36/35, 5/4.
///
/// Two intervals so small they are barely steps -- 63 and 49 cents -- followed
/// by a pure major third. Fourth-century BC, and the closest thing in the set
/// to a scale nobody expects.
[[nodiscard]] inline Scale archytasEnharmonic()
{
    return tetrachordScale ("Archytas enharmonic", 28.0 / 27.0, 36.0 / 35.0, 5.0 / 4.0);
}

/// Archytas' diatonic genus: 28/27, 8/7, 9/8. The recognisable one.
[[nodiscard]] inline Scale archytasDiatonic()
{
    return tetrachordScale ("Archytas diatonic", 28.0 / 27.0, 8.0 / 7.0, 9.0 / 8.0);
}

/// Archytas' chromatic genus: 28/27, 243/224, 32/27.
[[nodiscard]] inline Scale archytasChromatic()
{
    return tetrachordScale ("Archytas chromatic", 28.0 / 27.0, 243.0 / 224.0, 32.0 / 27.0);
}

// ---------------------------------------------------------------------------
// The list
// ---------------------------------------------------------------------------

/// Every built-in scale, in the order a menu should show them.
///
/// **Append-only if this is ever indexed by a choice parameter.** Today it is
/// a menu the plugin turns into a name, and the name is what gets stored --
/// which is the more robust choice for a list that will grow, and the reason
/// to do it that way rather than storing the index. CLAUDE.md section 8.
[[nodiscard]] inline std::vector<Scale> all()
{
    return {
        twelveToneEqual(),
        justMajor(),
        justMinor(),
        pythagorean(),
        harmonicSeries(),
        quarterCommaMeantone(),
        werckmeisterThree(),
        kirnbergerThree(),
        vallotti(),
        archytasEnharmonic(),
        archytasDiatonic(),
        archytasChromatic(),
        bohlenPierce(),
        carlosAlpha(),
        carlosBeta(),
        carlosGamma(),
        fiveToneEqual(),
        sevenToneEqual(),
        nineteenToneEqual(),
        quarterTones(),
        thirtyOneToneEqual(),
        fiftyThreeToneEqual(),
    };
}

} // namespace tezla::dsp::scales
