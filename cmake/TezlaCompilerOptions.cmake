# Shared compiler settings, exposed as an interface target every Tezla target
# links against: tezla::compiler-options

add_library(tezla_compiler_options INTERFACE)
add_library(tezla::compiler-options ALIAS tezla_compiler_options)

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
    if(TEZLA_ENABLE_AVX2)
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
    if(TEZLA_ENABLE_AVX2)
        target_compile_options(tezla_compiler_options INTERFACE -mavx2 -mfma)
    endif()
endif()
