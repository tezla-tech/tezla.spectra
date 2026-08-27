#pragma once

// The Scala `.scl` and `.kbm` formats, parsed from text.
//
// ---------------------------------------------------------------------------
// The parser is the risk, and there is a precedent in this repository
// ---------------------------------------------------------------------------
//
// Transpectus's `.tzref` loader took `strtod`'s result on trust. A corrupt file
// loaded as ninety-six silent zeros: no error, no warning, and a reference
// curve that was simply wrong. Requiring the whole line to be consumed fixed
// it, and the lesson generalises -- **a tuning that silently half-loads is
// worse than one that will not load**, because the plugin plays and the player
// spends an hour wondering why the bass is flat.
//
// So every line is either fully understood or the file is refused, with the
// line number said out loud. The line number is the *file's*, counting comments
// and blanks, because that is the one the player can find in a text editor.
//
// The one place strictness has to stop is trailing text. Scala's own format
// allows a comment after a pitch value and real archive files use it, so
// `3/2 the fifth` is valid. What is refused is garbage *attached* to the value
// -- `3/2x`, `100.0abc` -- which is the case where a typo silently becomes a
// different number.
//
// ---------------------------------------------------------------------------
// The format
// ---------------------------------------------------------------------------
//
// `.scl`, after comment lines beginning with `!` are removed:
//
//     line 1     free-text description, may be empty
//     line 2     the number of notes, N
//     lines 3..  N pitch values
//
// A pitch is **cents if it contains a decimal point** and a ratio otherwise --
// that is the format's own rule and it is why `2` and `2.0` mean completely
// different things: 2/1, an octave, against two cents. The last value is the
// interval the scale repeats at and is under no obligation to be an octave.
//
// `.kbm` is seven numbers and then `size` mapping entries, where `x` marks an
// unmapped key.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "Tuning.hpp"

namespace tezla::dsp {

/// What came back from a parse. `ok` false means nothing was loaded.
struct ScalaResult
{
    bool ok { false };
    std::string message;
    int line { 0 };

    [[nodiscard]] static ScalaResult success() { return { true, {}, 0 }; }

