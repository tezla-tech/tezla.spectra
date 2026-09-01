// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Minimum-phase FIR design from a magnitude target, by real cepstrum.
//
// Give it log |H| sampled at the design FFT's bins up to Nyquist; it returns
// an impulse whose magnitude matches and whose phase is the minimum-phase
// one -- zero latency, no pre-ring, energy packed at the front. The steps:
//
//     log-magnitude  --inverse FFT-->  real cepstrum
//     fold the anticausal half onto the causal  (keep c[0] and Nyquist,
//                                                double 1..N/2-1, zero rest)
//     --forward FFT-->  log H, now analytic  --exp-->  H  --inverse FFT-->  h
//
// Folding works because a minimum-phase system's log-spectrum is analytic:
// its phase is the Hilbert transform of its log magnitude, and the fold IS
// that transform done in the cepstral domain. See Oppenheim & Schafer,
// "Discrete-Time Signal Processing", the homomorphic chapter -- the standard
// construction, no licence to carry.
//
// What the caller owes the input: the target must not touch zero (a
// minimum-phase system cannot carry a unit-circle zero, and log(0) is the
// crash you would expect), so floor the magnitude first. WHERE to floor is a
// voicing decision that belongs to the caller: TapeLoss floors at -60 dB
// because its measured match degraded with a deeper floor (the impulse
// stretched past what its tap budget carries), and a different target may
// afford a different floor. Sample the target at bin * sampleRate / FftSize;
// leave everything above the last bin to the mirror.
//
// History, and the regression that guards it: this machinery was written for
// Ferrite's TapeLoss and lifted here unchanged when Membrana's CapsuleEq
// needed the same construction. "Unchanged" is load-bearing --
// tests/test_MinimumPhaseFir.cpp pins Ferrite's designed taps as BIT
// PATTERNS (FNV-1a over all taps, plus spot values, at four sample rates and
// two tape speeds) against the pre-lift design. Touch the arithmetic here --
// reorder a butterfly, "simplify" the fold -- and that test goes red, which
// is the point: every Ferrite project in existence renders through these
// exact bits. That is also why this file carries its own FFT kernel rather
// than calling Fft.hpp: the kernel's operation order is part of the pinned
// behaviour.
//
// Design-time only. The work is bounded and allocation-free (the scratch
// lives on the stack: eight FftSize arrays, 64 KB at 1024 points), so it is
// safe to call from a control-chunk boundary the way TapeLoss and CapsuleEq
// do, but it has no business inside a per-sample loop.

#include <cstddef>

#include <cmath>

namespace tezla::dsp {

template <int FftSize = 1024>
class MinimumPhaseFir
{
public:
    static_assert (FftSize > 0 && (FftSize & (FftSize - 1)) == 0,
                   "the design FFT is radix-2");

    static constexpr int kFft = FftSize;

    /// The same size as an unsigned quantity, for array bounds -- gcc's
    /// -Wsign-conversion flags a signed template parameter used as a bound.
    static constexpr std::size_t kSize = static_cast<std::size_t> (FftSize);

    /// Design tapCount minimum-phase taps from log |H| sampled at bins
    /// 0..FftSize/2 inclusive (halfLogMag has FftSize/2 + 1 values). The
    /// caller has already floored the magnitude away from zero.
    static void design (const double* halfLogMag, double* taps, int tapCount) noexcept
    {
        const auto at = [] (int index) { return static_cast<std::size_t> (index); };

        // 1. Mirror the half spectrum about Nyquist: the log magnitude of a
        // real impulse is even.
        double logMag[kSize] {};

        for (int bin = 0; bin <= kFft / 2; ++bin)
        {
            logMag[at (bin)] = halfLogMag[at (bin)];

            if (bin > 0 && bin < kFft / 2)
                logMag[at (kFft - bin)] = halfLogMag[at (bin)];
        }

        // 2. Real cepstrum: inverse FFT of the log magnitude (real, even).
        double re[kSize] {}, im[kSize] {};
        fft (logMag, nullptr, re, im, true);

        // 3. Fold the anticausal half onto the causal: the minimum-phase
        // cepstrum keeps c[0] and the Nyquist point, doubles 1..N/2-1, and
        // zeroes the rest.
        double folded[kSize] {};
        folded[0] = re[0];
        folded[at (kFft / 2)] = re[at (kFft / 2)];

        for (int n = 1; n < kFft / 2; ++n)
            folded[at (n)] = 2.0 * re[at (n)];

        // 4. Exponentiate in the frequency domain and come back to time.
        double fre[kSize] {}, fim[kSize] {};
        fft (folded, nullptr, fre, fim, false);

        for (int bin = 0; bin < kFft; ++bin)
        {
            const double magnitude = std::exp (fre[at (bin)]);
            const double phase = fim[at (bin)];
            fre[at (bin)] = magnitude * std::cos (phase);
            fim[at (bin)] = magnitude * std::sin (phase);
        }

        double hre[kSize] {}, him[kSize] {};
        fft (fre, fim, hre, him, true);

        for (int n = 0; n < tapCount; ++n)
            taps[at (n)] = hre[at (n)];
    }

private:
    static constexpr double kPi = 3.141592653589793;

    static constexpr int bitsFor (int n) noexcept
    {
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        return bits;
    }

    static constexpr int kBits = bitsFor (kFft);

    /// Radix-2 decimation-in-time FFT, design-time only. The textbook
    /// algorithm, kept here rather than in a library because its exact
    /// operation order is pinned by the Ferrite regression (see the header
    /// comment). `inverse` includes the 1/N.
    static void fft (const double* inRe, const double* inIm,
                     double* outRe, double* outIm, bool inverse) noexcept
    {
        // Bit-reversed copy in.
        for (int i = 0; i < kFft; ++i)
        {
            int reversed = 0;

            for (int bit = 0; bit < kBits; ++bit)
                reversed |= ((i >> bit) & 1) << (kBits - 1 - bit);

            outRe[reversed] = inRe[i];
            outIm[reversed] = inIm != nullptr ? inIm[i] : 0.0;
        }

        for (int length = 2; length <= kFft; length <<= 1)
        {
            const double angle = (inverse ? 2.0 : -2.0) * kPi / length;
            const double wRe = std::cos (angle);
            const double wIm = std::sin (angle);

            for (int start = 0; start < kFft; start += length)
            {
                double twRe = 1.0, twIm = 0.0;

                for (int k = 0; k < length / 2; ++k)
                {
                    const int even = start + k;
                    const int odd = start + k + length / 2;

                    const double oddRe = outRe[odd] * twRe - outIm[odd] * twIm;
                    const double oddIm = outRe[odd] * twIm + outIm[odd] * twRe;

                    outRe[odd] = outRe[even] - oddRe;
                    outIm[odd] = outIm[even] - oddIm;
                    outRe[even] += oddRe;
                    outIm[even] += oddIm;

                    const double nextRe = twRe * wRe - twIm * wIm;
                    twIm = twRe * wIm + twIm * wRe;
                    twRe = nextRe;
                }
            }
        }

        if (inverse)
        {
            for (int i = 0; i < kFft; ++i)
            {
                outRe[i] /= kFft;
                outIm[i] /= kFft;
            }
        }
    }
};

} // namespace tezla::dsp
