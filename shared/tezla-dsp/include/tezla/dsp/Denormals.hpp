#pragma once

// Denormal (subnormal) floating-point numbers cost 50-100x a normal operation
// on x86. In a recursive filter or an envelope follower, a signal decaying into
// silence produces them by the thousand, so a plugin that is idle can burn more
// CPU than one that is working. The fix is to tell the FPU to flush them to
// zero for the duration of the audio callback.
//
// Usage, once per processBlock, at the top:
//
//     const tezla::dsp::ScopedNoDenormals noDenormals;
//
// It is RAII: the previous FPU mode is restored on scope exit, which matters
// because the host's own code runs on the same thread and did not ask for FTZ.

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #define TEZLA_DSP_X86_SSE 1
  #include <xmmintrin.h>
  #include <pmmintrin.h> // _MM_DENORMALS_ZERO_ON (SSE3)
#else
  #define TEZLA_DSP_X86_SSE 0
#endif

namespace tezla::dsp {

class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if TEZLA_DSP_X86_SSE
        previousMxcsr_ = _mm_getcsr();
        // FTZ: results that would be subnormal are written as zero.
        // DAZ: subnormal inputs are treated as zero.
        _mm_setcsr(previousMxcsr_ | 0x8040u);
#endif
    }

    ~ScopedNoDenormals() noexcept
    {
#if TEZLA_DSP_X86_SSE
        _mm_setcsr(previousMxcsr_);
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

    /// True when this build can actually flush denormals. Non-x86 targets fall
    /// back to a no-op, so recursive state there still needs manual snapping.
    static constexpr bool isSupported() noexcept { return TEZLA_DSP_X86_SSE != 0; }

private:
#if TEZLA_DSP_X86_SSE
    unsigned int previousMxcsr_ {};
#endif
};

/// Belt-and-braces snap for state that must not creep towards a denormal even
/// on a target without FTZ. Cheap enough to use on filter state at block rate.
template <typename Float>
[[nodiscard]] inline Float snapToZero (Float value) noexcept
{
    constexpr Float threshold = static_cast<Float>(1.0e-30);
    return (value > -threshold && value < threshold) ? Float{} : value;
}

} // namespace tezla::dsp
