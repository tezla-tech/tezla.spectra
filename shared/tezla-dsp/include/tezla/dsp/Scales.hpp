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
// What is deliberately not here, and the line that decides it
// ---------------------------------------------------------------------------
//
// Javanese slendro and pelog and the 22 shruti are **not built in**, and that
// is not an oversight. They have no canonical tuning: a slendro is whatever a
// particular gamelan was tuned to, and any specific set of numbers would be
// one instrument's measurements presented as a standard. Shipping a made-up
// one would be worse than shipping none. They are exactly what `.scl` loading
// is for, and the Scala archive has hundreds measured from real instruments.
//
// The maqam and dastgah entries below do not cross that line, because they
// are not measurements of a practice -- they are **named theorists' published
// constructions**: al-Farabi's ratio for Zalzal's fret, Farhat's mean neutral
// seconds, the Arel-Ezgi-Uzdilek comma grid. A construction with an author is
// buildable and attributable the same way Werckmeister's is; each entry names
// its theorist, and its story says the living practice bends around it.
//
// The historical temperaments are a middle case and are marked as such in
// docs/DSP-REFERENCES.md: their *constructions* are written out here and
// tested against their defining intervals, but which fifths get tempered
// comes from general reference rather than from a source that could be read.
// The same access honesty applies to the Persian, Babylonian and Turkish
// numbers -- the references file says which rest on literature not read here.
//
// ---------------------------------------------------------------------------
// Every scale carries its own story
// ---------------------------------------------------------------------------
//
// `construction` is the theorem -- the one sentence of arithmetic the degrees
// fall out of. `story` is where it comes from and why it matters. They are in
// the library rather than the editor because they are part of what a scale
// *is* here (a construction, not a table), and because a test can then assert
// that no scale ships without them.

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