    [[nodiscard]] static ScalaResult failure (std::string why, int atLine)
    {
        return { false, std::move (why), atLine };
    }
};

namespace scala {

/// A line of the file, with the number it had before comments were stripped.
struct Line
{
    std::string text;
    int number { 0 };
};

[[nodiscard]] inline std::string_view trim (std::string_view text) noexcept
{
    const auto isSpace = [] (char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    };

    while (! text.empty() && isSpace (text.front()))
        text.remove_prefix (1);

    while (! text.empty() && isSpace (text.back()))
        text.remove_suffix (1);

    return text;
}

/// Splits into lines and drops the comments, keeping each survivor's original
/// line number so an error can name it.
///
/// **Blank lines are kept**, because the description line is allowed to be
/// empty and dropping blanks would silently shift the count line up into its
/// place -- turning a scale with no description into one with a description of
/// "12" and no note count.
[[nodiscard]] inline std::vector<Line> significantLines (std::string_view text)
{
    std::vector<Line> lines;

    int number = 0;
    std::size_t start = 0;

    while (start <= text.size())
    {
        const std::size_t end = std::min (text.find ('\n', start), text.size());

        ++number;

        const std::string_view raw = text.substr (start, end - start);
        const std::string_view body = trim (raw);

        if (body.empty() || body.front() != '!')
            lines.push_back ({ std::string (body), number });

        if (end >= text.size())
            break;

        start = end + 1;
    }

    // A file ending in a newline produces one trailing empty line that is not
    // a line of the file. Dropping it here keeps "not enough pitches" honest.
    if (! lines.empty() && lines.back().text.empty())
        lines.pop_back();

    return lines;
}

/// Parses one pitch line into a ratio.
///
/// Returns false and leaves `ratio` untouched when the line is not fully
/// understood.
[[nodiscard]] inline bool parsePitch (const std::string& text, double& ratio, std::string& why)
{
    const std::string_view body = trim (text);

    if (body.empty())
    {
        why = "empty pitch line";
        return false;
    }

    // The format's own rule: a decimal point makes it cents.
    const bool isCents = body.find ('.') != std::string_view::npos;

    const std::string owned (body);
    const char* begin = owned.c_str();
    char* after = nullptr;

    if (isCents)
    {
        const double cents = std::strtod (begin, &after);

        if (after == begin)
        {
            why = "not a number";
            return false;
        }

        if (*after != '\0' && ! std::isspace (static_cast<unsigned char> (*after)))
        {
            why = "unexpected text attached to the value";
            return false;
        }

        if (! std::isfinite (cents))
        {
            why = "value is not finite";
            return false;
        }

        ratio = std::pow (2.0, cents / 1200.0);
    }
    else
    {
        const long numerator = std::strtol (begin, &after, 10);

        if (after == begin)
        {
            why = "not a number";
            return false;
        }

        long denominator = 1;

        if (*after == '/')
        {
            const char* divisor = after + 1;

            denominator = std::strtol (divisor, &after, 10);

            if (after == divisor)
            {
                why = "ratio has no denominator";
                return false;
            }
        }

        if (*after != '\0' && ! std::isspace (static_cast<unsigned char> (*after)))
        {
            why = "unexpected text attached to the value";
            return false;
        }

        if (numerator <= 0 || denominator <= 0)
        {
            why = "a ratio must be positive";
            return false;
        }

        ratio = static_cast<double> (numerator) / static_cast<double> (denominator);
    }

    if (! (ratio > 0.0) || ! std::isfinite (ratio))
    {
        why = "value is not a usable pitch";
        return false;
    }

    return true;
}

/// Parses a line that must be a whole number and nothing else.
[[nodiscard]] inline bool parseInteger (const std::string& text, long& value, std::string& why)
{
    const std::string owned (trim (text));

    if (owned.empty())
    {
        why = "expected a number";
        return false;
    }

    const char* begin = owned.c_str();
    char* after = nullptr;

    value = std::strtol (begin, &after, 10);

    if (after == begin)
    {
        why = "not a number";
        return false;
    }

    if (*after != '\0' && ! std::isspace (static_cast<unsigned char> (*after)))
    {
        why = "unexpected text attached to the value";
        return false;
    }

    return true;
}

[[nodiscard]] inline bool parseDouble (const std::string& text, double& value, std::string& why)
{
    const std::string owned (trim (text));

    if (owned.empty())
    {
        why = "expected a number";
        return false;
    }

    const char* begin = owned.c_str();
    char* after = nullptr;

    value = std::strtod (begin, &after);

    if (after == begin)
    {
        why = "not a number";
        return false;
    }

    if (*after != '\0' && ! std::isspace (static_cast<unsigned char> (*after)))
    {
        why = "unexpected text attached to the value";
        return false;
    }

    if (! std::isfinite (value))
    {
        why = "value is not finite";
        return false;
    }

    return true;
}

} // namespace scala

/// The largest scale that will be accepted.
///
/// The Scala archive's biggest are a few hundred notes. A file claiming more is
/// either corrupt or hostile, and a note count is the one field that decides
/// how much this allocates.
inline constexpr long kMaximumScaleSize = 1024;

/// Parses a `.scl` file's text into a `Scale`.
///
/// `scale` is left untouched unless the whole file parsed.
[[nodiscard]] inline ScalaResult parseScl (std::string_view text, Scale& scale)
{
    const auto lines = scala::significantLines (text);

    if (lines.size() < 2)
        return ScalaResult::failure ("a scale file needs a description and a note count",
                                     lines.empty() ? 1 : lines.back().number);

    const std::string description = lines[0].text;

    long count = 0;
    std::string why;

    if (! scala::parseInteger (lines[1].text, count, why))
        return ScalaResult::failure ("note count: " + why, lines[1].number);

    if (count < 1 || count > kMaximumScaleSize)
        return ScalaResult::failure ("note count out of range (1 to "
                                       + std::to_string (kMaximumScaleSize) + ")",
                                     lines[1].number);

    if (static_cast<long> (lines.size()) < count + 2)
        return ScalaResult::failure ("the file claims " + std::to_string (count)
                                       + " notes but only " + std::to_string (lines.size() - 2)
                                       + " are present",
                                     lines.back().number);

    Scale built;

    built.name = description.empty() ? "Untitled scale" : description;
    built.ratios.clear();
    built.ratios.push_back (1.0);

    for (long index = 0; index < count; ++index)
    {
        const auto& line = lines[static_cast<std::size_t> (index + 2)];

        double ratio = 1.0;

        if (! scala::parsePitch (line.text, ratio, why))
            return ScalaResult::failure ("note " + std::to_string (index + 1) + ": " + why,
                                         line.number);

        // The last entry is the interval the scale repeats at, not a degree.
        if (index == count - 1)
            built.repeat = ratio;
        else
            built.ratios.push_back (ratio);
    }

    if (! built.isUsable())
        return ScalaResult::failure ("the scale is not usable -- pitches must ascend and stay "
                                     "below the repeat interval",
                                     lines[static_cast<std::size_t> (count + 1)].number);

    scale = std::move (built);

    return ScalaResult::success();
}

/// Parses a `.kbm` file's text into a `KeyboardMap`.
[[nodiscard]] inline ScalaResult parseKbm (std::string_view text, KeyboardMap& map)
{
    const auto lines = scala::significantLines (text);

    if (lines.size() < 7)
        return ScalaResult::failure ("a keyboard map needs seven values before its entries",
                                     lines.empty() ? 1 : lines.back().number);

    std::string why;

    const auto readInteger = [&] (std::size_t index, long& value, const char* what) -> ScalaResult
    {
        if (! scala::parseInteger (lines[index].text, value, why))
            return ScalaResult::failure (std::string (what) + ": " + why, lines[index].number);

        return ScalaResult::success();
    };

    long size = 0;
    long first = 0;
    long last = 127;
    long middle = 60;
    long reference = 69;
    long formalOctave = 0;
    double referenceHz = 440.0;

    if (auto result = readInteger (0, size, "map size"); ! result.ok) return result;
    if (auto result = readInteger (1, first, "first note"); ! result.ok) return result;
    if (auto result = readInteger (2, last, "last note"); ! result.ok) return result;
    if (auto result = readInteger (3, middle, "middle note"); ! result.ok) return result;
    if (auto result = readInteger (4, reference, "reference note"); ! result.ok) return result;

    if (! scala::parseDouble (lines[5].text, referenceHz, why))
        return ScalaResult::failure ("reference frequency: " + why, lines[5].number);

    if (auto result = readInteger (6, formalOctave, "formal octave degree"); ! result.ok)
        return result;

    if (size < 0 || size > kMaximumScaleSize)
        return ScalaResult::failure ("map size out of range", lines[0].number);

    if (referenceHz <= 0.0)
        return ScalaResult::failure ("reference frequency must be above zero", lines[5].number);

    if (first < 0 || last > 127 || first > last)
        return ScalaResult::failure ("the note range must lie within 0 to 127 and ascend",
                                     lines[1].number);

    KeyboardMap built;

    built.size = static_cast<int> (size);
    built.firstNote = static_cast<int> (first);
    built.lastNote = static_cast<int> (last);
    built.middleNote = static_cast<int> (middle);
    built.referenceNote = static_cast<int> (reference);
    built.referenceHz = referenceHz;
    built.formalOctaveDegree = static_cast<int> (formalOctave);

    if (static_cast<long> (lines.size()) < size + 7)
        return ScalaResult::failure ("the map claims " + std::to_string (size)
                                       + " entries but only " + std::to_string (lines.size() - 7)
                                       + " are present",
                                     lines.back().number);

    built.degrees.reserve (static_cast<std::size_t> (size));

    for (long index = 0; index < size; ++index)
    {
        const auto& line = lines[static_cast<std::size_t> (index + 7)];
        const std::string_view body = scala::trim (line.text);

        // 'x' is the format's marker for a key that plays nothing.
        if (body == "x" || body == "X")
        {
            built.degrees.push_back (-1);
            continue;
        }

        long degree = 0;

        if (! scala::parseInteger (line.text, degree, why))
            return ScalaResult::failure ("entry " + std::to_string (index + 1) + ": " + why
                                           + " (use 'x' for an unmapped key)",
                                         line.number);

        if (degree < 0)
            return ScalaResult::failure ("entry " + std::to_string (index + 1)
                                           + ": a degree cannot be negative -- use 'x' for an "
                                             "unmapped key",
                                         line.number);

        built.degrees.push_back (static_cast<int> (degree));
    }

    map = std::move (built);

    return ScalaResult::success();
}

} // namespace tezla::dsp
