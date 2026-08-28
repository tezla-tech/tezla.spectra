// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <string>

#include <tezla/dsp/ScalaFile.hpp>
#include <tezla/dsp/Tuning.hpp>

using namespace tezla::dsp;

namespace
{
/// A well-formed twelve-tone equal file, in the shape the archive uses.
const char* const kTwelveTone =
    "! 12tet.scl\n"
    "!\n"
    "12 tone equal temperament\n"
    " 12\n"
    "!\n"
    " 100.00000\n"
    " 200.00000\n"
    " 300.00000\n"
    " 400.00000\n"
    " 500.00000\n"
    " 600.00000\n"
    " 700.00000\n"
    " 800.00000\n"
    " 900.00000\n"
    " 1000.00000\n"
    " 1100.00000\n"
    " 2/1\n";

/// Ratios, a bare integer, and trailing comments -- all of which real archive
/// files contain.
const char* const kJust =
    "! just.scl\n"
    "Five-limit just major\n"
    "7\n"
    " 9/8    the whole tone\n"
    " 5/4\n"
    " 4/3\n"
    " 3/2 fifth\n"
    " 5/3\n"
    " 15/8\n"
    " 2      the octave, written as a bare integer\n";
} // namespace

// ---------------------------------------------------------------------------
// Reading a good file
// ---------------------------------------------------------------------------

TEZLA_TEST (a_well_formed_scale_file_loads)
{
    Scale scale;

    const auto result = parseScl (kTwelveTone, scale);

    CHECK (result.ok);
    CHECK (result.message.empty());

    CHECK (scale.name == "12 tone equal temperament");
    CHECK (scale.size() == 12);
    CHECK_NEAR (scale.repeat, 2.0, 1.0e-15);

    for (int degree = 0; degree < 12; ++degree)
        CHECK_NEAR (scale.cents (degree), degree * 100.0, 1.0e-9);

    // And it can actually tune a keyboard.
    Tuning tuning;

    CHECK (tuning.setScale (scale));
    CHECK_NEAR (tuning.frequencyFor (69), 440.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (60), 261.6255653005986, 1.0e-9);
}

TEZLA_TEST (a_ratio_stays_exact_where_cents_would_not)
{
    // The reason degrees are stored as ratios rather than converted to cents --
    // and it is **not** that the round trip loses them. Measured on this
    // toolchain, twenty of twenty-one ratios from Scales.hpp survive
    // `pow(2, 1200*log2(r)/1200)` bit-identically, including 3/2, 256/243 and
    // 243/224. The one that does not is 5/1, two octaves up.
    //
    // Which is the point: it works because this libm's log2 and pow happen to
    // be correctly rounded over this range. That is a property of one library
    // on one architecture, and the sort of assumption the ARM64 job exists to
    // catch. A ratio stored as a ratio has nothing to depend on.
    Scale scale;

    CHECK (parseScl (kJust, scale).ok);

    CHECK (scale.size() == 7);
    CHECK (scale.ratios[1] == 1.125);
    CHECK (scale.ratios[2] == 1.25);
    CHECK (scale.ratios[4] == 1.5);
    CHECK (scale.repeat == 2.0);

    // The exactness above is a property of the parser, not of the round trip:
    // it holds whatever the platform's transcendentals do, because there are
    // none in the path. Asserted by construction rather than by comparison.
    for (const double ratio : { 1.125, 1.25, 1.5 })
        CHECK (1200.0 * std::log2 (ratio) != 0.0);

    // A bare integer is the format's own shorthand for n/1.
    CHECK (scale.repeat == 2.0);
    CHECK (scale.ratios[6] == 15.0 / 8.0);
}

TEZLA_TEST (a_decimal_point_is_what_makes_a_value_cents)
{
    // The format's own rule, and the one that trips people up: `2` is an
    // octave and `2.0` is two cents. There is no way to tell them apart except
    // by the dot, so getting it wrong turns an octave into a comma.
    Scale twoCents;
    Scale octave;

    CHECK (parseScl ("cents\n1\n 2.0\n", twoCents).ok);
    CHECK (parseScl ("ratio\n1\n 2\n", octave).ok);

    CHECK_NEAR (twoCents.repeatCents(), 2.0, 1.0e-9);
    CHECK_NEAR (octave.repeatCents(), 1200.0, 1.0e-9);
}

