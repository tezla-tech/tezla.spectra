#pragma once

// The FFT itself now lives in the DSP library, because plugins link that and not
// this one, and the spectrum display needs it. These names are kept so every
// existing measurement and test carries on reading the way it did.

#include <tezla/dsp/Fft.hpp>

namespace tezla::measure {

using Complex  = tezla::dsp::Complex;
using Spectrum = tezla::dsp::Spectrum;

using tezla::dsp::isPowerOfTwo;
using tezla::dsp::fft;
using tezla::dsp::fftOfReal;

} // namespace tezla::measure
