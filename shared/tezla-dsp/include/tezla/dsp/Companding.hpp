// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// ITU-T G.711: the two companding laws that made a telephone sound like a
// telephone.
//
// ---------------------------------------------------------------------------
// Why this is not a bit crusher
// ---------------------------------------------------------------------------
//
// "Eight bits" is the answer everyone gives for what an old phone sounded
// like, and taken literally it is wrong in a way that matters. A linear 8-bit
// quantiser has a fixed step, so its noise floor is fixed too: loud passages
// get a good signal-to-noise ratio and quiet ones get a terrible one, and the
// character is that harsh grainy fizz that appears the moment a sound decays.
// That is a sampler, not a telephone.
//
// G.711's eight bits are **logarithmic**. The step size grows with the signal,
// so the quantisation noise rides up and down with it and the SNR is *the same
// at every level* -- about 38 dB across a 40 dB range, where a linear 8-bit
// quantiser's falls a decibel for every decibel of level. That is what makes a
// phone line sound consistently, evenly grubby rather than fizzy in the gaps,
// and it is the property the test in tests/test_Companding.cpp measures and
// break-checks against a linear quantiser.
//
// The dynamic range is the other half: eight companded bits carry roughly
// what thirteen or fourteen linear ones would, which is why 64 kbit/s was
// enough for a voice channel in 1972 and still is.
//
// ---------------------------------------------------------------------------
// The structure, which is where the sound is
// ---------------------------------------------------------------------------
//
// G.711 is not the smooth logarithm of the textbook formula. It is a
// **piecewise-linear approximation** of it: eight segments, sixteen uniform
// steps inside each, and the step doubling from one segment to the next. So it
// is a tiny floating-point format -- a sign bit, a three-bit exponent and a
// four-bit mantissa -- and the segment boundaries are simply where the octaves
// fall:
//
//     mu-law   segment s ends at (64 << s) - 1   in a 14-bit magnitude domain
//     A-law    segment s ends at (32 << s) - 1   in a 13-bit magnitude domain
//
// which is why this file derives them rather than carrying a table. Both laws
// reconstruct at the **midpoint** of the interval the encoder chose, and a
// test asserts that for all 256 code words of each law rather than trusting
// the arithmetic.
//
// mu-law's bias of 33 is the one constant that is not obvious. It exists so
// that the mantissa's implicit leading bit works out: adding it before the
// segment search guarantees the biased magnitude is at least 33, so the top
// bit of every segment is implied and all sixteen mantissa codes are usable.
// Without it the first segment would collide with itself.
//
// ---------------------------------------------------------------------------
// Sourcing, honestly (CLAUDE.md section 9)
// ---------------------------------------------------------------------------
//
// The ITU-T G.711 text could not be fetched from this container -- itu.int is
// refused by the egress proxy. The constants below (bias 33, clip 8159, the
// two segment layouts) were confirmed against the widely mirrored Sun
// Microsystems `g711.c`, whose header states "users may copy or modify this
// source code without charge". **No code was taken from it**: the segment ends
// are derivable, the bit packing follows from the structure, and everything
// here is then verified by measurement -- midpoint reconstruction for all 256
// codes, the flat-SNR property, round-trip stability, monotonicity, and the
// two ceilings the standard implies. Recorded again in docs/DSP-REFERENCES.md.
//
// Two deliberate deviations, both stated at the point they happen:
//
//  1. **A-law's idle is forced to exact zero.** A-law genuinely has no zero
//     code -- its smallest reconstruction is +/-1 LSB -- which is the real
//     reason A-law idle-channel noise is worse than mu-law's. Reproduced
//     literally that is a +2.44e-4 DC offset sitting on every instance
//     whenever nothing is playing, and CLAUDE.md section 7 requires silence in
//     to be silence out. So an exactly-zero input returns exactly zero. The
//     deviation is one sample value wide and the step it introduces is exactly
//     the size of the quantiser's own step there.
//  2. **The negative side is symmetric.** An integer implementation folds
//     negatives with `-x - 1`, which is an artefact of two's complement rather
//     than of the law, and leaves the negative decision boundaries half a step
//     from the positive ones. This uses the magnitude, so the two sides match.
//     The difference is half of the smallest step: 1.2e-4 of full scale.

#include <algorithm>
#include <cmath>

#include "Bitcrusher.hpp"
#include "Exact.hpp"