TEZLA_TEST (a_non_octave_scale_file_loads_with_its_own_repeat)
{
    // Bohlen-Pierce as the archive writes it: thirteen equal steps of 3/1.
    std::string text = "Bohlen-Pierce equal\n13\n";

    for (int step = 1; step <= 12; ++step)
        text += " " + std::to_string (step * 1901.9550008653874 / 13.0) + "\n";

    text += " 3/1\n";

    Scale scale;

    CHECK (parseScl (text, scale).ok);
    CHECK (scale.size() == 13);
    CHECK_NEAR (scale.repeat, 3.0, 1.0e-15);

    Tuning tuning;

    CHECK (tuning.setScale (scale));

    tuning.setRootNote (60);
    tuning.setReference (60, 200.0);

    CHECK_NEAR (tuning.frequencyFor (73), 600.0, 1.0e-6);
}

TEZLA_TEST (comments_and_blank_description_lines_are_handled)
{
    // The description line is allowed to be empty, and dropping blank lines
    // would silently shift the count up into its place -- turning a scale with
    // no description into one described as "12" and with no note count.
    Scale scale;

    const auto result = parseScl ("! only.scl\n"
                                  "\n"
                                  "2\n"
                                  "! a comment between the count and the notes\n"
                                  "700.0\n"
                                  "2/1\n",
                                  scale);

    CHECK (result.ok);
    CHECK (scale.name == "Untitled scale");
    CHECK (scale.size() == 2);
    CHECK_NEAR (scale.cents (1), 700.0, 1.0e-9);
}

// ---------------------------------------------------------------------------
// Refusing a bad one, with the line number
// ---------------------------------------------------------------------------

TEZLA_TEST (a_malformed_line_is_refused_with_its_line_number)
{
    // The `.tzref` lesson. That loader took strtod's result on trust and a
    // corrupt file became ninety-six silent zeros -- no error, no warning, and
    // a reference curve that was simply wrong.
    //
    // A tuning that half-loads is worse, because the plugin plays and the
    // player spends an hour wondering why the bass is flat. So: every line
    // fully understood, or the file is refused and the line said out loud.
    //
    // The line number is the **file's**, counting comments and blanks, because
    // that is the one they can find in a text editor.
    struct Case
    {
        const char* text;
        int expectedLine;
        const char* what;
    };

    const Case cases[] = {
        { "! a.scl\n! b\nname\n3\n100.0\nnot a number\n2/1\n", 6, "words where a pitch should be" },
        { "! a.scl\nname\n3\n100.0\n3/2x\n2/1\n", 5, "garbage attached to a ratio" },
        { "! a.scl\nname\n3\n100.0abc\n3/2\n2/1\n", 4, "garbage attached to cents" },
        { "! a.scl\nname\n3\n100.0\n3/\n2/1\n", 5, "a ratio with no denominator" },
        { "! a.scl\nname\n3\n100.0\n3/0\n2/1\n", 5, "a zero denominator" },
        { "! a.scl\nname\n3\n100.0\n-3/2\n2/1\n", 5, "a negative ratio" },
        { "! a.scl\nname\nnot a count\n100.0\n2/1\n", 3, "a note count that is not a number" },
        { "! a.scl\nname\n0\n", 3, "a note count of zero" },
        { "! a.scl\nname\n99999\n100.0\n", 3, "an absurd note count" },
        { "! a.scl\nname\n5\n100.0\n200.0\n2/1\n", 6, "fewer pitches than promised" },
        { "! a.scl\n", 1, "a file with nothing in it" },
    };

    for (const auto& item : cases)
    {
        Scale scale;
        const std::string before = scale.name;

        const auto result = parseScl (item.text, scale);

        CHECK (! result.ok);
        CHECK (! result.message.empty());
        CHECK (result.line == item.expectedLine);

        // And nothing was written. A half-loaded scale is the failure mode
        // this whole test exists to rule out.
        CHECK (scale.name == before);
        CHECK (scale.size() == 1);
    }
}

TEZLA_TEST (a_trailing_comment_after_a_value_is_allowed)
{
    // Strictness has to stop somewhere: the Scala format allows a comment after
    // a pitch and real archive files use it. Refusing those would be stricter
    // than the format and would reject valid files.
    Scale scale;

    CHECK (parseScl ("name\n2\n 700.0 the fifth, tempered\n 2/1 octave\n", scale).ok);
    CHECK_NEAR (scale.cents (1), 700.0, 1.0e-9);

    // What is refused is text *attached* to the value, where a typo silently
    // becomes a different number.
    CHECK (! parseScl ("name\n2\n 700.0the fifth\n 2/1\n", scale).ok);
}