/// Attaches the construction line and the story to a scale on its way out.
///
/// A helper rather than constructor arguments everywhere, so the arithmetic
/// stays readable and the prose sits beside it instead of inside it.
[[nodiscard]] inline Scale described (Scale scale, std::string construction, std::string story)
{
    scale.construction = std::move (construction);
    scale.story = std::move (story);
    return scale;
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

[[nodiscard]] inline Scale twelveToneEqual()
{
    return described (equalDivisions ("12-TET", 12),
        "Twelve equal parts of the octave: every step is exactly 2^(1/12), every key identical.",
        "The modern default. Nothing in it is pure except the octave -- the fifth is two cents "
        "flat, the major third fourteen cents sharp of 5/4, which is why a sustained 12-TET "
        "third churns against a key-tracked comb while a just one locks. First computed to full "
        "precision by Zhu Zaiyu in China in 1584, decades before any European; standard on "
        "keyboards only since the 19th century.");
}

[[nodiscard]] inline Scale fiveToneEqual()
{
    return described (equalDivisions ("5-TET", 5),
        "Five equal steps of 240 cents.",
        "The skeleton that Javanese slendro hovers around -- each real gamelan differently, "
        "which is why the genuine article is a .scl file of one instrument's measurements. "
        "No interval commits to major or minor; everything floats.");
}

[[nodiscard]] inline Scale sevenToneEqual()
{
    return described (equalDivisions ("7-TET", 7),
        "Seven equal steps of 171.4 cents.",
        "The frame of Thai classical tuning and close to several African traditions: no "
        "semitones, no leading tone, seven gentle steps that refuse the pull of Western "
        "cadence. Melody floats free of harmony's gravity.");
}

[[nodiscard]] inline Scale nineteenToneEqual()
{
    return described (equalDivisions ("19-TET", 19),
        "Nineteen equal steps of 63.2 cents -- third-comma meantone closed into a circle.",
        "Proposed by Francisco de Salinas in 1577. Its thirds are nearly pure and every "
        "meantone piece plays in it with no wolf anywhere -- the Renaissance's tuning problems "
        "solved by adding seven notes. Guitarists like it because the frets still fit.");
}

[[nodiscard]] inline Scale thirtyOneToneEqual()
{
    return described (equalDivisions ("31-TET", 31),
        "Thirty-one equal steps of 38.7 cents -- quarter-comma meantone closed into a circle.",
        "Christiaan Huygens' division, 1691. Thirds land almost just and the harmonic seventh "
        "7/4 is nearly exact, so it speaks both classical and blues. Adriaan Fokker built a "
        "31-tone organ in Haarlem in 1950 and a Dutch school composed for it for decades.");
}

[[nodiscard]] inline Scale fiftyThreeToneEqual()
{
    return described (equalDivisions ("53-TET", 53),
        "Fifty-three equal steps of 22.64 cents -- the Holdrian comma.",
        "The circle of fifths genuinely closes here: 53 fifths miss 31 octaves by a fiftieth "
        "of a cent, which Jing Fang computed in 45 BC and Nicholas Mercator rediscovered. "
        "Fifths and thirds both come out essentially pure, and Turkish theory writes all of "
        "makam music on this grid -- see Rast (AEU) below.");
}

/// Quarter tones. The scaffolding modern Arabic notation sits on, though not a
/// maqam itself -- the practised degrees bend off the grid by ear.
[[nodiscard]] inline Scale quarterTones()
{
    return described (equalDivisions ("24-TET", 24),
        "Twenty-four equal quarter-tones of 50 cents.",
        "Codified as the grid of modern Arabic music theory at the Cairo Congress of 1932, "
        "and proposed for Persian music by Ali-Naqi Vaziri: every maqam degree written as "
        "halves of a semitone. A master's Rast does not actually sit on the grid -- the "
        "half-flat degrees ride higher or lower by maqam and by region -- but this is the "
        "notation's scale, and the doorway most players enter microtonality through.");
}

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
[[nodiscard]] inline Scale bohlenPierce()
{
    return described (equalDivisions ("Bohlen-Pierce", 13, 3.0),
        "Thirteen equal parts of 3/1 -- the tritave -- with no octave anywhere in it.",
        "Discovered three times independently (Heinz Bohlen 1972, Kees van Prooijen, John "
        "Pierce): a scale built on odd harmonics only, consonant on 3:5:7 where ours is on "
        "4:5:6. Square waves and odd-order saturation -- this instrument's home territory -- "
        "consonate in it by construction. Notes an octave apart are not the same note here, "
        "which rewires the ear within minutes.");
}

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
[[nodiscard]] inline Scale carlosAlpha()
{
    return described (equalDivisions ("Carlos Alpha", 9, 1.5),
        "Nine equal steps of a pure 3/2 fifth -- 78.0 cents each; the octave does not exist.",
        "Wendy Carlos, Beauty in the Beast (1986): divide the fifth instead of the octave, "
        "chosen so triads come out near just. Alpha is the coarse sibling -- thirds 3.7 cents "
        "off pure -- with a step size that melody can still walk.");
}

[[nodiscard]] inline Scale carlosBeta()
{
    return described (equalDivisions ("Carlos Beta", 11, 1.5),
        "Eleven equal steps of a pure 3/2 fifth -- 63.8 cents each.",
        "The middle Carlos scale: thirds 3.4 cents from just, the weave one size finer than "
        "Alpha. Like its siblings it has no octave at all, so doublings shimmer instead of "
        "merging -- the sound that carries Beauty in the Beast.");
}

[[nodiscard]] inline Scale carlosGamma()
{
    return described (equalDivisions ("Carlos Gamma", 20, 1.5),
        "Twenty equal steps of a pure 3/2 fifth -- 35.1 cents each.",
        "The famous one: major and minor thirds a quarter of a cent from just -- more in tune "
        "than any octave-repeating scale can ever be, at the price of having no octave. "
        "Chords of impossible purity out of a melody of microscopic steps.");
}

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

    return described (std::move (scale),
        "A chain of pure 3/2 fifths, five down to six up, folded into one octave. Twelve "
        "fifths overshoot seven octaves by 23.46 cents; the leftover lands between F# and Db "
        "as the wolf.",
        "The oldest tuning mathematics in the West, credited to Pythagoras though the "
        "arithmetic is on Babylonian tablets a millennium older. Fifths and fourths are "
        "perfect; the major third is a wide 81/64, which is why medieval theory heard thirds "
        "as dissonances. Fierce in thirds, immaculate in fifths -- everything after it is an "
        "attempt to trade one for the other.");
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

    return described (std::move (scale),
        "Seven degrees, every one a ratio of whole numbers no larger than 15: "
        "1/1 9/8 5/4 4/3 3/2 5/3 15/8.",
        "The scale the harmonic series implies: thirds are pure 5/4 and 6/5, so a sustained "
        "triad does not beat at all. The cost is one home key -- D to A is a wolf 40/27 -- "
        "which is the problem every temperament in this menu exists to solve. With the comb "
        "key-tracked this is the scale where instrument and tuning agree completely.");
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

    return described (std::move (scale),
        "The 5-limit minor: 1/1 9/8 6/5 4/3 3/2 8/5 9/5, every third and sixth pure.",
        "The mirror of the just major -- minor third 6/5, minor sixth 8/5, minor seventh 9/5. "
        "Hugo Riemann's dualism read it as the major scale reflected: the undertone view of "
        "the same arithmetic, which this menu's undertone series makes literal.");
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

    return described (std::move (scale),
        "Harmonics 8 to 15 of one fundamental, played as a scale: 8:9:10:11:12:13:14:15.",
        "Not an approximation of the harmonic series -- the series itself. Every degree sits "
        "on a harmonic of the tonic, so a key-tracked comb lands on every note by definition. "
        "The 11th and 13th harmonics fall between the piano's cracks -- they are what horn "
        "players learn to lip away -- and here they are simply notes.");
}