namespace tezla::dsp {

/// Which companding law a channel uses.
///
/// **APPEND-ONLY forever** (CLAUDE.md section 8) -- this backs a choice
/// parameter, and a choice parameter stores an index.
enum class CompandingLaw
{
    off = 0,   ///< bit-exact identity
    muLaw,     ///< G.711 mu-law -- North America and Japan
    aLaw,      ///< G.711 A-law -- Europe and most of the rest of the world
    linear     ///< a plain uniform quantiser, for comparison and for crushing
};

/// G.711's two laws, as a quantiser on normalised samples.
///
/// +/-1.0 is the codec's clip point, so a full-scale input lands exactly on
/// the top of the encoder's range and nothing above it is wasted. The peak
/// *output* is therefore slightly below 1.0 -- 0.984312 for mu-law and
/// 0.984615 for A-law -- because both laws reconstruct at interval midpoints
/// and the top interval's midpoint is not its top. That is the codec's real
/// ceiling, and it is 0.137 dB.
class Compander
{
public:
    // -----------------------------------------------------------------------
    // The constants, and where each comes from
    // -----------------------------------------------------------------------

    /// mu-law works in a 14-bit magnitude domain and clips at 8159, which is
    /// exactly 8192 - 33: the clip and the bias are chosen together so the
    /// biased magnitude never exceeds the last segment's range by more than
    /// one.
    static constexpr double kMuScale = 8159.0;
    static constexpr int    kMuBias  = 33;

    /// A-law works in a 13-bit magnitude domain with no bias -- its first two
    /// segments share a step instead, which does the same job.
    static constexpr double kAScale = 4095.0;

    /// The largest magnitude each law can reconstruct, as the standard's
    /// structure implies: the midpoint of the top interval.
    static constexpr double kMuPeak = 8031.0 / kMuScale;   // 0.984312...
    static constexpr double kAPeak  = 4032.0 / kAScale;    // 0.984615...

    static constexpr int kCodeCount = 256;
    static constexpr int kCodeBits  = 8;

    // -----------------------------------------------------------------------
    // Controls
    // -----------------------------------------------------------------------

    /// The depth has to be pushed through once at construction: `Bitcrusher`
    /// defaults to bypassed, which would make a freshly built compander in
    /// linear mode disagree with its own `getBits()`.
    Compander() noexcept { setBits (bits_); }

    void setLaw (CompandingLaw law) noexcept
    {
        law_ = law;
    }

    /// How many bits of the code word survive.
    ///
    /// In a companding mode this is 1 to 8 and it masks the low bits of the
    /// code word, which is what a T1 span did when it stole them for
    /// signalling -- 7-bit mu-law is a sound the network really made. In
    /// linear mode it is 1 to 16 and means what it usually means, with 16
    /// bypassing exactly.
    ///
    /// The mask lands on the **logical** word -- sign, exponent, mantissa --
    /// rather than on the inverted octet that goes down the line. Both are
    /// defensible models of a signalling bit stuck at zero, and the difference
    /// was measured rather than assumed: masking the transmitted octet forces
    /// the mantissa's low bit to *one*, which biases every reconstruction up
    /// by half a step, and a consistent magnitude offset is distortion rather
    /// than a coarser grid. It cost 10.9 dB of SNR on the first bit removed
    /// against 6.9 dB for this one. A control called Bits should coarsen, so
    /// this is the one that ships.
    void setBits (int bits) noexcept
    {
        bits_ = std::clamp (bits, 1, 16);
        crusher_.setBits (static_cast<double> (bits_));

        const int companded = std::min (bits_, kCodeBits);
        codeMask_ = (0xFF << (kCodeBits - companded)) & 0xFF;
    }

    [[nodiscard]] CompandingLaw getLaw() const noexcept { return law_; }
    [[nodiscard]] int getBits() const noexcept { return bits_; }

    /// True when this stage is the identity, bit for bit.
    [[nodiscard]] bool isBypassed() const noexcept
    {
        return law_ == CompandingLaw::off
                 || (law_ == CompandingLaw::linear && bits_ >= 16);
    }

    // -----------------------------------------------------------------------
    // Processing
    // -----------------------------------------------------------------------

    [[nodiscard]] double process (double x) const noexcept
    {
        switch (law_)
        {
            case CompandingLaw::off:
                return x;

            case CompandingLaw::linear:
                return crusher_.process (x);

            case CompandingLaw::muLaw:
                // Exactly zero is already exactly zero through mu-law -- the
                // bias cancels -- so this guard costs nothing here and keeps
                // the two laws' contract identical. See the header for why
                // A-law needs it.
                return isExactlyZero (x)
                         ? x
                         : muLawDecode (muLawTransmit (muLawLogical (muLawEncode (x)) & codeMask_));

            case CompandingLaw::aLaw:
                return isExactlyZero (x)
                         ? x
                         : aLawDecode (aLawTransmit (aLawLogical (aLawEncode (x)) & codeMask_));
        }

        return x;
    }