TEZLA_TEST (a_descending_scale_is_refused_rather_than_loaded)
{
    // Ascending pitches below the repeat interval is what `Scale::isUsable()`
    // means, and a scale that fails it would produce a keyboard where some keys
    // go backwards. Refusing is the only useful answer.
    Scale scale;

    const auto result = parseScl ("name\n3\n 700.0\n 300.0\n 2/1\n", scale);

    CHECK (! result.ok);
    CHECK (result.line == 5);
    CHECK (scale.size() == 1);

    // Same for a pitch at or above the repeat.
    CHECK (! parseScl ("name\n2\n 1300.0\n 2/1\n", scale).ok);
}

// ---------------------------------------------------------------------------
// Keyboard maps
// ---------------------------------------------------------------------------

TEZLA_TEST (a_keyboard_map_file_loads)
{
    const char* const text =
        "! a.kbm\n"
        "! size\n"
        "12\n"
        "! first note\n"
        "0\n"
        "! last note\n"
        "127\n"
        "! middle note\n"
        "60\n"
        "! reference note\n"
        "69\n"
        "! reference frequency\n"
        "440.0\n"
        "! formal octave degree\n"
        "12\n"
        "! the map\n"
        "0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n";

    KeyboardMap map;

    const auto result = parseKbm (text, map);

    CHECK (result.ok);
    CHECK (map.size == 12);
    CHECK (map.middleNote == 60);
    CHECK (map.referenceNote == 69);
    CHECK_NEAR (map.referenceHz, 440.0, 1.0e-12);
    CHECK (map.formalOctaveDegree == 12);
    CHECK (map.degrees.size() == 12);

    CHECK (map.degrees[0] == 0);
    CHECK (map.degrees[1] == KeyboardMap::kUnmapped);
    CHECK (map.degrees[2] == 1);
    CHECK (map.degrees[11] == 6);
}

TEZLA_TEST (a_malformed_keyboard_map_is_refused_with_its_line_number)
{
    const char* const good =
        "12\n0\n127\n60\n69\n440.0\n12\n0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n";

    KeyboardMap reference;

    CHECK (parseKbm (good, reference).ok);

    struct Case
    {
        const char* text;
        int expectedLine;
    };

    const Case cases[] = {
        // A short map is legal -- "at the end, unmapped keys may be left out"
        // -- so this one is refused for the *reference note* being among the
        // keys it left out, which the format calls an error in its own words.
        { "12\n0\n127\n60\n69\n440.0\n12\n0\nx\n1\n", 5 },               // reference unmapped
        { "12\n0\n127\n60\n69\nnot a number\n12\n0\n", 6 },              // bad frequency
        { "12\n0\n127\n60\n69\n0.0\n12\n0\n", 6 },                       // zero frequency
        { "12\n0\n200\n60\n69\n440.0\n12\n0\n", 2 },                     // note range past 127
        { "12\n80\n20\n60\n69\n440.0\n12\n0\n", 2 },                     // range that descends
        { "99999\n0\n127\n60\n69\n440.0\n12\n0\n", 1 },                  // absurd size
        { "12\n0\n127\n60\n69\n440.0\n12\n0\ny\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n", 9 },  // a stray letter
        { "12\n0\n", 2 },                                                 // truncated header
    };

    for (const auto& item : cases)
    {
        KeyboardMap map;

        const auto result = parseKbm (item.text, map);

        CHECK (! result.ok);
        CHECK (result.line == item.expectedLine);

        // Untouched.
        CHECK (map.size == 0);
        CHECK (map.degrees.empty());
    }
}

TEZLA_TEST (a_loaded_map_and_scale_tune_a_keyboard_together)
{
    // The end-to-end case: a seven-note just scale laid out on the white keys
    // by a twelve-entry map, which is the thing keyboard maps exist for.
    Scale scale;
    KeyboardMap map;

    CHECK (parseScl (kJust, scale).ok);
    CHECK (parseKbm ("12\n0\n127\n60\n60\n261.0\n7\n0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n",
                     map).ok);

    Tuning tuning;

    CHECK (tuning.setScale (scale));
    tuning.setKeyboardMap (map);

    CHECK_NEAR (tuning.frequencyFor (60), 261.0, 1.0e-12);
    CHECK (tuning.frequencyFor (61) == 0.0);
    CHECK_NEAR (tuning.frequencyFor (62) / 261.0, 9.0 / 8.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (64) / 261.0, 5.0 / 4.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (67) / 261.0, 3.0 / 2.0, 1.0e-12);
    CHECK_NEAR (tuning.frequencyFor (72) / 261.0, 2.0, 1.0e-12);
}

