// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Every tone a telephone network makes, as data.
//
// ---------------------------------------------------------------------------
// Why this file is copied rather than derived
// ---------------------------------------------------------------------------
//
// CLAUDE.md section 9 says derive by default and copy only what measurement
// cannot check. This file is the clearest case of the exception in the whole
// repository: **no measurement can tell you that 941 Hz should have been 940.**
// A telephone tone *is* its specification -- a DTMF pair a real decoder would
// reject is not a slightly different sound, it is the wrong instrument. So
// every frequency, level and cadence below is taken from its published
// standard, named at the point of use, recorded again in
// docs/DSP-REFERENCES.md, and pinned by a test.
//
// The honest caveat, per section 9: **none of the primary documents could be
// fetched from this container.** itu.int, the Bell practice archives and BT's
// specification are all refused by the egress proxy. The figures reached this
// file through search results and secondary technical sources that agree with
// one another -- ETSI ES 201 235-2, vendor application notes, telephony
// references. That is a weaker claim than "the standard says", and
// DSP-REFERENCES.md states it in those words and names what to re-check first
// if the primary sources ever become reachable.
//
// ---------------------------------------------------------------------------
// A tone is a program
// ---------------------------------------------------------------------------
//
// Not a waveform, and not a special case per tone. Every telephone signal in
// the book is a short list of *steps*, each holding up to four simultaneous
// frequencies for a stated time, looping or not:
//
//   a DTMF digit          one step, forever, two frequencies
//   a busy tone           two steps of half a second, looping
//   the UK congestion     four steps, one of them 6 dB louder than the rest
//   the intercept SIT     three ascending steps and then silence
//   a rotary dial         two steps with a click at each edge, looping
//
// One mechanism, every tone. And the durations are seconds rather than
// samples, so the cadences are identical at 44.1, 48, 96 and 192 kHz by
// construction rather than by care (CLAUDE.md section 6).
//
// ---------------------------------------------------------------------------
// Levels
// ---------------------------------------------------------------------------
//
// The Precise Tone Plan specifies its four North American tones at -13, -19
// and -24 dBm, so a busy tone really is quieter than a dial tone and always
// was. Those *differences* are audible and are honoured exactly; the absolute
// dBm reference is not, because a plugin's ceiling is full scale rather than a
// line. So 0 dB here means "as loud as a North American dial tone", and the
// relative levels fall out of the standard:
//
//   dial 0 dB   ringback -6 dB   busy and reorder -11 dB
//
// The one figure deliberately not honoured is the howler's. The receiver
// off-hook tone is specified at 0 dBm0 *per frequency*, four frequencies at
// once, roughly +13 dB over the dial tone -- it is meant to be heard across a
// room. Reproduced literally it would clip every time. Its four frequencies
// and its 0.1 s cadence are exact; its level is capped at the dial tone's, and
// this comment is the record of that being a choice rather than an error.

#include <cmath>
#include <cstddef>

#include <tezla/dsp/Decibels.hpp>

namespace tezla::crossbar {

namespace dsp = tezla::dsp;

/// The most simultaneous frequencies any telephone tone uses. Four, and it is
/// the howler that needs them all.
inline constexpr int kMaxPartials = 4;

/// The most steps any cadence needs. Four, and it is the UK's ringing and
/// congestion tones that need them all.
inline constexpr int kMaxSteps = 4;

/// A step that lasts until the key is released.
inline constexpr double kForever = -1.0;

/// One segment of a tone's cadence.
struct ToneStep
{
    double seconds { kForever };
    double frequency[kMaxPartials] {};
    double gain[kMaxPartials] {};
    int    partials { 0 };

    /// A rotary dial's loop-break transient rather than a tone: the generator
    /// fires one click at this step's start. `partials` is 0 on these.
    bool click { false };
};

/// A complete telephone signal.
struct ToneProgram
{
    ToneStep steps[kMaxSteps] {};
    int  stepCount { 0 };

