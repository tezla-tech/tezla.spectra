#pragma once

// Small radix-2 FFT.
//
// Clarity and numerical accuracy beat speed here, so the twiddle factors are
// computed exactly rather than accumulated. That makes it unsuitable for the
// audio thread and perfectly suitable everywhere else it is used: offline
// measurement, and the spectrum display, which runs on the message thread at
// 30 frames a second.
//
// It lives in the DSP library rather than in the measurement one because a
// plugin links only the former, and the spectrum analyser needs it.

#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

namespace tezla::dsp {

using Complex  = std::complex<double>;
using Spectrum = std::vector<Complex>;

[[nodiscard]] inline bool isPowerOfTwo (std::size_t n) noexcept
{
    return n != 0 && (n & (n - 1)) == 0;
}

/// In-place forward FFT. Size must be a power of two.
inline void fft (Spectrum& data)
{
    const std::size_t n = data.size();
    if (n < 2 || ! isPowerOfTwo (n))
        return;

    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;

        if (i < j)
            std::swap (data[i], data[j]);
    }

    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const std::size_t half = length / 2;

        for (std::size_t start = 0; start < n; start += length)
        {
            for (std::size_t k = 0; k < half; ++k)
            {
                const double angle = -2.0 * std::numbers::pi * static_cast<double> (k)
                                          / static_cast<double> (length);
                const Complex twiddle { std::cos (angle), std::sin (angle) };

                const Complex even = data[start + k];
                const Complex odd  = data[start + k + half] * twiddle;

                data[start + k]        = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }
}

/// Forward FFT of a real signal, returned as the full complex spectrum.
[[nodiscard]] inline Spectrum fftOfReal (const std::vector<double>& signal)
{
    Spectrum data (signal.size());
    for (std::size_t i = 0; i < signal.size(); ++i)
        data[i] = Complex { signal[i], 0.0 };

    fft (data);
    return data;
}

} // namespace tezla::dsp
