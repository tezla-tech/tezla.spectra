// tezla-render -- runs a plugin's *JUCE layer* over a test signal and writes
// the samples out.
//
// tezla-measure exercises the engines, which is where the DSP is, and that
// leaves the wrapper untested: parameter plumbing, block handling, bypass
// mixing, and now modulation all live above the engine and none of it is
// reachable from a framework-free test.
//
// That mattered the moment modulation arrived. The claim worth proving is that
// with nothing assigned the plugin is byte-for-byte what it was before the
// feature existed, and the only honest way to check it is to render the same
// audio through both builds and compare. Everything else is an argument.
//
//   tezla-render <samples> <blockSize> <out.raw> [id=value | preset=N ...]
//   tezla-render params
//
// Output is raw little-endian doubles, interleaved stereo, so a diff is a
// byte comparison and needs no parser. Parameters are set by their string ID
// in the plugin's own units -- `focus=8000`, `modDepth1=-0.4` -- so a check
// reads the way the plugin does rather than in normalised fractions.
//
// `preset=N` selects a factory program, in argument order, so
// `preset=11 preset=0` is "load a modulated preset, then load Clean" -- which is
// how the claim that a preset is a *complete* parameter set gets checked rather
// than asserted.
//
// `params` prints the whole parameter list: index, ID, name, default and range.
// CLAUDE.md section 8 says these are frozen forever, and until now the only way
// to check that after a refactor was to read two files side by side. Diffing two
// dumps says it in one line.

#include <cstdio>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
constexpr double kSampleRate = 48000.0;

/// Something with content across the spectrum, so every stage is doing work:
/// a bass note with partials, a mid tone, and a repeating transient for the
/// envelope followers and Punch to bite on.
double source (std::size_t index)
{
    const double t = static_cast<double> (index) / kSampleRate;
    const double twoPi = 2.0 * std::numbers::pi_v<double>;

    double value = 0.45 * std::sin (twoPi *  55.0 * t)
                 + 0.22 * std::sin (twoPi * 110.0 * t)
                 + 0.15 * std::sin (twoPi * 440.0 * t)
                 + 0.08 * std::sin (twoPi * 2600.0 * t);

    // A click every 200 ms.
    if (index % 9600 < 64)
        value += 0.5 * std::exp (-0.05 * static_cast<double> (index % 9600));

    return value * 0.8;
}
} // namespace

/// A mouse event at a pixel, built the way JUCE builds one, so the component
/// under test sees exactly what a real pointer would produce.
///
/// Needed because this container has no window manager, and X11 without one
/// delivers no enter and no motion events at all -- a real pointer parked over
/// a component produces nothing to photograph.
juce::MouseEvent eventAt (juce::Component& component, float x, float y)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> position { x, y };

    return { source, position, juce::ModifierKeys::currentModifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &component, &component, juce::Time::getCurrentTime(), position,
             juce::Time::getCurrentTime(), 1, false };
}

/// Depth-first search for a component by its ID.
juce::Component* findById (juce::Component& parent, const juce::String& id)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* found = findById (*child, id))
            return found;
    }

    return nullptr;
}