/// The harmonic series again, an octave higher up it.
///
/// Sixteen steps rather than eight, and the steps get smaller as they climb --
/// which is the point: it is a scale whose own intervals are the intervals the
/// comb's key tracking lands on, so the two agree by construction rather than
/// by luck.
[[nodiscard]] inline Scale harmonicSeriesHigh()
{
    Scale scale;

    scale.name = "Harmonic series 16-32";
    scale.ratios.clear();
    scale.repeat = 2.0;

    for (int harmonic = 16; harmonic < 32; ++harmonic)
        scale.ratios.push_back (harmonic / 16.0);

    return described (std::move (scale),
        "Harmonics 16 to 31: the same series an octave further up, sixteen steps that shrink "
        "from a whole tone to a third of one as they climb.",
        "The upper octave of the bugle's scale. The steps tighten as they rise exactly as the "
        "series does, so the scale's own geometry is a crescendo -- melodies accelerate as "
        "they climb it.");
}

/// The **undertone** series: the harmonic series reflected.
///
/// Where the overtone series divides a string into equal parts, this multiplies
/// it -- 16/16, 16/15, 16/14 and so on up to 16/9 -- so the steps grow as they
/// climb instead of shrinking. It is the minor-shaped mirror of the major-shaped
/// harmonic series, and Harry Partch and Hugo Riemann both built theories on the
/// symmetry.
///
/// **It belongs in this instrument in particular**, because kargyraa is exactly
/// this idea in the time domain: a subharmonic that is the source divided rather
/// than a second note added. A scale of subharmonics under a voice that is
/// generating one is a thing very few instruments can do at all.
[[nodiscard]] inline Scale undertoneSeries()
{
    Scale scale;

    scale.name = "Undertone series 16-9";
    scale.ratios.clear();
    scale.repeat = 2.0;

    for (int divisor = 16; divisor > 8; --divisor)
        scale.ratios.push_back (16.0 / divisor);

    return described (std::move (scale),
        "The harmonic series reflected: 16/16, 16/15, 16/14 ... 16/9 -- frequency divided "
        "instead of multiplied, so the steps grow as they climb.",
        "The subharmonic series. Riemann built a theory of the minor mode on it and Partch "
        "half of his Monophony -- and this instrument generates real subharmonics: kargyraa "
        "is this scale's physics in the time domain, the source divided rather than a note "
        "added. A scale of undertones on a voice that makes its own is a rare alignment.");
}

/// Just intonation extended to the seventh harmonic.
///
/// Five-limit just intonation is built from 2, 3 and 5 and has no home for 7/4
/// -- the harmonic that a brass instrument plays and that a saw wave has plenty
/// of. Adding it gives the flat seventh its pure form, 969 cents rather than
/// 12-TET's 1000, which on a sustained bass is the difference between a seventh
/// that beats and one that locks.
[[nodiscard]] inline Scale justSevenLimit()
{
    Scale scale;

    scale.name = "Just 7-limit";
    scale.repeat = 2.0;
    scale.ratios = { 1.0,
                     16.0 / 15.0,   // 112 c
                     9.0 / 8.0,     // 204 c
                     7.0 / 6.0,     // 267 c -- septimal minor third
                     5.0 / 4.0,     // 386 c
                     4.0 / 3.0,     // 498 c
                     7.0 / 5.0,     // 583 c -- septimal tritone
                     3.0 / 2.0,     // 702 c
                     8.0 / 5.0,     // 814 c
                     5.0 / 3.0,     // 884 c
                     7.0 / 4.0,     // 969 c -- the harmonic seventh
                     15.0 / 8.0 };  // 1088 c

    return described (std::move (scale),
        "Five-limit just intonation extended to the seventh harmonic: 7/6, 7/5 and 7/4 join "
        "the family.",
        "The blue notes, tuned. 7/4 at 969 cents is the seventh a brass section actually "
        "locks to -- 31 cents inside 12-TET's -- and the septimal minor third 7/6 is the dark "
        "narrow one. Barbershop quartets live in this arithmetic without ever naming it.");
}

/// A Pythagorean chain run out to seventeen notes rather than twelve.
///
/// Twelve fifths do not close -- they overshoot by a Pythagorean comma, 23.46
/// cents -- and a twelve-note Pythagorean scale hides that in one unusable
/// "wolf" fifth. Seventeen notes keep going instead of hiding it, which is what
/// medieval Arabic theory did with the same arithmetic, and gives both a sharp
/// and a flat for each of the five black keys.
[[nodiscard]] inline Scale pythagoreanSeventeen()
{
    Scale scale;

    scale.name = "Pythagorean 17";
    scale.repeat = 2.0;
    scale.ratios.clear();

    // Eight fifths up and eight down from the tonic, each folded into the
    // octave. Derived rather than tabulated: the whole scale is 3/2.
    for (int step = -8; step <= 8; ++step)
    {
        double ratio = std::pow (1.5, step);

        while (ratio >= 2.0) ratio /= 2.0;
        while (ratio < 1.0)  ratio *= 2.0;

        scale.ratios.push_back (ratio);
    }

    std::sort (scale.ratios.begin(), scale.ratios.end());

    return described (std::move (scale),
        "The same chain of pure fifths run to seventeen notes, eight up and eight down, so "
        "every black key gets both its sharp and its flat.",
        "What Arabic theory did with Pythagoras' arithmetic: Safi al-Din al-Urmawi's "
        "13th-century seventeen-tone system, the classical framework of maqam scholarship. "
        "Each sharp-flat pair sits a 23.46-cent comma apart -- audible, playable, and the "
        "seed of the Turkish comma system elsewhere in this menu.");
}