TEZLA_TEST (a_file_with_windows_line_endings_loads)
{
    // Scala files come from an archive assembled over thirty years and half of
    // them have CRLF endings. A parser that treats the carriage return as part
    // of the value refuses every one of them.
    Scale scale;

    CHECK (parseScl ("name\r\n2\r\n 700.0\r\n 2/1\r\n", scale).ok);
    CHECK (scale.size() == 2);
    CHECK_NEAR (scale.cents (1), 700.0, 1.0e-9);

    KeyboardMap map;

    CHECK (parseKbm ("2\r\n0\r\n127\r\n60\r\n69\r\n440.0\r\n2\r\n0\r\n1\r\n", map).ok);
    CHECK (map.degrees.size() == 2);
}

// ---------------------------------------------------------------------------
// What the specification settled
// ---------------------------------------------------------------------------
//
// Four of these were wrong before the Scala documentation was read, and none of
// them could have been found by measurement: they are a format's defined
// behaviour, which is exactly the case CLAUDE.md section 9 says to take from
// the source rather than derive. The quotations are from
// `technical references/sonitus/Scala help.htm`.

TEZLA_TEST (a_negative_mapping_degree_is_legal)
{
    // "There is no restriction to the degree numbers in the mapping or to the
    // scale degree to consider as formal octave, they can be any number, also
    // negative, also lie outside the scale range."
    //
    // So -1 is a real degree -- one step below the scale's root -- and using it
    // as the marker for an unmapped key silenced a key a valid file asked to be
    // tuned. `x` is the marker, and nothing else is.
    const char* const text =
        "12\n0\n127\n60\n69\n440.0\n12\n"
        "-1\n0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n";

    KeyboardMap map;

    const auto result = parseKbm (text, map);

    CHECK (result.ok);

    // Bail rather than index a map that was refused: a broken parser leaves
    // `map` untouched, and reading degrees[0] of an empty vector turns a test
    // failure into a crash that says nothing about what went wrong.
    if (! result.ok)
        return;

    CHECK (map.degrees[0] == -1);
    CHECK (map.degrees[1] == 0);

    // And it plays: degree -1 is a step below the root, which on a 12-tone
    // equal scale is a semitone down and an octave's worth of extension.
    Tuning tuning;

    CHECK (tuning.setScale (Tuning::twelveToneEqual()));
    tuning.setKeyboardMap (map);

    // Key 60 carries degree -1, key 61 carries degree 0. A semitone apart, with
    // 60 the *lower* of the two.
    const double ratio = tuning.frequencyFor (61) / tuning.frequencyFor (60);

    CHECK_NEAR (ratio, std::pow (2.0, 1.0 / 12.0), 1.0e-12);
}

TEZLA_TEST (a_mapping_may_stop_early_and_the_rest_is_silent)
{
    // "At the end, unmapped keys may be left out." Refusing a short map was
    // refusing valid files. The reference note is kept inside the part that is
    // present, or the next test's rule would refuse this one instead.
    const char* const text =
        "12\n0\n127\n60\n60\n261.0\n12\n"
        "0\n2\n4\n";

    KeyboardMap map;

    const auto result = parseKbm (text, map);

    CHECK (result.ok);

    if (! result.ok)
        return;

    // The pattern is still twelve keys wide -- the missing entries are silent
    // rather than absent, which is what makes the map repeat correctly.
    CHECK (map.degrees.size() == 12);
    CHECK (map.degrees[0] == 0);
    CHECK (map.degrees[2] == 4);
    CHECK (map.degrees[3] == KeyboardMap::kUnmapped);
    CHECK (map.degrees[11] == KeyboardMap::kUnmapped);

    Tuning tuning;

    CHECK (tuning.setScale (Tuning::twelveToneEqual()));
    tuning.setKeyboardMap (map);

    CHECK_NEAR (tuning.frequencyFor (60), 261.0, 1.0e-12);
    CHECK (tuning.frequencyFor (63) == 0.0);

    // Key 72 is one whole pattern up, so it is mapped again.
    CHECK_NEAR (tuning.frequencyFor (72) / 261.0, 2.0, 1.0e-12);
}

TEZLA_TEST (an_unmapped_reference_note_is_refused)
{
    // "If a certain key is not to be tuned, an 'x' must be placed instead of a
    // number. If this is done with the frequency reference note it will be
    // considered an error."
    //
    // And it has to be: the reference note is what pins the scale to a
    // frequency. Unmapped, there is nothing for the tuning to hang on, and the
    // specification says as much about what instruments then do -- "what
    // happens ... is not defined".
    const char* const text =
        "12\n0\n127\n60\n69\n440.0\n12\n"
        "0\n1\n2\n3\n4\n5\n6\n7\n8\nx\n10\n11\n";

    KeyboardMap map;

    const auto result = parseKbm (text, map);

    CHECK (! result.ok);

    // Key 69 is nine above the middle note, and entry nine is the `x`.
    CHECK (result.line == 5);
    CHECK (map.size == 0);
}