    /// Whether the cadence repeats. The SIT does not; a busy tone does.
    bool loops { false };
};

// ---------------------------------------------------------------------------
// DTMF -- ITU-T Q.23
// ---------------------------------------------------------------------------
//
// Four low-group and four high-group frequencies; a key is one of each. The
// fourth column (1633 Hz) carries A/B/C/D, which never appeared on a domestic
// telephone but is in the standard and was used for military precedence
// signalling on AUTOVON.
//
// These are international. Unlike everything below them, they do not vary by
// region, and a test asserts that.

inline constexpr double kDtmfRowHz[4] { 697.0, 770.0, 852.0, 941.0 };
inline constexpr double kDtmfColHz[4] { 1209.0, 1336.0, 1477.0, 1633.0 };

/// Q.23's frequency tolerance -- 1.8% generally, 1.5% by European practice.
/// Not used to generate anything; it is what the accuracy test judges against,
/// and the margin our own numbers beat by is the point of measuring at all.
inline constexpr double kDtmfToleranceFraction = 0.015;

/// The keypad, in reading order. Row 0 is `1 2 3 A`.
inline constexpr char kKeypad[4][4] {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' },
};

/// Which region's call-progress set to use.
///
/// **APPEND-ONLY forever** (CLAUDE.md section 8) -- this is a choice
/// parameter, and a choice parameter stores an index. Inserting a country
/// between these two would silently repoint every saved project.
enum class Region
{
    northAmerica = 0,
    unitedKingdom
};

/// Everything a key can play.
///
/// The order **is** the keyboard layout, and it is frozen: a saved MIDI clip
/// refers to note numbers, so moving an entry here moves what an existing
/// pattern plays. New tones go on the end.
enum class Tone
{
    // The keypad -- one octave from the map root is the phone.
    digit1 = 0, digit2, digit3,
    digit4, digit5, digit6,
    digit7, digit8, digit9,
    star, digit0, hash,

    // The fourth DTMF column.
    dtmfA, dtmfB, dtmfC, dtmfD,

    // Call progress.
    dialTone, busy, ringback, congestion, unobtainable, howler, callWaiting,
    sit, faxCalling, modemAnswer, singleFrequency, rotaryPulse,

    // The eight constituent frequencies alone, for sound design.
    row697, row770, row852, row941,
    col1209, col1336, col1477, col1633,

    /// Not a program: the engine plays the stored dial string when this
    /// arrives. It is last so the programs above stay contiguous.
    dialNumber,

    count
};

inline constexpr int kToneCount = static_cast<int> (Tone::count);

/// The default MIDI note the map starts at: C1, where a drum map starts.
inline constexpr int kDefaultMapRoot = 36;

/// Whether a tone is one of the sixteen DTMF keys.
[[nodiscard]] constexpr bool isDtmf (Tone tone) noexcept
{
    return tone >= Tone::digit1 && tone <= Tone::dtmfD;
}

/// The row and column indices of a DTMF key, in keypad reading order.
///
/// The enum runs `1 2 3 4 5 6 7 8 9 * 0 #` and then `A B C D`, because that is
/// the useful order to put on a keyboard -- an octave of white and black keys
/// is the whole domestic keypad. The *matrix* runs `1 2 3 A / 4 5 6 B / ...`.
/// This is the one place the two orders meet.
constexpr bool dtmfIndices (Tone tone, int& row, int& column) noexcept
{
    const int index = static_cast<int> (tone);

    if (tone >= Tone::digit1 && tone <= Tone::digit9)     // 1-9: three per row
    {
        row = index / 3;
        column = index % 3;
        return true;
    }

    if (tone >= Tone::star && tone <= Tone::hash)         // * 0 # : the bottom row
    {
        row = 3;
        column = index - static_cast<int> (Tone::star);
        return true;
    }

    if (tone >= Tone::dtmfA && tone <= Tone::dtmfD)       // the fourth column
    {
        row = index - static_cast<int> (Tone::dtmfA);
        column = 3;
        return true;
    }

    return false;
}

/// The inverse of `dtmfIndices`: which key sits at a place in the matrix.
///
/// The dialler needs it -- it reads characters, gets matrix coordinates, and
/// has to come back to a tone -- and having both directions in one file is
/// what keeps them from disagreeing.
constexpr Tone toneForDtmf (int row, int column) noexcept
{
    if (row < 0 || row > 3 || column < 0 || column > 3)
        return Tone::count;

    if (column == 3)
        return static_cast<Tone> (static_cast<int> (Tone::dtmfA) + row);

    if (row == 3)
        return static_cast<Tone> (static_cast<int> (Tone::star) + column);

    return static_cast<Tone> (row * 3 + column);
}

/// The DTMF key a character stands for, for the dialler. Returns false for
/// anything that is not a key -- spaces, dashes and brackets in a written
/// phone number, which the dialler skips.
constexpr bool dtmfForCharacter (char c, int& row, int& column) noexcept
{
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k)
            if (kKeypad[r][k] == c)
            {
                row = r;
                column = k;
                return true;
            }

    return false;
}