[[nodiscard]] inline Scale seventeenToneEqual()
{
    return described (equalDivisions ("17-TET", 17),
        "Seventeen equal steps of 70.6 cents.",
        "The equal-tempered rendering of the seventeen-tone Arabic tradition: fifths land two "
        "cents sharp, and neutral seconds and thirds are built in rather than borrowed. A "
        "modern workhorse for maqam-shaped writing on keyboard-shaped instruments.");
}

[[nodiscard]] inline Scale twentyTwoToneEqual()
{
    return described (equalDivisions ("22-TET", 22),
        "Twenty-two equal steps of 54.5 cents.",
        "The equal frame nearest the Indian claim of twenty-two shruti -- the real shruti are "
        "unequal and belong to .scl files -- and independently one of the great 7-limit "
        "tunings: its intervals organise harmony 12-TET cannot spell, which is why western "
        "microtonalists built new theory (pajara and its relatives) inside it.");
}

[[nodiscard]] inline Scale fortyOneToneEqual()
{
    return described (equalDivisions ("41-TET", 41),
        "Forty-one equal steps of 29.3 cents.",
        "The next genuinely great fifth after 12 and before 53: pure enough for schismatic "
        "harmony, fine enough to give every 7-limit interval its own name. The tuning the "
        "Kite guitar community settled on for exactly that balance.");
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

    return described (temperedChain ("Quarter-comma meantone", narrowing),
        "Every fifth narrowed by a quarter of the syntonic comma, (81/80)^(1/4), so that four "
        "fifths land on an exactly pure 5/4.",
        "The tuning of the Renaissance and early Baroque for the better part of two "
        "centuries: thirds pure and radiant, fifths slightly soft, and one enormous wolf "
        "where the chain fails to close. The sound of Byrd and Frescobaldi -- and the reason "
        "split-key keyboards existed, giving G# and Ab separate levers.");
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

    return described (temperedChain ("Werckmeister III", narrowing),
        "Four fifths -- C-G, G-D, D-A and B-F# -- each narrowed a quarter of the Pythagorean "
        "comma; the other eight left pure.",
        "Andreas Werckmeister, 1691: the famous 'well temperament'. Every key playable, each "
        "with its own colour, the remote ones stormier -- and the strongest candidate for "
        "what Bach meant by wohltemperirt. Its C-E sits at 390 cents: nearly just at home, "
        "Pythagorean out in the sharps.");
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

    return described (temperedChain ("Vallotti", narrowing),
        "The six fifths F-C-G-D-A-E-B narrowed a sixth of the Pythagorean comma each; the "
        "other six pure.",
        "Francesco Vallotti, 18th-century Padua: the smoothest of the well temperaments -- "
        "key colour without key danger. The default 'Baroque tuning' of modern historically "
        "informed performance, which says something about how gracefully it ages.");
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

    return described (temperedChain ("Kirnberger III", narrowing),
        "C-G-D-A-E narrowed a quarter of the *syntonic* comma each, so C-E is an exactly pure "
        "5/4; a two-cent schisma hides in F#-C#; everything else pure.",
        "Johann Kirnberger -- a student of Bach's -- published 1779. A meantone heart inside "
        "a Pythagorean shell: home keys sound like the Renaissance, far keys like the middle "
        "ages, and the seam between the two worlds is one schisma wide.");
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
    return described (
        tetrachordScale ("Archytas enharmonic", 28.0 / 27.0, 36.0 / 35.0, 5.0 / 4.0),
        "Two identical tetrachords of 28/27 x 36/35 x 5/4 -- each spanning an exact 4/3 -- "
        "joined by a 9/8 tone.",
        "Archytas of Tarentum, 4th century BC: Plato's friend, general, and the man who "
        "solved the doubling of the cube. His enharmonic genus crowds two microtonal steps "
        "of 63 and 49 cents at the bottom of each tetrachord, then leaps a pure major third "
        "-- the strangest scale to survive from antiquity, and the earliest documented use "
        "of the numbers 5 and 7 in tuning.");
}

/// Archytas' diatonic genus: 28/27, 8/7, 9/8. The recognisable one.
[[nodiscard]] inline Scale archytasDiatonic()
{
    return described (
        tetrachordScale ("Archytas diatonic", 28.0 / 27.0, 8.0 / 7.0, 9.0 / 8.0),
        "Tetrachords of 28/27 x 8/7 x 9/8, each spanning an exact 4/3.",
        "The everyday genus of Greek music in Archytas' arithmetic, with the wide septimal "
        "tone 8/7 where later theory put 10/9. Ptolemy reports -- half in complaint -- that "
        "this is what lyre players actually tuned.");
}

