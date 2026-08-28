// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

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

// Two architectures, two completely different control registers -- and the
// second one is easy to forget, because leaving it out produces no error, no
// warning and no crash. It produces a guard that silently does nothing, which
// is exactly what shipped here until a macOS CI run caught it: the runners are
// Apple Silicon, and every modern Mac is.
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #define TEZLA_DSP_DENORMAL_X86   1
  #define TEZLA_DSP_DENORMAL_ARM64 0
  #include <xmmintrin.h>
  #include <pmmintrin.h> // _MM_DENORMALS_ZERO_ON (SSE3)
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define TEZLA_DSP_DENORMAL_X86   0
  #define TEZLA_DSP_DENORMAL_ARM64 1
  #include <cstdint>
  #if defined(_MSC_VER)
    #include <intrin.h>
  #endif
#else
  #define TEZLA_DSP_DENORMAL_X86   0
  #define TEZLA_DSP_DENORMAL_ARM64 0
#endif

#define TEZLA_DSP_DENORMAL_CONTROL (TEZLA_DSP_DENORMAL_X86 || TEZLA_DSP_DENORMAL_ARM64)

namespace tezla::dsp {

namespace detail {

#if TEZLA_DSP_DENORMAL_X86

using DenormalRegister = unsigned int;

/// FTZ (bit 15): results that would be subnormal are written as zero.
/// DAZ (bit 6):  subnormal inputs are treated as zero.
inline constexpr DenormalRegister kDenormalOffBits = 0x8040u;

[[nodiscard]] inline DenormalRegister readDenormalRegister() noexcept { return _mm_getcsr(); }
inline void writeDenormalRegister (DenormalRegister value) noexcept { _mm_setcsr (value); }

#elif TEZLA_DSP_DENORMAL_ARM64

using DenormalRegister = std::uint64_t;

/// FPCR bit 24 is FZ. AArch64 has no separate DAZ: FZ covers subnormal inputs
/// and outputs together for everything except half precision.
inline constexpr DenormalRegister kDenormalOffBits = DenormalRegister { 1 } << 24;

[[nodiscard]] inline DenormalRegister readDenormalRegister() noexcept
{
  #if defined(_MSC_VER)
    return static_cast<DenormalRegister> (_ReadStatusReg (ARM64_FPCR));
  #else
    DenormalRegister value;
    __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (value));
    return value;
  #endif
}

inline void writeDenormalRegister (DenormalRegister value) noexcept
{
  #if defined(_MSC_VER)
    _WriteStatusReg (ARM64_FPCR, static_cast<__int64> (value));
  #else
    __asm__ __volatile__ ("msr fpcr, %0" : : "r" (value));
  #endif
}

#else

using DenormalRegister = unsigned int;
inline constexpr DenormalRegister kDenormalOffBits = 0;
[[nodiscard]] inline DenormalRegister readDenormalRegister() noexcept { return 0; }
inline void writeDenormalRegister (DenormalRegister) noexcept {}

#endif

} // namespace detail

class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if TEZLA_DSP_DENORMAL_CONTROL
        previous_ = detail::readDenormalRegister();
        detail::writeDenormalRegister (previous_ | detail::kDenormalOffBits);
#endif
    }

    ~ScopedNoDenormals() noexcept
    {
#if TEZLA_DSP_DENORMAL_CONTROL
        detail::writeDenormalRegister (previous_);
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

    /// True when this build can actually flush denormals. A target where this
    /// is false needs its own control-register path adding before it can be
    /// supported -- not a test that skips itself, which is how a guard that
    /// does nothing gets to look like a guard that works.
    static constexpr bool isSupported() noexcept { return TEZLA_DSP_DENORMAL_CONTROL != 0; }

private:
#if TEZLA_DSP_DENORMAL_CONTROL
    detail::DenormalRegister previous_ {};
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