/// How many loop breaks a rotary dial makes for a character. Ten for '0',
/// which is why a zero takes a second to dial and is the reason emergency
/// numbers were chosen to be short. Returns 0 for anything not a digit.
constexpr int pulsesForCharacter (char c) noexcept
{
    if (c == '0')
        return 10;

    return (c >= '1' && c <= '9') ? c - '0' : 0;
}

// ---------------------------------------------------------------------------
// Building programs
// ---------------------------------------------------------------------------

namespace detail {

/// A step holding one to four frequencies at a single level.
///
/// `levelDb` is the level of the *step*, and the partials share it equally, so
/// a two-tone step and a four-tone step at the same level peak at the same
/// place instead of the four-tone one being twice as loud.
inline ToneStep tones (double seconds, double levelDb,
                       double f0, double f1 = 0.0, double f2 = 0.0, double f3 = 0.0) noexcept
{
    ToneStep step;
    step.seconds = seconds;

    const double frequencies[kMaxPartials] { f0, f1, f2, f3 };

    for (double f : frequencies)
        if (f > 0.0)
            step.frequency[step.partials++] = f;

    if (step.partials == 0)
        return step;   // a step with no frequency is silence, not a division by zero

    const double share = dsp::dbToGain (levelDb) / static_cast<double> (step.partials);

    for (int i = 0; i < step.partials; ++i)
        step.gain[i] = share;

    return step;
}

inline ToneStep silence (double seconds) noexcept
{
    ToneStep step;
    step.seconds = seconds;
    return step;
}

inline ToneStep clickStep (double seconds) noexcept
{
    ToneStep step;
    step.seconds = seconds;
    step.click = true;
    return step;
}

inline ToneProgram program (bool loops, ToneStep a) noexcept
{
    ToneProgram p;
    p.loops = loops;
    p.steps[p.stepCount++] = a;
    return p;
}

inline ToneProgram program (bool loops, ToneStep a, ToneStep b) noexcept
{
    ToneProgram p = program (loops, a);
    p.steps[p.stepCount++] = b;
    return p;
}

inline ToneProgram program (bool loops, ToneStep a, ToneStep b, ToneStep c) noexcept
{
    ToneProgram p = program (loops, a, b);
    p.steps[p.stepCount++] = c;
    return p;
}

inline ToneProgram program (bool loops, ToneStep a, ToneStep b, ToneStep c, ToneStep d) noexcept
{
    ToneProgram p = program (loops, a, b, c);
    p.steps[p.stepCount++] = d;
    return p;
}

/// The Bell Precise Tone Plan's relative levels, as differences from the dial
/// tone: -13 dBm dial, -19 dBm ringback, -24 dBm busy and reorder.
inline constexpr double kDialDb     = 0.0;
inline constexpr double kRingbackDb = -6.0;
inline constexpr double kBusyDb     = -11.0;

/// The UK set sits 6 dB down so that BT's congestion tone -- whose second
/// burst is specified 6 dB above the first -- has somewhere to go without
/// clipping. The *step* is what BT specified and the step is exact.
inline constexpr double kUkDb     = -6.0;
inline constexpr double kUkLoudDb = 0.0;

/// The intercept SIT, whose four variants differ in which of two frequencies
/// each of the first two segments uses and how long they last. `longVariant`
/// picks the high pair and the 380 ms segments.
inline ToneProgram sitProgram (bool longVariant) noexcept
{
    const double first  = longVariant ? 985.2 : 913.8;
    const double second = longVariant ? 1428.5 : 1370.6;
    const double span   = longVariant ? 0.380 : 0.274;

    // Three ascending tones and then nothing: on a real line the recorded
    // announcement follows, and holding the key gives you the silence where it
    // would have been.
    return program (false,
                    tones (span,  kRingbackDb, first),
                    tones (span,  kRingbackDb, second),
                    tones (0.380, kRingbackDb, 1776.7));
}

/// The receiver off-hook howler. Four frequencies at 0.1 s on, 0.1 s off --
/// see the header for why its level is the one figure not honoured literally.
inline ToneProgram howlerProgram() noexcept
{
    return program (true,
                    tones (0.1, kDialDb, 1400.0, 2060.0, 2450.0, 2600.0),
                    silence (0.1));
}

/// Ten pulses a second, 60% break and 40% make -- a rotary dial's loop
/// interruptions. What reaches the earpiece is the transient at each edge
/// rather than a tone, which is why these steps carry a click and no
/// frequency at all.
inline ToneProgram rotaryProgram() noexcept
{
    return program (true, clickStep (0.060), clickStep (0.040));
}

} // namespace detail