/// Archytas' chromatic genus: 28/27, 243/224, 32/27.
[[nodiscard]] inline Scale archytasChromatic()
{
    return described (
        tetrachordScale ("Archytas chromatic", 28.0 / 27.0, 243.0 / 224.0, 32.0 / 27.0),
        "Tetrachords of 28/27 x 243/224 x 32/27, each spanning an exact 4/3.",
        "The middle genus, between diatonic and enharmonic: two close steps and a minor "
        "third. The odd-looking 243/224 is no accident -- it is exactly what makes the "
        "three steps close on a pure fourth.");
}

/// Ptolemy's "even" (homalon) diatonic: 12/11, 11/10, 10/9.
///
/// Three superparticular steps as nearly equal as the arithmetic allows, and
/// the product telescopes: 12/11 * 11/10 * 10/9 = 12/9 = 4/3 exactly.
[[nodiscard]] inline Scale ptolemyEven()
{
    return described (
        tetrachordScale ("Ptolemy even diatonic", 12.0 / 11.0, 11.0 / 10.0, 10.0 / 9.0),
        "Tetrachords of 12/11 x 11/10 x 10/9 -- three nearly equal superparticular steps "
        "whose product telescopes to an exact 4/3.",
        "From Ptolemy's Harmonics, 2nd century AD: the homalon or 'even' genus. Its 12/11 "
        "neutral second of 150.6 cents is the earliest documented ancestor of the neutral "
        "intervals that maqam and dastgah music are built on -- the quarter-tone's Greek "
        "grandfather, eight centuries before Zalzal's fret.");
}

// ---------------------------------------------------------------------------
// Persian -- Farhat's theoretical intervals
// ---------------------------------------------------------------------------

/// A scale given directly as cents degrees. For the constructions whose
/// definition *is* a set of measured or theorised sizes -- Farhat's dastgah
/// frames -- rather than a generating rule.
[[nodiscard]] inline Scale centsScale (std::string name, std::vector<double> centsDegrees)
{
    Scale scale;

    scale.name = std::move (name);
    scale.repeat = 2.0;
    scale.ratios.clear();
    scale.ratios.reserve (centsDegrees.size());

    for (const double value : centsDegrees)
        scale.ratios.push_back (std::pow (2.0, value / 1200.0));

    return scale;
}

/// Shur, the mother dastgah, in Hormoz Farhat's theoretical sizes.
///
/// Farhat measures two families of neutral second in Persian practice --
/// 125-145 cents and 150-170 cents -- and proposes their means, 135 and 165,
/// as the theoretical sizes. Here they hang on a pure Pythagorean frame
/// (fourth 498, fifth 702, minor seventh 996), with the tone left over (198)
/// absorbing the flexibility his own doctrine assigns to whole tones. The
/// pure fifth is deliberate: on a bass drone the fifth is the interval that
/// must not beat.
[[nodiscard]] inline Scale shur()
{
    return described (
        centsScale ("Shur (Persian)", { 0.0, 135.0, 300.0, 498.0, 702.0, 792.0, 996.0 }),
        "Farhat's mean neutral seconds -- 135 and 165 cents -- hung on a pure frame of "
        "fourth (498), fifth (702) and minor seventh (996); one flexible tone of 198 "
        "closes the octave.",
        "Shur is the mother dastgah of Persian classical music -- Farhat calls it the "
        "most important of the twelve, and half the repertoire descends from it. The "
        "koron on the second degree is the Persian quarter-tone: not half a semitone but "
        "its own interval, played anywhere from 125 to 145 cents and theorised at 135. "
        "After Hormoz Farhat's The Dastgah Concept in Persian Music (1990); his sizes "
        "are means of a living, flexible practice, not a temperament.");
}

/// Chahargah, the bright ceremonial dastgah, in the same sizes.
///
/// Two *identical* tetrachords of 135 + 270 + 93 cents -- each spanning the
/// pure fourth -- joined by a 204 tone. The 270-cent step is Farhat's "plus
/// tone", the interval that exists in no Western or Arabic theory.
[[nodiscard]] inline Scale chahargah()
{
    return described (
        centsScale ("Chahargah (Persian)", { 0.0, 135.0, 405.0, 498.0, 702.0, 837.0, 1107.0 }),
        "Two identical Persian tetrachords -- 135 + 270 + 93 cents, each spanning a pure "
        "4/3 -- joined by a 204 tone. The 270-cent 'plus tone' is the signature.",
        "The brightest dastgah, the one Persian theory reaches for at moments of ceremony. "
        "A koron sits a neutral second above the tonic and above the fifth, and from each "
        "the melody leaps more than a whole tone to a natural -- Farhat's plus tone, the "
        "specifically Persian interval. The same two-tetrachord architecture as the Greeks, "
        "built from intervals the Greeks never had.");
}