/// Creates the editor, optionally clicks a list of controls by component ID,
/// resizes it through its whole range, and destroys it.
///
/// An editor is the part of a plugin that never gets measured, because it has
/// no numbers -- but it has layout arithmetic that divides by its own size, and
/// lifetimes that outlive a click. Both are reachable here with no host and no
/// window manager.
///
/// **What it cannot reach: anything that opens a native window.** This is a
/// console app, and putting a top-level window on the desktop from one fails on
/// X11 before any plugin code is involved -- addToDesktop alone reproduces it
/// with no clicking at all. A control that detaches a panel therefore has to be
/// exercised through the standalone build instead.
int runEditorCheck (juce::AudioProcessor& processor, int argc, char** argv)
{
    if (! processor.hasEditor())
    {
        std::printf ("this plugin has no editor\n");
        return 0;
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditorAndMakeActive() };

    if (editor == nullptr)
    {
        std::fprintf (stderr, "the editor would not instantiate\n");
        return 1;
    }

    std::printf ("editor created: %d x %d%s\n",
                 editor->getWidth(), editor->getHeight(),
                 editor->isResizable() ? ", resizable" : "");


    int failures = 0;

    for (int i = 2; i < argc; ++i)
    {
        juce::String id { argv[i] };

        // "id@x,y" moves the pointer over that component instead of clicking
        // it, for anything that only shows itself under a cursor.
        if (id.contains ("@"))
        {
            const auto target = id.upToFirstOccurrenceOf ("@", false, false);
            const auto where = id.fromFirstOccurrenceOf ("@", false, false);
            const float x = where.upToFirstOccurrenceOf (",", false, false).getFloatValue();
            const float y = where.fromFirstOccurrenceOf (",", false, false).getFloatValue();

            if (auto* found = findById (*editor, target))
            {
                found->mouseMove (eventAt (*found, x, y));
                std::printf ("  pointed at %s (%.0f, %.0f), which is %d x %d\n",
                             target.toRawUTF8(), x, y, found->getWidth(), found->getHeight());
            }
            else
            {
                std::fprintf (stderr, "  no component with id %s\n", target.toRawUTF8());
                ++failures;
            }

            continue;
        }

        // "shot:file.png" photographs the editor as it stands.
        if (id.startsWith ("shot:"))
        {
            const juce::File out = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile (id.fromFirstOccurrenceOf ("shot:", false, false));

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 2.0f);

            out.deleteFile();
            std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());

            if (stream != nullptr)
            {
                juce::PNGImageFormat png;

                if (png.writeImageToStream (image, *stream))
                {
                    std::printf ("  wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(),
                                 image.getWidth(), image.getHeight());
                    continue;
                }
            }

            std::fprintf (stderr, "  could not write %s\n", out.getFullPathName().toRawUTF8());
            ++failures;
            continue;
        }

        if (auto* found = findById (*editor, id))
        {
            if (auto* button = dynamic_cast<juce::Button*> (found))
            {
                // The handler directly rather than triggerClick(), which posts
                // a message: there is no modal dispatch loop in a plugin build
                // to pump it with, and a sequence of clicks has to happen in
                // the order it was written.
                if (button->onClick)
                    button->onClick();

                std::printf ("  clicked %s\n", id.toRawUTF8());
            }
            else
            {
                std::fprintf (stderr, "  %s is not a button\n", id.toRawUTF8());
                ++failures;
            }
        }
        else
        {
            std::fprintf (stderr, "  no component with id %s\n", id.toRawUTF8());
            ++failures;
        }
    }

    // Through the whole resize range, because a layout that divides by a
    // dimension has a smallest size at which it stops being sensible.
    for (const auto& size : { std::pair<int, int> { 760, 520 }, std::pair<int, int> { 1200, 900 },
                              std::pair<int, int> { 1520, 1040 }, std::pair<int, int> { 900, 640 } })
        editor->setSize (size.first, size.second);   // resized() runs synchronously

    std::printf ("  resized through the range\n");

    // The point of the whole exercise: destroy it with whatever the clicks
    // left open.
    processor.editorBeingDeleted (editor.get());
    editor.reset();

    std::printf ("%s\n", failures == 0 ? "editor destroyed cleanly"
                                       : "editor destroyed, but some ids were not found");
    return failures == 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    const bool dumpParameters = argc >= 2 && juce::String (argv[1]) == "params";

    const bool exerciseEditor = argc >= 2 && juce::String (argv[1]) == "editor";

    if (argc < 4 && ! dumpParameters && ! exerciseEditor)
    {
        std::printf ("usage: tezla-render <samples> <blockSize> <out.raw> [id=value ...]\n"
                     "       tezla-render params\n"
                     "       tezla-render editor [componentId | id@x,y | shot:out.png ...]\n");
        return 2;
    }

    // Guarded on both commands, not just one: argv[2] does not exist for either
    // of them, and atoi does not check.
    const bool renderingAudio = ! dumpParameters && ! exerciseEditor;

    const int totalSamples = renderingAudio ? std::atoi (argv[1]) : 0;
    const int blockSize    = renderingAudio ? std::max (1, std::atoi (argv[2])) : 1;

    juce::ScopedJuceInitialiser_GUI juceInit;

    std::unique_ptr<juce::AudioProcessor> processor { createPluginFilter() };

    if (processor == nullptr)
    {
        std::fprintf (stderr, "the plugin would not instantiate\n");
        return 1;
    }

    if (exerciseEditor)
        return runEditorCheck (*processor, argc, argv);

    if (dumpParameters)
    {
        int index = 0;

        for (auto* candidate : processor->getParameters())
        {
            const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (candidate);

            if (ranged == nullptr)
            {
                std::printf ("%3d  <not ranged>  %s\n", index++, candidate->getName (64).toRawUTF8());
                continue;
            }

            const auto& range = ranged->getNormalisableRange();

            // The default in the parameter's own units, not normalised: that is
            // the number a project reopening depends on.
            std::printf ("%3d  %-16s  %-28s  default %-12g range %g .. %g  steps %g\n",
                         index++,
                         ranged->paramID.toRawUTF8(),
                         ranged->getName (64).toRawUTF8(),
                         static_cast<double> (ranged->convertFrom0to1 (ranged->getDefaultValue())),
                         static_cast<double> (range.start),
                         static_cast<double> (range.end),
                         static_cast<double> (range.interval));
        }

        return 0;
    }

    // Parameters before prepareToPlay, so the engine is built for them.
    for (int argument = 4; argument < argc; ++argument)
    {
        const juce::String assignment { argv[argument] };
        const int equals = assignment.indexOfChar ('=');

        if (equals <= 0)
        {
            std::fprintf (stderr, "expected id=value, got '%s'\n", argv[argument]);
            return 2;
        }

        const auto id = assignment.substring (0, equals);
        const float value = assignment.substring (equals + 1).getFloatValue();

        if (id == "preset")
        {
            processor->setCurrentProgram (juce::roundToInt (value));
            std::printf ("  preset %d (%s)\n", juce::roundToInt (value),
                         processor->getProgramName (juce::roundToInt (value)).toRawUTF8());
            continue;
        }

        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
            [&]() -> juce::AudioProcessorParameter*
            {
                for (auto* candidate : processor->getParameters())
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (candidate))
                        if (ranged->paramID == id)
                            return ranged;

                return nullptr;
            }());

        if (parameter == nullptr)
        {
            std::fprintf (stderr, "no parameter called '%s'\n", id.toRawUTF8());
            return 2;
        }

        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        std::printf ("  %s = %g\n", id.toRawUTF8(), static_cast<double> (value));
    }

    processor->setPlayConfigDetails (2, 2, kSampleRate, blockSize);
    processor->prepareToPlay (kSampleRate, blockSize);

    // What the plugin tells the host. Printed rather than left to a DAW,
    // because a wrong number here is invisible until something is out of time
    // -- and a plugin that reports zero while delaying the audio is worse than
    // one that reports too much.
    std::fprintf (stderr, "latency: %d samples (%.3f ms)\n",
                  processor->getLatencySamples(),
                  1000.0 * processor->getLatencySamples() / kSampleRate);

    juce::AudioBuffer<double> buffer (2, blockSize);
    juce::MidiBuffer midi;

    std::vector<double> output;
    output.reserve (static_cast<std::size_t> (totalSamples) * 2);

    std::size_t index = 0;

    for (int written = 0; written < totalSamples; written += blockSize)
    {
        const int span = std::min (blockSize, totalSamples - written);
        buffer.setSize (2, span, false, false, true);

        for (int i = 0; i < span; ++i)
        {
            const double value = source (index++);
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value * 0.85);
        }

        processor->processBlock (buffer, midi);

        for (int i = 0; i < span; ++i)
        {
            output.push_back (buffer.getSample (0, i));
            output.push_back (buffer.getSample (1, i));
        }
    }

    processor->releaseResources();

    std::FILE* file = std::fopen (argv[3], "wb");
    if (file == nullptr)
    {
        std::fprintf (stderr, "could not open %s\n", argv[3]);
        return 1;
    }

    std::fwrite (output.data(), sizeof (double), output.size(), file);
    std::fclose (file);

    std::printf ("wrote %zu samples to %s (block size %d)\n",
                 output.size() / 2, argv[3], blockSize);
    return 0;
}