/// The program a tone plays in a region.
///
/// `twistDb` is Q.24's twist: the level of the high group relative to the low
/// group, which real transmitters set to about +2 dB to survive line loss.
///
/// The two gains are **normalised so they always sum to exactly 1.0**, which
/// is what keeps twist a tone control rather than a level control -- and, more
/// usefully, what stops a DTMF key clipping. The partials of a step start in
/// phase, so the sum of the gains *is* the peak: an unnormalised +2 dB twist
/// would peak at 1.0067, which is a plugin that clips on its own default
/// setting. What the standard defines is the *difference* between the two, and
/// the difference survives the normalisation exactly.
///
/// Twist affects DTMF pairs only; everything else ignores it.
[[nodiscard]] inline ToneProgram programFor (Tone tone, Region region, double twistDb = 2.0)
{
    using namespace detail;

    const bool uk = region == Region::unitedKingdom;

    if (isDtmf (tone))
    {
        int row = 0, column = 0;
        dtmfIndices (tone, row, column);

        ToneStep step;
        step.seconds = kForever;
        step.partials = 2;
        step.frequency[0] = kDtmfRowHz[row];
        step.frequency[1] = kDtmfColHz[column];

        const double lowRelative  = dsp::dbToGain (-0.5 * twistDb);
        const double highRelative = dsp::dbToGain (0.5 * twistDb);
        const double normalise    = 1.0 / (lowRelative + highRelative);

        step.gain[0] = lowRelative * normalise;
        step.gain[1] = highRelative * normalise;

        return program (false, step);
    }

    switch (tone)
    {
        // -- North America: the Bell Precise Tone Plan -----------------------
        // -- United Kingdom: the BT set --------------------------------------

        case Tone::dialTone:
            return uk ? program (false, tones (kForever, kUkDb, 350.0, 450.0))
                      : program (false, tones (kForever, kDialDb, 350.0, 440.0));

        case Tone::busy:
            return uk ? program (true,
                                 tones (0.375, kUkDb, 400.0),
                                 silence (0.375))
                      : program (true,
                                 tones (0.5, kBusyDb, 480.0, 620.0),
                                 silence (0.5));

        case Tone::ringback:
            // The UK's double ring: two 400 ms bursts 200 ms apart, then two
            // seconds of nothing. The 50 Hz beat between 400 and 450 Hz is the
            // "burr" -- it is two tones summed, not one modulated.
            return uk ? program (true,
                                 tones (0.4, kUkDb, 400.0, 450.0),
                                 silence (0.2),
                                 tones (0.4, kUkDb, 400.0, 450.0),
                                 silence (2.0))
                      : program (true,
                                 tones (2.0, kRingbackDb, 440.0, 480.0),
                                 silence (4.0));

        case Tone::congestion:
            // Reorder, or "fast busy": the same pair as busy at twice the
            // rate. BT's equivalent is asymmetric and steps 6 dB louder on its
            // second burst, which is the whole character of it.
            return uk ? program (true,
                                 tones (0.4, kUkDb, 400.0),
                                 silence (0.35),
                                 tones (0.225, kUkLoudDb, 400.0),
                                 silence (0.525))
                      : program (true,
                                 tones (0.25, kBusyDb, 480.0, 620.0),
                                 silence (0.25));

        case Tone::unobtainable:
            // The UK has a dedicated number-unobtainable tone; North America
            // does not -- the condition is signalled by an intercept SIT and
            // then a recording, so this key gives the *long* SIT variant there
            // and the short one stays on its own key.
            return uk ? program (false, tones (kForever, kUkDb, 400.0))
                      : sitProgram (true);

        case Tone::howler:
            // No BT-specific figure was obtainable, so both regions get the
            // North American howler. Saying so beats inventing one.
            return howlerProgram();

        case Tone::callWaiting:
            // North America: one 440 Hz burst of 300 ms, repeated after ten
            // seconds. The UK: two short bursts of 400 Hz instead.
            return uk ? program (true,
                                 tones (0.1, kUkDb, 400.0),
                                 silence (0.1),
                                 tones (0.1, kUkDb, 400.0),
                                 silence (9.7))
                      : program (true,
                                 tones (0.3, kRingbackDb, 440.0),
                                 silence (9.7));

        case Tone::sit:
            return sitProgram (false);

        case Tone::faxCalling:
            // CNG: what a fax machine sends while it waits for an answer.
            return program (true,
                            tones (0.5, kRingbackDb, 1100.0),
                            silence (3.0));

        case Tone::modemAnswer:
            // CED: the answering modem's continuous 2100 Hz.
            return program (false, tones (kForever, kRingbackDb, 2100.0));

        case Tone::singleFrequency:
            // 2600 Hz: the single-frequency supervision tone that told an old
            // trunk the line was idle. Historically interesting, and a clean
            // 2600 Hz sine is useful on its own.
            return program (false, tones (kForever, kRingbackDb, 2600.0));

        case Tone::rotaryPulse:
            return rotaryProgram();

        // -- the constituent frequencies, for sound design -------------------

        case Tone::row697:  return program (false, tones (kForever, kDialDb, kDtmfRowHz[0]));
        case Tone::row770:  return program (false, tones (kForever, kDialDb, kDtmfRowHz[1]));
        case Tone::row852:  return program (false, tones (kForever, kDialDb, kDtmfRowHz[2]));
        case Tone::row941:  return program (false, tones (kForever, kDialDb, kDtmfRowHz[3]));
        case Tone::col1209: return program (false, tones (kForever, kDialDb, kDtmfColHz[0]));
        case Tone::col1336: return program (false, tones (kForever, kDialDb, kDtmfColHz[1]));
        case Tone::col1477: return program (false, tones (kForever, kDialDb, kDtmfColHz[2]));
        case Tone::col1633: return program (false, tones (kForever, kDialDb, kDtmfColHz[3]));

        default:
            // Tone::dialNumber has no program of its own -- the engine plays
            // the stored string -- and neither does Tone::count.
            return ToneProgram {};
    }
}

