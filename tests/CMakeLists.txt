# Framework-free DSP tests. No JUCE, no external test library, no network.
# Build and run them anywhere:  cmake --build build --target tezla-tests && ctest

file(GLOB _test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")

add_executable(tezla-tests
    main.cpp
    TestFramework.cpp
    ${_test_sources})

target_include_directories(tezla-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

target_link_libraries(tezla-tests PRIVATE
    tezla::dsp
    tezla::measure
    tezla::compiler-options)

set_target_properties(tezla-tests PROPERTIES FOLDER "Tests")

add_test(NAME tezla-dsp COMMAND tezla-tests)