// ---------------------------------------------------------------------------
// Old Babylonian -- the seven tunings of the tablets
// ---------------------------------------------------------------------------

/// One of the seven Old Babylonian tunings: the diatonic of a chain of pure
/// fifths, rotated to a given degree.
///
/// The white-key set F-C-G-D-A-E-B is six fifths of chain; `rotation` picks
/// which of its seven notes is the tonic, and every degree is re-derived
/// relative to it. Rotation 0 is the major shape.
[[nodiscard]] inline Scale babylonianTuning (std::string name, int rotation, std::string story)
{
    // The chain -1..+5 fifths, i.e. F C G D A E B, folded and sorted gives
    // the C major set.
    std::vector<double> base;

    for (int fifths = -1; fifths <= 5; ++fifths)
        base.push_back (pythagoreanFifths (fifths));

    std::sort (base.begin(), base.end());

    Scale scale;

    scale.name = std::move (name);
    scale.repeat = 2.0;
    scale.ratios.clear();

    const auto count = static_cast<int> (base.size());
    const double tonic = base[static_cast<std::size_t> (rotation % count)];

    for (int degree = 0; degree < count; ++degree)
    {
        const int index = (rotation + degree) % count;
        scale.ratios.push_back (fold (base[static_cast<std::size_t> (index)] / tonic));
    }

    std::sort (scale.ratios.begin(), scale.ratios.end());
    scale.ratios[0] = 1.0;

    return described (std::move (scale),
        "Seven notes from a chain of six pure fifths -- the diatonic in its oldest "
        "documented form -- rotated to one of its seven modes. The tablets define each "
        "tuning by which string pair is left 'unclear': the tritone.",
        std::move (story));
}

/// The seven, in the cycle order of the tuning text (UET VII 74): each tuning
/// is one retuned string away from the next. The naming follows the rising
/// reading of the tablets, anchored by the two identifications the literature
/// agrees on most: nid qabli as the major-scale octave species, and embubum
/// as the palindromic mode. The falling reading renames the seven without
/// changing their sounds -- all seven rotations are here either way.

[[nodiscard]] inline Scale nidQabli()
{
    return babylonianTuning ("Nid qabli (Babylonian)", 0,
        "The tuning of the oldest written music on Earth: the colophons of the Hurrian "
        "hymns from Ugarit, around 1400 BC, name nid qabli as their tuning, and the usual "
        "reading makes it the major-scale octave species. The Old Babylonian tablets teach "
        "a cycle of retunings on the nine-stringed sammu lyre a thousand years before "
        "Pythagoras -- the chain of fifths enters history here. The name means 'casting "
        "down of the middle'.");
}

[[nodiscard]] inline Scale isartum()
{
    return babylonianTuning ("Isartum (Babylonian)", 2,
        "'The proper, the straight' -- the normal tuning of the Babylonian cycle, the one "
        "the scribes' lists begin with. Under the reading followed here it matches the "
        "octave species the Greeks called Dorian and modern ears call Phrygian-shaped: a "
        "semitone leaning hard on the tonic. Scholars still argue rising against falling "
        "readings of the tablets; the argument renames the seven without changing their "
        "sounds.");
}

[[nodiscard]] inline Scale embubum()
{
    return babylonianTuning ("Embubum (Babylonian)", 1,
        "'The reed pipe.' The palindrome of the seven -- its steps read the same rising "
        "and falling, the shape modern theory calls Dorian -- and therefore the one tuning "
        "whose identity survives every scholarly argument about which way the scales ran. "
        "One retuned string away from isartum in the tablet's cycle.");
}

[[nodiscard]] inline Scale kitmum()
{
    return babylonianTuning ("Kitmum (Babylonian)", 5,
        "'The covering.' The minor-shaped member of the cycle -- the natural minor in "
        "modern terms, reached from isartum by clearing one tritone. That a Babylonian "
        "lyre of 1800 BC and a modern bass line can share a scale, note for note, is the "
        "kind of fact this menu exists for.");
}

[[nodiscard]] inline Scale pitum()
{
    return babylonianTuning ("Pitum (Babylonian)", 4,
        "'The opening.' The dominant-shaped tuning -- Mixolydian to modern ears, the major "
        "scale with its seventh softened. Between embubum and nid qabli in the retuning "
        "cycle: tighten one string and the reed-pipe tuning opens into the hymn tuning.");
}

[[nodiscard]] inline Scale nisGabrim()
{
    return babylonianTuning ("Nis gabrim (Babylonian)", 3,
        "'The rise of the counterpart.' The brightest of the seven -- Lydian-shaped, with "
        "the tritone right against the tonic's own fourth -- and the far end of the cycle "
        "from qablitum. Also read as nis tuhrim; the Akkadian is argued about as much as "
        "the music.");
}

