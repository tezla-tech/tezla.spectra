// tezla-ui-preview -- renders a UI component offscreen to a PNG.
//
// The plugins are developed in a Linux container with no audio device, so the
// standalone builds only ever see silence and every spectrum drawn in them is a
// flat line at the floor. That is enough to check a layout and useless for
// checking what the picture actually says.
//
// This feeds a component synthetic audio, renders it, and writes a file --
// which is how the difference between "the code compiles" and "the display is
// telling the truth" gets settled here rather than on the user's machine. It is
// the same argument tezla-measure makes about DSP, applied to drawing.
//
//   tezla-ui-preview spectrum out.png

#include <cmath>
#include <numbers>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/dsp/SpectrumAnalyser.hpp>
#include <tezla/ui/SpectrumDisplay.hpp>

namespace
{
constexpr double kSampleRate = 48000.0;

/// A plausible piece of music rather than a test tone: a bass note with a few
/// partials, some mid content, and a little broadband air.
double source (double t)
{
    double value = 0.55 * std::sin (2.0 * std::numbers::pi *  55.0 * t)
                 + 0.30 * std::sin (2.0 * std::numbers::pi * 110.0 * t)
                 + 0.16 * std::sin (2.0 * std::numbers::pi * 220.0 * t)
                 + 0.13 * std::sin (2.0 * std::numbers::pi * 440.0 * t)
                 + 0.09 * std::sin (2.0 * std::numbers::pi * 880.0 * t)
                 + 0.05 * std::sin (2.0 * std::numbers::pi * 1760.0 * t)
                 + 0.02 * std::sin (2.0 * std::numbers::pi * 3520.0 * t);

    return value * 0.7;
}

/// The same, with harmonics added above 3 kHz -- what Halo does, close enough
/// for the display to be checked against something with a known shape.
double excited (double t)
{
    double value = source (t);

    for (const double harmonic : { 5280.0, 7040.0, 8800.0, 10560.0, 12320.0, 14080.0 })
        value += 0.035 * std::sin (2.0 * std::numbers::pi * harmonic * t);

    return value;
}

int renderSpectrum (const juce::File& destination)
{
    tezla::ui::Palette palette;
    palette.accent       = juce::Colour (0xffd9b24a);
    palette.accentBright = juce::Colour (0xfff2d888);
    palette.secondary    = juce::Colour (0xff54c7c0);

    tezla::ui::SpectrumDisplay display { palette };
    display.setSize (820, 260);
    display.prepare (kSampleRate);
    display.setFocusFrequency (3000.0, true);
    display.setHarmonicLimits (true, 220.0, true, 16000.0);

    tezla::dsp::SpectrumCapture input, output;
    input.prepare (1 << 14);
    output.prepare (1 << 14);

    constexpr int blockSize = 512;
    std::vector<double> inputBlock (blockSize), outputBlock (blockSize);
    std::size_t index = 0;

    // Long enough for the falling ballistics to have settled everywhere.
    for (int frame = 0; frame < 120; ++frame)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const double t = static_cast<double> (index) / kSampleRate;
            inputBlock[static_cast<std::size_t> (i)]  = source (t);
            outputBlock[static_cast<std::size_t> (i)] = excited (t);
            ++index;
        }

        input.push (inputBlock.data(), blockSize);
        output.push (outputBlock.data(), blockSize);
        display.update (input, output);
    }

    const auto image = display.createComponentSnapshot (display.getLocalBounds(), false, 2.0f);

    destination.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (destination.createOutputStream());

    if (stream == nullptr)
    {
        std::fprintf (stderr, "could not write %s\n", destination.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, *stream))
    {
        std::fprintf (stderr, "could not encode the image\n");
        return 1;
    }

    std::printf ("wrote %s (%d x %d)\n", destination.getFullPathName().toRawUTF8(),
                 image.getWidth(), image.getHeight());
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: tezla-ui-preview spectrum <out.png>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String what { argv[1] };
    const juce::File destination = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);

    if (what == "spectrum")
        return renderSpectrum (destination);

    std::fprintf (stderr, "unknown preview '%s'\n", argv[1]);
    return 2;
}
