# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

# Shared compiler settings, exposed as an interface target every Tezla target
# links against: tezla::compiler-options

add_library(tezla_compiler_options INTERFACE)
add_library(tezla::compiler-options ALIAS tezla_compiler_options)

# AVX2 is x86-only. On Apple Silicon -- or any ARM target -- passing -mavx2
# fails the build outright, so the option is ignored rather than obeyed there.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$"
   OR CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
    set(_tezla_is_x86 TRUE)
else()
    set(_tezla_is_x86 FALSE)
endif()

if(TEZLA_ENABLE_AVX2 AND NOT _tezla_is_x86)
    message(WARNING "TEZLA_ENABLE_AVX2 is set but this is not an x86 target; ignoring it.")
endif()

# A universal macOS build compiles every file once per slice with the same
# flags, and Apple clang refuses -mavx2 for the arm64 slice, so the option
# cannot apply to one. It should not want to: AVX2 is for a build of the
# machine in front of you, and a universal bundle is for handing out. Fail with
# the fix named rather than let the arm64 slice fail deep inside the build.
list(LENGTH CMAKE_OSX_ARCHITECTURES _tezla_osx_arch_count)
if(TEZLA_ENABLE_AVX2 AND _tezla_is_x86 AND _tezla_osx_arch_count GREATER 1)
    message(FATAL_ERROR
        "TEZLA_ENABLE_AVX2 cannot apply to a universal (${CMAKE_OSX_ARCHITECTURES}) build: "
        "the arm64 slice would be compiled with -mavx2 and fail. Build for one "
        "architecture instead: ./scripts/build.sh --native --avx2, or "
        "-DCMAKE_OSX_ARCHITECTURES=x86_64. A universal bundle is for handing out, "
        "and an AVX2 binary should not be.")
endif()

if(MSVC)
    target_compile_options(tezla_compiler_options INTERFACE
        /W4                 # warnings are how DSP bugs announce themselves early
        /permissive-        # standard-conforming mode
        /utf-8
        /Zc:__cplusplus     # otherwise __cplusplus lies and C++20 checks fail
        /MP)                # parallel compilation

    # Do NOT enable /fp:fast. It licenses the compiler to reassociate floating
    # point, which quietly changes filter and nonlinearity behaviour.
    #
    # /fp:precise also disables contraction into fused multiply-add, which is
    # the property the GCC/Clang branch below has to ask for explicitly.
    target_compile_options(tezla_compiler_options INTERFACE /fp:precise)

    if(TEZLA_WARNINGS_AS_ERRORS)
        target_compile_options(tezla_compiler_options INTERFACE /WX)
    endif()
    if(TEZLA_ENABLE_AVX2 AND _tezla_is_x86)
        target_compile_options(tezla_compiler_options INTERFACE /arch:AVX2)
    endif()
else()
    target_compile_options(tezla_compiler_options INTERFACE
        -Wall -Wextra -Wshadow -Wpedantic)

    # Same reasoning as above: no -ffast-math, ever.
    target_compile_options(tezla_compiler_options INTERFACE -fno-fast-math)

    # And -fno-fast-math is not enough on its own, which was found by running
    # the suite under qemu-aarch64. GCC's default is -ffp-contract=fast: it may
    # fuse `a * b + c` into a single multiply-add with one rounding instead of
    # two. On x86-64 that is invisible, because without -mfma there is no
    # instruction to fuse into. On AArch64 `fmadd` is baseline, so it always
    # fuses -- and the same source then computes different numbers on Apple
    # Silicon than on the machine it was written on.
    #
    # That surfaced as a state-variable filter failing to match the linear
    # difference equation it implements, bit for bit, on ARM64 only. The
    # bit-exact neutral CLAUDE.md section 7 asks for is not a property a build
    # can be allowed to vary on, so contraction is off everywhere. It also
    # matters on x86 the moment TEZLA_ENABLE_AVX2 adds -mfma below.
    #
    # MSVC's /fp:precise already behaves this way, so this is what makes the
    # three toolchains agree rather than a preference between them.
    target_compile_options(tezla_compiler_options INTERFACE -ffp-contract=off)

    if(TEZLA_WARNINGS_AS_ERRORS)
        target_compile_options(tezla_compiler_options INTERFACE -Werror)
    endif()
    if(TEZLA_ENABLE_AVX2 AND _tezla_is_x86)
        target_compile_options(tezla_compiler_options INTERFACE -mavx2 -mfma)
    endif()
endif()

# ------------------------------------------------------------------ LTO ------
# Link-time optimisation, off by default -- see the comment on the option in
# the root CMakeLists.txt for the measurement behind that.
#
# It lives here, on the interface target every Tezla target links, rather than
# on the plugin targets alone as JUCE's `juce_recommended_lto_flags` would put
# it -- for two reasons:
#
#  * The instrument must match the thing it measures. `tezla-tests` and
#    `tezla-measure` link this target too, so a `-DTEZLA_LTO=ON` build gives
#    the CPU-budget tests the same optimisation the shipped plugin gets. A
#    number measured on one configuration and shipped on another is not a
#    measurement.
#
#  * JUCE's target passes a plain `-flto`, which is **serial** on GCC and
#    **monolithic** on Apple clang (run 38: six hours in one link). The forms
#    below parallelise: `-flto=auto` lets GCC use every core for the LTRANS
#    stage, and `-flto=thin` is clang's parallel, incremental variant. MSVC's
#    /GL + /LTCG is what JUCE passes and is already multi-threaded.
#
# Release-only, as JUCE does it: a Debug build with LTO is slow to link and
# useless to debug.
if(TEZLA_LTO)
    if(MSVC)
        target_compile_options(tezla_compiler_options INTERFACE $<$<CONFIG:Release>:/GL>)
        target_link_options(tezla_compiler_options INTERFACE $<$<CONFIG:Release>:/LTCG>)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")   # Clang and AppleClang
        target_compile_options(tezla_compiler_options INTERFACE $<$<CONFIG:Release>:-flto=thin>)
        target_link_options(tezla_compiler_options INTERFACE $<$<CONFIG:Release>:-flto=thin>)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(tezla_compiler_options INTERFACE $<$<CONFIG:Release>:-flto=auto>)
        target_link_options(tezla_compiler_options INTERFACE $<$<CONFIG:Release>:-flto=auto>)
    else()
        message(WARNING "TEZLA_LTO is set but this compiler (${CMAKE_CXX_COMPILER_ID}) has no LTO flags configured here; ignoring it.")
    endif()
    message(STATUS "TEZLA_LTO: on (Release only; see cmake/TezlaCompilerOptions.cmake)")
endif()