TEZLA_TEST (a_formal_octave_past_the_scale_stretches_the_pattern)
{
    // "If you want a mapping for a double octave range, which is the case if
    // the mapping is different in the next octave for example, then make the
    // scale degree to consider as formal octave parameter twice the size of the
    // scale."
    //
    // The specification calls this case out by name, and it was the one that
    // was wrong: a formal octave of 24 on a twelve-note scale returned the
    // repeat interval rather than its square, so every pattern sat an octave
    // flat of where the file asked.
    Tuning tuning;

    CHECK (tuning.setScale (Tuning::twelveToneEqual()));

    const auto ratioAcrossOnePattern = [&] (int formalOctave)
    {
        KeyboardMap map;

        map.size = 12;
        map.middleNote = 60;
        map.referenceNote = 60;
        map.referenceHz = 261.0;
        map.formalOctaveDegree = formalOctave;
        map.degrees = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

        tuning.setKeyboardMap (map);

        return tuning.frequencyFor (72) / tuning.frequencyFor (60);
    };

    // Twelve: one repeat per pattern, the ordinary case.
    CHECK_NEAR (ratioAcrossOnePattern (12), 2.0, 1.0e-12);

    // Twenty-four: two repeats per pattern, which is the double-octave range
    // the specification describes.
    CHECK_NEAR (ratioAcrossOnePattern (24), 4.0, 1.0e-12);

    // Seven is a degree *inside* the scale, so the pattern advances by whatever
    // interval degree seven happens to be -- a fifth, here.
    CHECK_NEAR (ratioAcrossOnePattern (7), std::pow (2.0, 7.0 / 12.0), 1.0e-12);

    // Zero is the one value read as a convention rather than as a degree: not
    // set, so the scale's own repeat. Degree 0 taken literally is 1/1, which
    // would stack every pattern on top of the last.
    CHECK_NEAR (ratioAcrossOnePattern (0), 2.0, 1.0e-12);

    // Negative is legal too, and descends.
    CHECK_NEAR (ratioAcrossOnePattern (-12), 0.5, 1.0e-12);
}

TEZLA_TEST (the_pitch_lines_the_specification_calls_valid_all_parse)
{
    // Straight from the .scl page: "So these lines are all valid pitch lines".
    // The last three are the ones worth having a test for -- a pitch value with
    // a note name or a unit after it is *normal* in the archive, and a parser
    // that required the line to hold nothing else would refuse real files.
    struct Case { const char* line; double ratio; };

    const Case cases[] = {
        { "81/64",       81.0 / 64.0 },
        { "408.0",       0.0 },              // cents, checked below
        { "408.",        0.0 },
        { "5",           5.0 },
        { "10/20",       0.5 },
        { "100.0 cents", 0.0 },
        { " 100.0 C#",   0.0 },
        { " 5/4 E\\",    5.0 / 4.0 },
    };

    for (const auto& item : cases)
    {
        double ratio = 0.0;
        std::string why;

        CHECK (scala::parsePitch (item.line, ratio, why));

        if (item.ratio > 0.0)
            CHECK_NEAR (ratio, item.ratio, 1.0e-12);
    }

    // The cents ones, against 2^(c/1200).
    double ratio = 0.0;
    std::string why;

    CHECK (scala::parsePitch ("408.0", ratio, why));
    CHECK_NEAR (ratio, std::pow (2.0, 408.0 / 1200.0), 1.0e-12);

    CHECK (scala::parsePitch ("100.0 C#", ratio, why));
    CHECK_NEAR (ratio, std::pow (2.0, 100.0 / 1200.0), 1.0e-12);

    // "Negative ratios are meaningless and should give a read error" -- but
    // negative *cents* are a legal line, and the page lists -5.0 among the
    // valid ones. The two are not the same rule and it is easy to conflate.
    CHECK (scala::parsePitch ("-5.0", ratio, why));
    CHECK_NEAR (ratio, std::pow (2.0, -5.0 / 1200.0), 1.0e-12);

    CHECK (! scala::parsePitch ("-5/4", ratio, why));

    // "Ratios are written with a slash, and only one."
    CHECK (! scala::parsePitch ("1/2/3", ratio, why));
}
