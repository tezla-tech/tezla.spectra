#include "TestFramework.hpp"

#include <cstdio>
#include <string>

#include <tezla/dsp/Version.hpp>

int main (int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : std::string {};

    std::printf ("tezla-dsp %s -- unit tests\n\n", tezla::dsp::kVersionString);

    return tezla::test::runAll (filter);
}