[[nodiscard]] inline Scale qablitum()
{
    return babylonianTuning ("Qablitum (Babylonian)", 6,
        "'The middle.' The darkest rotation -- diminished-fifth-shaped, the one modern "
        "theory shelved as Locrian and rarely uses. The Babylonians kept it in the cycle "
        "as a working tuning: seven strings, seven tunings, no favourites. On a growl "
        "bass its flattened fifth is not a defect but a colour.");
}

// ---------------------------------------------------------------------------
// Maqam theory -- two named constructions of Rast
// ---------------------------------------------------------------------------

/// Rast on al-Farabi's ratios: the rast tetrachord doubled at the fifth, with
/// Zalzal's neutral third 27/22.
[[nodiscard]] inline Scale rastZalzal()
{
    Scale scale;

    scale.name = "Rast (Zalzal, just)";
    scale.repeat = 2.0;
    scale.ratios = { 1.0,
                     9.0 / 8.0,      //  204 c
                     27.0 / 22.0,    //  355 c -- Zalzal's wosta
                     4.0 / 3.0,      //  498 c
                     3.0 / 2.0,      //  702 c
                     27.0 / 16.0,    //  906 c
                     81.0 / 44.0 };  // 1057 c -- the wosta over the fifth

    return described (std::move (scale),
        "The rast tetrachord 1 - 9/8 - 27/22 - 4/3, doubled at the fifth. The neutral "
        "third 27/22 (354.5 cents) is Zalzal's wosta, the lute fret al-Farabi wrote down.",
        "Rast -- 'straight' in Persian -- is the fundamental maqam, the do-scale of the "
        "Arab world. The 8th-century Baghdad lutenist Mansur Zalzal is credited with the "
        "fret that split the difference between the major and minor thirds; al-Farabi "
        "recorded its ratio a century later. The result beats against neither: played over "
        "a drone, the neutral third is a consonance in its own right, not a compromise "
        "between two Western ones.");
}

/// Rast in the Arel-Ezgi-Uzdilek system: degrees on the 53-comma grid.
[[nodiscard]] inline Scale rastArelEzgi()
{
    Scale scale;

    scale.name = "Rast (Turkish, AEU)";
    scale.repeat = 2.0;
    scale.ratios.clear();

    // Tone 9 commas, greater mucenneb 8, limma 5: T K S T T K S.
    const int commas[] = { 0, 9, 17, 22, 31, 40, 48 };

    for (const int comma : commas)
        scale.ratios.push_back (std::pow (2.0, comma / 53.0));

    return described (std::move (scale),
        "Degrees at 0, 9, 17, 22, 31, 40 and 48 of 53 Holdrian commas: tone 9, greater "
        "mucenneb 8, limma 5 -- the makam formula T K S T T K S.",
        "The Arel-Ezgi-Uzdilek system has been Turkey's official theory since the 1930s: "
        "all of makam music written on the 53-comma grid, because 53 fifths genuinely "
        "close the circle. Its Rast differs audibly from the Arab one -- the third at 17 "
        "commas is 385 cents, two cents from a pure 5/4, where Zalzal's floats thirty "
        "cents lower. Same name, different worlds, both in this menu on purpose.");
}

// ---------------------------------------------------------------------------
// Ancient China -- the twelve lu
// ---------------------------------------------------------------------------

/// The twelve lu by the san fen sun yi rule: eleven successive generations of
/// "subtract a third" (x 3/2 up) and "add a third" (x 4/3-style down),
/// which is a one-directional chain of pure fifths folded into the octave.
[[nodiscard]] inline Scale twelveLu()
{
    Scale scale;

    scale.name = "Twelve lu (China)";
    scale.repeat = 2.0;
    scale.ratios.clear();

    for (int fifths = 0; fifths <= 11; ++fifths)
        scale.ratios.push_back (pythagoreanFifths (fifths));

    std::sort (scale.ratios.begin(), scale.ratios.end());
    scale.ratios[0] = 1.0;

    return described (std::move (scale),
        "Eleven successive generations of the san fen sun yi rule -- remove a third of "
        "the pipe's length, then add a third -- which is a chain of eleven pure fifths "
        "run in one direction from the Yellow Bell and folded into its octave.",
        "China's twelve lu, documented in the Guanzi and the Lushi Chunqiu by the 3rd "
        "century BC: the same pure-fifth arithmetic as Pythagoras, run upward only, so "
        "its fourth is the sharp 521-cent kind rather than the pure 498. Each pitch held "
        "a cosmological office -- Huangzhong, the Yellow Bell, was a standard of state, "
        "recast when dynasties changed. Zhu Zaiyu's 1584 equal temperament was invented "
        "precisely to heal this chain's failure to close.");
}

// ---------------------------------------------------------------------------
// Partch -- the 43-tone Monophony
// ---------------------------------------------------------------------------