/// A tone's name, for the editor and for test failure messages.
[[nodiscard]] inline const char* nameFor (Tone tone) noexcept
{
    switch (tone)
    {
        case Tone::digit1: return "1";
        case Tone::digit2: return "2";
        case Tone::digit3: return "3";
        case Tone::digit4: return "4";
        case Tone::digit5: return "5";
        case Tone::digit6: return "6";
        case Tone::digit7: return "7";
        case Tone::digit8: return "8";
        case Tone::digit9: return "9";
        case Tone::star:   return "*";
        case Tone::digit0: return "0";
        case Tone::hash:   return "#";
        case Tone::dtmfA:  return "A";
        case Tone::dtmfB:  return "B";
        case Tone::dtmfC:  return "C";
        case Tone::dtmfD:  return "D";

        case Tone::dialTone:        return "Dial tone";
        case Tone::busy:            return "Busy";
        case Tone::ringback:        return "Ringback";
        case Tone::congestion:      return "Congestion";
        case Tone::unobtainable:    return "Unobtainable";
        case Tone::howler:          return "Howler";
        case Tone::callWaiting:     return "Call waiting";
        case Tone::sit:             return "SIT intercept";
        case Tone::faxCalling:      return "Fax CNG";
        case Tone::modemAnswer:     return "Modem CED";
        case Tone::singleFrequency: return "SF 2600";
        case Tone::rotaryPulse:     return "Rotary pulse";

        case Tone::row697:  return "697 Hz";
        case Tone::row770:  return "770 Hz";
        case Tone::row852:  return "852 Hz";
        case Tone::row941:  return "941 Hz";
        case Tone::col1209: return "1209 Hz";
        case Tone::col1336: return "1336 Hz";
        case Tone::col1477: return "1477 Hz";
        case Tone::col1633: return "1633 Hz";

        case Tone::dialNumber: return "Dial number";
        case Tone::count:      break;
    }

    return "";
}

/// How long one pass of a program takes. Negative for anything that holds
/// until the key is released.
[[nodiscard]] inline double periodSeconds (const ToneProgram& program) noexcept
{
    double total = 0.0;

    for (int i = 0; i < program.stepCount; ++i)
    {
        if (program.steps[i].seconds < 0.0)
            return kForever;

        total += program.steps[i].seconds;
    }

    return total;
}

// ---------------------------------------------------------------------------
// The keyboard map
// ---------------------------------------------------------------------------
//
// Drum-sampler style, from a movable root. The layout is deliberate rather
// than merely ordered: the first octave *is* a telephone keypad, so `1` on
// the keypad is the root, `#` is the B a semitone below the next octave, and
// a phone number played by hand is a melody in that octave.

/// The tone a MIDI note plays, or `Tone::count` for a note outside the map.
[[nodiscard]] constexpr Tone toneForNote (int midiNote, int mapRoot = kDefaultMapRoot) noexcept
{
    const int offset = midiNote - mapRoot;

    if (offset < 0 || offset >= kToneCount)
        return Tone::count;

    return static_cast<Tone> (offset);
}

/// The MIDI note that plays a tone.
[[nodiscard]] constexpr int noteForTone (Tone tone, int mapRoot = kDefaultMapRoot) noexcept
{
    return mapRoot + static_cast<int> (tone);
}

} // namespace tezla::crossbar
