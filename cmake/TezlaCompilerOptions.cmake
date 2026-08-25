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

if(MSVC)
    target_compile_options(tezla_compiler_options INTERFACE
        /W4                 # warnings are how DSP bugs announce themselves early
        /permissive-        # standard-conforming mode
        /utf-8
        /Zc:__cplusplus     # otherwise __cplusplus lies and C++20 checks fail
        /MP)                # parallel compilation

    # Do NOT enable /fp:fast. It licenses the compiler to reassociate floating
    # point, which quietly changes filter and nonlinearity behaviour.
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

    if(TEZLA_WARNINGS_AS_ERRORS)
        target_compile_options(tezla_compiler_options INTERFACE -Werror)
    endif()
    if(TEZLA_ENABLE_AVX2 AND _tezla_is_x86)
        target_compile_options(tezla_compiler_options INTERFACE -mavx2 -mfma)
    endif()
endif()
