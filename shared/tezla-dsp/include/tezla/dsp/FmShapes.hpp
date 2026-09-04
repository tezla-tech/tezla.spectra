// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The waveform an FM operator reads, and why it is a table of harmonics.
//
// ---------------------------------------------------------------------------
// A BLEP oscillator cannot be an FM operator
// ---------------------------------------------------------------------------
//
// `BlepOscillator` is the house band-limited generator and it is the obvious
// thing to reach for here. It is the wrong thing, and the reason is structural
// rather than a matter of tuning: BLEP corrects a waveform's discontinuity
// using the phase **increment** -- how far the phase moves per sample -- and an
// FM operator is read at a phase that has been *displaced*, by an amount that
// changes every sample and can move backwards. The correction is computed for a
// step the reader is not taking. It does not merely sound worse; it is
// answering a different question.
//
// ---------------------------------------------------------------------------
// So the shapes are additive, and the harmonic count is the point
// ---------------------------------------------------------------------------
//
// Each shape is a fixed sum of sine harmonics rendered once into a one-cycle
// table and read with linear interpolation at whatever phase arrives. That
// makes it:
//
//  - **band-limited by construction** -- there is no discontinuity to correct,
//    because the wave is a finite sum of sines and always was;
//  - **readable at any phase**, forwards, backwards or in jumps, which is the
//    thing an operator actually needs;
//  - and, the reason this matters more here than anywhere else, its bandwidth
//    is a **known integer**. `FmBandwidth`'s predicted top sideband is stated
//    in multiples of the modulator's frequency, and a modulator carrying `n`
//    harmonics puts its ladder `n` times further out. That number is
//    `harmonicCount`, and the index cap multiplies by it -- so a non-sine
//    operator is protected by arithmetic rather than by luck.
//
// In FM a non-sine operator is not a cosmetic choice. A saw modulator at index
// 4 is an aliasing catastrophe where a sine at the same index is clean, because
// the harmonics multiply rather than add. Sixteen harmonics on the saw-ish
// shape is a deliberate ceiling: it is enough to hear the difference and few
// enough that the cap can still find a scale that fits under Nyquist.
//
// ---------------------------------------------------------------------------
// Sine is index 0 and is bit-exact
// ---------------------------------------------------------------------------
//
// CLAUDE.md section 7: "almost identity" means every existing project changes
// the day the plugin updates. Sine does not go through the table at all -- the
// operator branches to `std::sin` exactly as it did before this file existed,
// so every Stryda patch saved before shapes shipped renders bit for bit.
//
// The list is a choice parameter, so it is **append-only and frozen** from the
// commit it ships in (CLAUDE.md section 8): a saved patch stores the index.

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace tezla::dsp
{

/// **Append-only.** New shapes go on the end, always.
enum class FmShape
{
    sine = 0,      ///< one harmonic; the default, and bit-exactly std::sin
    bright,        ///< two harmonics -- the cheapest useful step away from a sine
    triangle,      ///< odd harmonics, 1/n^2: soft, and the mildest of the rest
    square,        ///< odd harmonics, 1/n: hollow, and where a reese lives
    saw,           ///< every harmonic, 1/n: the brightest, and the one to watch
    halfSine,      ///< a rectified sine: even harmonics, a hard nasal edge

    count
};

inline constexpr const char* fmShapeNames[] {
    "Sine", "Bright", "Triangle", "Square", "Saw", "Half sine"
};

static_assert (std::size (fmShapeNames) == static_cast<std::size_t> (FmShape::count),
               "fmShapeNames must have one entry per FmShape");

/// How many harmonics of the operator's own frequency a shape carries.
///
/// **This is what the bandwidth predictor multiplies by**, so it is a promise
/// rather than a description: the table below must not contain a partial above
/// this number, or the index cap under-protects.
[[nodiscard]] inline constexpr int fmShapeHarmonics (FmShape shape) noexcept
{
    switch (shape)
    {
        case FmShape::sine:     return 1;
        case FmShape::bright:   return 2;
        case FmShape::triangle: return 15;   // odd only, so 8 partials
        case FmShape::square:   return 15;   // odd only, so 8 partials
        case FmShape::saw:      return 16;
        case FmShape::halfSine: return 8;
        case FmShape::count:    break;
    }

    return 1;
}

/// One cycle of every shape, built once and shared.
///
/// A table rather than a per-sample harmonic sum because the sum is 16
/// transcendentals per sample per operator and this is 6 operators times up to
/// 32 voices: the table is two loads and a multiply-add.
class FmShapeTables
{
public:
    /// Power of two, so the index wrap is a mask and the interpolation weight
    /// falls out of the fraction.
    static constexpr std::size_t kSize = 4096;

    static const FmShapeTables& instance()
    {
        static const FmShapeTables tables;
        return tables;
    }

    /// Read shape `shape` at `phase` in cycles. The phase may be anything at
    /// all -- that is the whole reason this is a table.
    ///
    /// -----------------------------------------------------------------------
    /// **Catmull-Rom, and this is headroom rather than a fix**
    /// -----------------------------------------------------------------------
    ///
    /// Interpolation error behaves differently here than it does in a sampler,
    /// and that is the thing worth knowing: read at a phase that walks forward
    /// at a constant rate, the error is periodic and lands on the harmonics --
    /// which is why "does every shape stop where it promises" reads -300 dB
    /// whatever the interpolator. Read at a *modulated* phase, which is the
    /// only way an FM operator ever reads, it is broadband and inharmonic. So
    /// the number that matters is the inharmonic floor with the table driving a
    /// carrier, measured on the 16-harmonic saw at index 1 (the worst shape at
    /// a realistic index), 192 kHz, 65 536 samples, carrier exactly on a bin:
    ///
    ///     table    linear      cubic
    ///       512    -78.8 dB    -99.6 dB
    ///      1024    -90.7      -129.6
    ///      2048    -90.7      -129.6
    ///      4096   -103.5      -148.8
    ///      8192   -116.6      -167.5
    ///
    /// **Linear at 2048 was already thirty dB under CLAUDE.md section 7's
    /// -60 dB gate**, so cubic is not repairing anything: it buys about 39 dB
    /// at the same table size for four loads instead of two, and that headroom
    /// is worth having because the gate is stated for the audible band and this
    /// is a modulator whose error lands wherever the carrier puts it.
    ///
    /// Said plainly because it nearly went in the other way round: a -36.6 dB
    /// figure was measured mid-session and read as a defect in linear, and it
    /// came from a tree that still had a break-check patch in it. The table
    /// above is from a standalone harness against the real series, and the two
    /// builds it overlaps with agree with `tests/test_FmShapes.cpp` exactly.
    /// **No test in the suite distinguishes cubic from linear at 4096**, since
    /// both clear the gate by 40 dB or more; the justification is the table,
    /// not a red test.
    [[nodiscard]] double read (FmShape shape, double phase) const noexcept
    {
        if (shape == FmShape::sine)
            return std::sin (2.0 * std::numbers::pi * phase);

        const double wrapped = phase - std::floor (phase);
        const double position = wrapped * static_cast<double> (kSize);

        const auto index = static_cast<std::size_t> (position);
        const double t = position - std::floor (position);

        const auto& table = tables_[static_cast<std::size_t> (shape)];

        // The wrap is a mask because kSize is a power of two, and `- 1` on an
        // unsigned index wraps to kSize - 1 before the mask, which is exactly
        // the point one below zero.
        const double y0 = table[(index - 1) & (kSize - 1)];
        const double y1 = table[index & (kSize - 1)];
        const double y2 = table[(index + 1) & (kSize - 1)];
        const double y3 = table[(index + 2) & (kSize - 1)];

        // Catmull-Rom in Horner form. Standard cubic interpolation; the
        // arrangement is the usual one and nothing here is taken from anywhere.
        const double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
        const double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c = -0.5 * y0 + 0.5 * y2;

        return ((a * t + b) * t + c) * t + y1;
    }

private:
    FmShapeTables()
    {
        for (int s = 0; s < static_cast<int> (FmShape::count); ++s)
        {
            const auto shape = static_cast<FmShape> (s);
            auto& table = tables_[static_cast<std::size_t> (s)];

            double peak = 0.0;

            for (std::size_t i = 0; i < kSize; ++i)
            {
                const double phase = static_cast<double> (i) / static_cast<double> (kSize);
                const double value = sum (shape, phase);

                table[i] = value;
                peak = std::max (peak, std::abs (value));
            }

            // **Normalised to unit peak, and that is not cosmetic.** An
            // operator's output is a phase deviation in cycles for whatever it
            // modulates, so a shape whose peak were 1.27 would silently deliver
            // 27 % more index than the matrix cell says -- and the index cap
            // would be capping a number that is not the one doing the work.
            if (peak > 0.0)
                for (auto& value : table)
                    value /= peak;
        }
    }

    /// The harmonic sum, evaluated once per table entry at build time.
    ///
    /// Every one of these is the ordinary Fourier series of the named wave,
    /// truncated at `fmShapeHarmonics`. Nothing is fitted or taken from
    /// anywhere: they are the textbook series and the truncation is ours.
    [[nodiscard]] static double sum (FmShape shape, double phase) noexcept
    {
        constexpr double kTwoPi = 2.0 * std::numbers::pi;
        const double theta = kTwoPi * phase;
        const int harmonics = fmShapeHarmonics (shape);

        double value = 0.0;

        switch (shape)
        {
            case FmShape::sine:
                return std::sin (theta);

            case FmShape::bright:
                // Two harmonics, the second at a third: enough to hear, and
                // still narrow enough to modulate hard with.
                return std::sin (theta) + (1.0 / 3.0) * std::sin (2.0 * theta);

            case FmShape::triangle:
                for (int n = 1; n <= harmonics; n += 2)
                {
                    const double sign = ((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
                    value += sign * std::sin (static_cast<double> (n) * theta)
                               / (static_cast<double> (n) * static_cast<double> (n));
                }
                return value;

            case FmShape::square:
                for (int n = 1; n <= harmonics; n += 2)
                    value += std::sin (static_cast<double> (n) * theta) / static_cast<double> (n);
                return value;

            case FmShape::saw:
                for (int n = 1; n <= harmonics; ++n)
                    value += std::sin (static_cast<double> (n) * theta) / static_cast<double> (n);
                return value;

            case FmShape::halfSine:
                // |sin| has only even harmonics: 2/pi - (4/pi) * sum over even
                // n of cos(n theta) / (n^2 - 1). Built from the series rather
                // than by calling std::abs, so the truncation is explicit and
                // the wave is band-limited rather than merely rectified.
                value = 2.0 / std::numbers::pi;

                for (int n = 2; n <= harmonics; n += 2)
                    value -= (4.0 / std::numbers::pi) * std::cos (static_cast<double> (n) * theta)
                               / (static_cast<double> (n) * static_cast<double> (n) - 1.0);

                // Centred, so it is a modulator rather than a modulator with a
                // DC offset -- an offset here is a constant phase shift on
                // whatever it drives, which is inaudible and misleading.
                return value - 2.0 / std::numbers::pi;

            case FmShape::count:
                break;
        }

        return std::sin (theta);
    }

    std::array<std::array<double, kSize>, static_cast<std::size_t> (FmShape::count)> tables_ {};
};

} // namespace tezla::dsp