/// Harry Partch's 43-tone scale, as published in Genesis of a Music (2nd
/// edition, 1974).
///
/// Reproduced, not derived: the 43 degrees are Partch's artistic selection
/// from 11-limit just intonation and there is no rule that generates them.
/// What *can* be checked is checked in tests/test_Scales.cpp: 43 degrees,
/// strictly ascending, nothing beyond the 11-limit, and exact inversional
/// symmetry -- for every ratio r the scale also contains 2/r. Attribution and
/// access are recorded in docs/DSP-REFERENCES.md.
[[nodiscard]] inline Scale partch43()
{
    Scale scale;

    scale.name = "Partch 43";
    scale.repeat = 2.0;
    scale.ratios.clear();

    constexpr struct { int numerator, denominator; } degrees[] = {
        { 1, 1 },     { 81, 80 },   { 33, 32 },   { 21, 20 },   { 16, 15 },
        { 12, 11 },   { 11, 10 },   { 10, 9 },    { 9, 8 },     { 8, 7 },
        { 7, 6 },     { 32, 27 },   { 6, 5 },     { 11, 9 },    { 5, 4 },
        { 14, 11 },   { 9, 7 },     { 21, 16 },   { 4, 3 },     { 27, 20 },
        { 11, 8 },    { 7, 5 },     { 10, 7 },    { 16, 11 },   { 40, 27 },
        { 3, 2 },     { 32, 21 },   { 14, 9 },    { 11, 7 },    { 8, 5 },
        { 18, 11 },   { 5, 3 },     { 27, 16 },   { 12, 7 },    { 7, 4 },
        { 16, 9 },    { 9, 5 },     { 20, 11 },   { 11, 6 },    { 15, 8 },
        { 40, 21 },   { 64, 33 },   { 160, 81 },
    };

    for (const auto& degree : degrees)
        scale.ratios.push_back (static_cast<double> (degree.numerator)
                                  / static_cast<double> (degree.denominator));

    return described (std::move (scale),
        "Harry Partch's Monophony: 43 unequal degrees of 11-limit just intonation, "
        "symmetric about the centre -- for every ratio r, 2/r is also in the scale.",
        "Genesis of a Music (1949): the founding document of modern microtonality. Partch "
        "burned his early scores, rebuilt music on the harmonic series through the 11th "
        "partial, and built an orchestra by hand -- Chromelodeon, Cloud-Chamber Bowls, the "
        "Quadrangularis Reversum -- to play exactly these forty-three tones. The list is "
        "his artistic selection, reproduced with attribution; its symmetry, ordering and "
        "11-limit purity are verified by test.");
}

// ---------------------------------------------------------------------------
// The golden section
// ---------------------------------------------------------------------------

/// Seven equal divisions of phi -- this instrument's own construction, after
/// Heinz Bohlen's 833-cent studies.
[[nodiscard]] inline Scale goldenPhi()
{
    const double phi = 0.5 * (1.0 + std::sqrt (5.0));

    return described (equalDivisions ("Golden phi", 7, phi),
        "Seven equal divisions of the golden ratio phi = (1+sqrt(5))/2, an 833.09-cent "
        "repeat: steps of 119.0 cents, and no octave anywhere.",
        "Phi is the interval that reproduces itself: because 1/phi = phi - 1, the "
        "difference tone of two notes a phi apart lands exactly a phi below the lower "
        "note, so the scale's own combination tones stay inside it. Heinz Bohlen -- of "
        "Bohlen-Pierce -- built a scale on this repeat in the 1970s from Fibonacci "
        "combination tones; the equal seven-fold division here is this instrument's own "
        "construction on his interval, and says so.");
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
        // Pure
        twelveToneEqual(),
        justMajor(),
        justMinor(),
        pythagorean(),
        harmonicSeries(),
        harmonicSeriesHigh(),
        undertoneSeries(),
        justSevenLimit(),
        pythagoreanSeventeen(),
        partch43(),

        // Historical temperaments
        quarterCommaMeantone(),
        werckmeisterThree(),
        kirnbergerThree(),
        vallotti(),

        // Ancient Greece
        archytasEnharmonic(),
        archytasDiatonic(),
        archytasChromatic(),
        ptolemyEven(),

        // Old Babylonian -- the seven tunings of the tablets
        nidQabli(),
        isartum(),
        embubum(),
        kitmum(),
        pitum(),
        nisGabrim(),
        qablitum(),

        // Ancient China
        twelveLu(),

        // Persian dastgah theory
        shur(),
        chahargah(),

        // Maqam theory
        rastZalzal(),
        rastArelEzgi(),

        // Non-octave
        bohlenPierce(),
        carlosAlpha(),
        carlosBeta(),
        carlosGamma(),
        goldenPhi(),

        // Equal divisions
        fiveToneEqual(),
        sevenToneEqual(),
        seventeenToneEqual(),
        nineteenToneEqual(),
        twentyTwoToneEqual(),
        quarterTones(),
        fortyOneToneEqual(),
        thirtyOneToneEqual(),
        fiftyThreeToneEqual(),
    };
}

} // namespace tezla::dsp::scales