    // -----------------------------------------------------------------------
    // The codecs themselves -- static, so the tests and the measurement
    // harness can drive them without an instance
    // -----------------------------------------------------------------------

    /// A normalised sample to a mu-law code word, as it would go on the line
    /// (inverted, which is the standard's own convention -- it puts more ones
    /// on an idle channel, which helps a receiver keep clock).
    [[nodiscard]] static int muLawEncode (double x) noexcept
    {
        const int sign = x < 0.0 ? 0x80 : 0x00;

        const double magnitude = std::min (std::abs (x) * kMuScale, kMuScale);
        const int biased = static_cast<int> (magnitude) + kMuBias;

        int segment = 0;

        while (segment < 7 && biased >= (64 << segment))
            ++segment;

        // The clip lands one past the last segment's range -- 8159 + 33 is
        // 8192, and segment 7 ends at 8191 -- so it takes the top code
        // explicitly rather than wrapping the mantissa back to zero.
        const int mantissa = biased > 8191 ? 0xF : (biased >> (segment + 1)) & 0xF;

        return (~(sign | (segment << 4) | mantissa)) & 0xFF;
    }

    [[nodiscard]] static double muLawDecode (int code) noexcept
    {
        const int word = (~code) & 0xFF;
        const int segment = (word >> 4) & 0x7;
        const int mantissa = word & 0xF;

        // The midpoint of the interval this code stands for. Written as
        // (2m + 33) << s minus the bias, which is the biased midpoint
        // (m + 16.5) * 2^(s+1) with the arithmetic kept in integers.
        const int magnitude = (((2 * mantissa) + kMuBias) << segment) - kMuBias;
        const double value = static_cast<double> (magnitude) / kMuScale;

        return (word & 0x80) != 0 ? -value : value;
    }

    /// A normalised sample to an A-law code word, alternate bits inverted as
    /// the standard specifies. A-law's sign bit is **set for positive**, which
    /// is the opposite of mu-law's and is a genuine difference between them.
    [[nodiscard]] static int aLawEncode (double x) noexcept
    {
        const int sign = x < 0.0 ? 0x00 : 0x80;

        const double magnitude = std::min (std::abs (x) * kAScale, kAScale);
        const int m = static_cast<int> (magnitude);

        int segment = 0;

        while (segment < 7 && m >= (32 << segment))
            ++segment;

        // Segments 0 and 1 share a step -- A-law's substitute for mu-law's
        // bias, and the reason it has no zero code.
        const int mantissa = segment < 2 ? (m >> 1) & 0xF : (m >> segment) & 0xF;

        return (sign | (segment << 4) | mantissa) ^ 0x55;
    }

    [[nodiscard]] static double aLawDecode (int code) noexcept
    {
        const int word = (code ^ 0x55) & 0xFF;
        const int segment = (word >> 4) & 0x7;
        const int mantissa = word & 0xF;

        const int magnitude = segment == 0 ? 2 * mantissa + 1
                            : segment == 1 ? 2 * mantissa + 33
                                           : ((2 * mantissa) + 33) << (segment - 1);

        const double value = static_cast<double> (magnitude) / kAScale;

        return (word & 0x80) != 0 ? value : -value;
    }

    /// The logical word behind a transmitted code, and back again.
    ///
    /// mu-law inverts every bit before transmission and A-law inverts the
    /// alternate ones; both are line-coding conventions that put more
    /// transitions on an idle channel so a receiver can keep clock. The
    /// *structure* -- sign, exponent, mantissa -- is in the logical word, so
    /// anything that reasons about the code's fields works on that.
    [[nodiscard]] static int muLawLogical (int code) noexcept  { return (~code) & 0xFF; }
    [[nodiscard]] static int muLawTransmit (int word) noexcept { return (~word) & 0xFF; }
    [[nodiscard]] static int aLawLogical (int code) noexcept   { return (code ^ 0x55) & 0xFF; }
    [[nodiscard]] static int aLawTransmit (int word) noexcept  { return (word ^ 0x55) & 0xFF; }

    /// The width of one quantisation interval in a law's segment, normalised.
    /// The tests use it to check that a code decodes to the middle of its own
    /// interval rather than to an end of it.
    [[nodiscard]] static double muLawStep (int segment) noexcept
    {
        return static_cast<double> (2 << segment) / kMuScale;
    }

    [[nodiscard]] static double aLawStep (int segment) noexcept
    {
        return static_cast<double> (segment < 2 ? 2 : (2 << (segment - 1))) / kAScale;
    }

private:
    CompandingLaw law_ { CompandingLaw::off };
    int bits_ { 8 };
    int codeMask_ { 0xFF };

    Bitcrusher crusher_;
};

} // namespace tezla::dsp
