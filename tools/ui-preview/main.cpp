// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

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
//   tezla-ui-preview spectrum   out.png     the display against synthetic audio
//   tezla-ui-preview focus-drag out.png     the same, mid-drag, with the gesture
//                                           driven through the component and the
//                                           frequencies it reported printed
//   tezla-ui-preview goniometer out.png     the stereo shapes that matter, side by
//                                           side: mono, wide, hard-panned, out of
//                                           phase, and a sub that is inverted under
//                                           a top that is not
//   tezla-ui-preview meters     out.png     every state of the level meter side
//                                           by side: silent, quiet, hot, over,
//                                           and referenced to a ceiling. A meter
//                                           in a standalone with no audio device
//                                           reads -inf forever, so this is the
//                                           only way to see the states that
//                                           matter without a DAW.

#include <cmath>
#include <numbers>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/dsp/SpectrumAnalyser.hpp>
#include <tezla/ui/Goniometer.hpp>
#include <tezla/ui/LevelMeter.hpp>
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

/// Renders whatever the display currently holds and writes it out.
/// Photographs any component at 2x, so the result is readable on a high-DPI
/// display rather than a 190-pixel thumbnail.
int writeSnapshot (juce::Component& component, const juce::File& destination)
{
    const auto image = component.createComponentSnapshot (component.getLocalBounds(), false, 2.0f);

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

/// Feeds the display a run of frames so the ballistics settle.
void feed (tezla::ui::SpectrumDisplay& display,
           tezla::dsp::SpectrumCapture& input,
           tezla::dsp::SpectrumCapture& output,
           std::size_t& index,
           int frames)
{
    constexpr int blockSize = 512;
    std::vector<double> inputBlock (blockSize), outputBlock (blockSize);

    for (int frame = 0; frame < frames; ++frame)
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
}

tezla::ui::Palette housePalette()
{
    tezla::ui::Palette palette;
    palette.accent       = juce::Colour (0xffd9b24a);
    palette.accentBright = juce::Colour (0xfff2d888);
    palette.secondary    = juce::Colour (0xff54c7c0);
    return palette;
}

/// A mouse event at a pixel, built the way JUCE builds one, so the component
/// under test sees exactly what it would see from a real pointer.
juce::MouseEvent eventAt (juce::Component& component, int x, int y)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> position { static_cast<float> (x), static_cast<float> (y) };

    return { source, position, juce::ModifierKeys::currentModifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &component, &component, juce::Time::getCurrentTime(), position,
             juce::Time::getCurrentTime(), 1, false };
}

/// Drives a drag across the display and prints what it asked for at each step,
/// then checks the answer round-trips: the frequency reported for a pixel must
/// put the Focus line back on that pixel, or the line and the pointer disagree.
int renderFocusDrag (const juce::File& destination)
{
    auto palette = housePalette();

    tezla::ui::SpectrumDisplay display { palette };
    display.setSize (820, 260);
    display.prepare (kSampleRate);
    display.setFocusFrequency (3000.0, true);
    display.setHarmonicLimits (true, 220.0, true, 16000.0);

    double lastHz = 0.0;
    int began = 0, moved = 0, ended = 0;

    display.onFocusDragged = [&] (double hz, tezla::ui::SpectrumDisplay::DragPhase phase)
    {
        using Phase = tezla::ui::SpectrumDisplay::DragPhase;

        if (phase == Phase::began) ++began;
        else if (phase == Phase::ended) ++ended;
        else { ++moved; lastHz = hz; display.setFocusFrequency (hz, true); }
    };

    tezla::dsp::SpectrumCapture input, output;
    input.prepare (1 << 14);
    output.prepare (1 << 14);
    std::size_t index = 0;
    feed (display, input, output, index, 120);

    display.mouseEnter (eventAt (display, 400, 130));

    int worstPixel = 0;
    std::printf ("  %-6s %10s   %s\n", "x", "reported", "line lands at x");

    display.mouseDown (eventAt (display, 60, 130));

    for (const int x : { 60, 150, 300, 450, 600, 750, 812 })
    {
        display.mouseDrag (eventAt (display, x, 130));

        // Where the display would now draw the line, found by bisecting its own
        // mapping -- the only honest way to ask a private transform a question.
        int landed = x;
        for (int candidate = 0; candidate < display.getWidth(); ++candidate)
        {
            if (display.frequencyAt (candidate) >= lastHz)
            {
                landed = candidate;
                break;
            }
        }

        worstPixel = std::max (worstPixel, std::abs (landed - x));
        std::printf ("  %-6d %8.1f Hz   %d\n", x, lastHz, landed);
    }

    display.mouseUp (eventAt (display, 812, 130));

    std::printf ("\n  gestures: %d began, %d moved, %d ended\n", began, moved, ended);
    std::printf ("  worst pointer-to-line error: %d px\n", worstPixel);

    const bool ok = began == 1 && ended == 1 && moved >= 7 && worstPixel <= 2;
    std::printf ("  %s\n\n", ok ? "PASS" : "FAIL");

    // Redrawn mid-drag, so the readout and the thickened line are in the picture
    // rather than only in the code. Parked at 700 px deliberately: that is where
    // the readout would print straight through the IN/OUT legend if the two were
    // still sharing the top row.
    display.mouseDown (eventAt (display, 700, 130));
    display.mouseDrag (eventAt (display, 700, 130));
    feed (display, input, output, index, 4);

    const int result = writeSnapshot (display, destination);
    return result != 0 ? result : (ok ? 0 : 1);
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

    return writeSnapshot (display, destination);
}

/// The stereo shapes a goniometer exists to tell apart.
///
/// Every one of these is a case where a correlation number alone is either
/// ambiguous or actively misleading, which is the argument for having the
/// picture at all. Fed through the real StereoScope rather than drawn by hand,
/// so the rotation, the scaling and the striding are all being checked too.
int renderGoniometer (const juce::File& destination)
{
    struct Case
    {
        const char* caption;
        /// Fills one second of stereo audio.
        void (*fill) (std::vector<double>&, std::vector<double>&);
    };

    const auto tone = [] (std::size_t i, double hz)
    {
        return std::sin (2.0 * std::numbers::pi * hz * static_cast<double> (i) / kSampleRate);
    };

    const Case cases[] {
        // r = +1. A vertical line: everything in the middle.
        { "mono", [] (std::vector<double>& l, std::vector<double>& r)
          {
              for (std::size_t i = 0; i < l.size(); ++i)
              {
                  const double v = 0.7 * std::sin (2.0 * std::numbers::pi * 220.0
                                                   * static_cast<double> (i) / kSampleRate);
                  l[i] = v;
                  r[i] = v;
              }
          } },

        // A wide mix: correlated centre with decorrelated sides. Reads about
        // +0.6 and looks like a full ellipse.
        { "wide", [] (std::vector<double>& l, std::vector<double>& r)
          {
              for (std::size_t i = 0; i < l.size(); ++i)
              {
                  const auto t = static_cast<double> (i) / kSampleRate;
                  const double mid  = 0.55 * std::sin (2.0 * std::numbers::pi * 220.0 * t);
                  const double side = 0.35 * std::sin (2.0 * std::numbers::pi * 331.0 * t);
                  l[i] = mid + side;
                  r[i] = mid - side;
              }
          } },

        // Two mono tracks that never met. Also reads about 0, exactly like the
        // wide case, and needs the opposite fix.
        { "hard panned", [] (std::vector<double>& l, std::vector<double>& r)
          {
              for (std::size_t i = 0; i < l.size(); ++i)
              {
                  const auto t = static_cast<double> (i) / kSampleRate;
                  l[i] = 0.75 * std::sin (2.0 * std::numbers::pi * 220.0 * t);
                  r[i] = 0.75 * std::sin (2.0 * std::numbers::pi * 277.0 * t);
              }
          } },

        // r = -1. Horizontal, and silent the moment anything sums to mono.
        { "polarity inverted", [] (std::vector<double>& l, std::vector<double>& r)
          {
              for (std::size_t i = 0; i < l.size(); ++i)
              {
                  const double v = 0.7 * std::sin (2.0 * std::numbers::pi * 220.0
                                                   * static_cast<double> (i) / kSampleRate);
                  l[i] =  v;
                  r[i] = -v;
              }
          } },

        // Lopsided: the same content, 6 dB louder on the left. Correlation is
        // still +1 -- it normalises level away by construction -- and only the
        // picture leans.
        { "6 dB left", [] (std::vector<double>& l, std::vector<double>& r)
          {
              for (std::size_t i = 0; i < l.size(); ++i)
              {
                  const double v = 0.7 * std::sin (2.0 * std::numbers::pi * 220.0
                                                   * static_cast<double> (i) / kSampleRate);
                  l[i] = v;
                  r[i] = v * 0.5;
              }
          } },

        // The failure this suite cares most about: a sub that cancels under a
        // top that does not.
        { "sub inverted", [] (std::vector<double>& l, std::vector<double>& r)
          {
              for (std::size_t i = 0; i < l.size(); ++i)
              {
                  const auto t = static_cast<double> (i) / kSampleRate;
                  const double sub = 0.45 * std::sin (2.0 * std::numbers::pi * 50.0 * t);
                  const double top = 0.35 * std::sin (2.0 * std::numbers::pi * 900.0 * t);
                  l[i] =  sub + top;
                  r[i] = -sub + top;
              }
          } },
    };

    (void) tone;

    constexpr int size = 190;
    constexpr int gap = 10;

    const int total = static_cast<int> (std::size (cases));

    struct Holder : juce::Component
    {
        explicit Holder (juce::Colour c) : colour (c) {}
        void paint (juce::Graphics& g) override { g.fillAll (colour); }
        juce::Colour colour;
    };

    tezla::ui::Palette palette;
    palette.accent       = juce::Colour { 0xff5bb98c };   // Transpectus green
    palette.accentBright = juce::Colour { 0xff8fe0b4 };

    Holder holder { palette.background };
    holder.setSize (3 * (size + gap) + gap, 2 * (size + gap) + gap + 34);

    std::vector<std::unique_ptr<tezla::ui::Goniometer>> scopes;

    for (int i = 0; i < total; ++i)
    {
        auto scope = std::make_unique<tezla::dsp::StereoScope>();
        scope->prepare (kSampleRate);

        const auto length = static_cast<std::size_t> (kSampleRate);
        std::vector<double> left (length, 0.0), right (length, 0.0);
        cases[i].fill (left, right);

        // Pushed in host-sized blocks, so the ring wraps the way it does in a
        // plugin rather than in one clean sweep.
        for (std::size_t offset = 0; offset < length; offset += 512)
        {
            const auto span = std::min<std::size_t> (512, length - offset);
            const double* pointers[2] { left.data() + offset, right.data() + offset };
            scope->push (pointers, 2, static_cast<int> (span));
        }

        // And read the correlation the picture is supposed to explain, so the
        // caption can state both.
        tezla::dsp::CorrelationMeter correlation;
        correlation.prepare (kSampleRate, 0.400);

        for (std::size_t i2 = 0; i2 < length; ++i2)
            correlation.process (left[i2], right[i2]);

        auto view = std::make_unique<tezla::ui::Goniometer> (palette);
        view->update (*scope);

        const int column = i % 3;
        const int row = i / 3;
        view->setBounds (gap + column * (size + gap), 26 + row * (size + gap), size, size);

        holder.addAndMakeVisible (*view);
        scopes.push_back (std::move (view));

        std::printf ("  %-20s r = %+.3f\n", cases[i].caption, correlation.getCorrelation());
    }

    return writeSnapshot (holder, destination);
}


/// The excursion hold, demonstrated the way it is used: a wide passage, then
/// a narrow one. The live trail collapses to the vertical mono line while the
/// violet outline keeps the ellipse the image once reached -- through the real
/// scope, the real fold and the real paint, because a ghost drawn by hand
/// would be checking nothing.
int renderImageHold (const juce::File& destination)
{
    tezla::ui::Palette palette;
    palette.accent       = juce::Colour { 0xff5bb98c };
    palette.accentBright = juce::Colour { 0xff8fe0b4 };

    struct Holder : juce::Component
    {
        explicit Holder (juce::Colour c) : colour (c) {}
        void paint (juce::Graphics& g) override { g.fillAll (colour); }
        juce::Colour colour;
    };

    constexpr int size = 260;

    Holder holder { palette.background };
    holder.setSize (size + 20, size + 20);

    tezla::dsp::StereoScope scope;
    scope.prepare (kSampleRate);

    tezla::ui::Goniometer view { palette };

    std::vector<float> hold (static_cast<std::size_t> (tezla::ui::Goniometer::kHoldSectors), 0.0f);
    view.attachExcursionHold (&hold);

    // Phase one: a second of a wide mix. Update per block, the way the editor
    // timer would, so the hold folds as it goes rather than from one frame.
    const auto run = [&] (auto&& fill)
    {
        const auto length = static_cast<std::size_t> (kSampleRate);
        std::vector<double> left (length), right (length);

        for (std::size_t i = 0; i < length; ++i)
            fill (i, left[i], right[i]);

        for (std::size_t offset = 0; offset < length; offset += 512)
        {
            const auto span = std::min<std::size_t> (512, length - offset);
            const double* pointers[2] { left.data() + offset, right.data() + offset };
            scope.push (pointers, 2, static_cast<int> (span));
            view.update (scope);
        }
    };

    run ([] (std::size_t i, double& l, double& r)
    {
        const auto t = static_cast<double> (i) / kSampleRate;
        const double mid  = 0.55 * std::sin (2.0 * std::numbers::pi * 220.0 * t);
        const double side = 0.35 * std::sin (2.0 * std::numbers::pi * 331.0 * t);
        l = mid + side;
        r = mid - side;
    });

    // Phase two: the image narrows to mono. The trail follows; the hold must
    // not.
    run ([] (std::size_t i, double& l, double& r)
    {
        const double v = 0.4 * std::sin (2.0 * std::numbers::pi * 220.0
                                         * static_cast<double> (i) / kSampleRate);
        l = r = v;
    });

    view.setBounds (10, 10, size, size);
    holder.addAndMakeVisible (view);

    float widest = 0.0f;
    for (const float r : hold)
        widest = std::max (widest, r);

    std::printf ("  hold: widest sector %.3f (the wide phase), trail now mono\n", widest);

    return writeSnapshot (holder, destination);
}

/// Every state of the level meter, side by side.
///
/// A standalone build in a container has no audio device, so its meters read
/// -inf forever and the interesting states -- a hot level, a held overshoot,
/// the ceiling line -- are unreachable by running the plugin. Feeding the
/// component directly is the only way to look at them, which is the same
/// argument tezla-measure makes about DSP applied to drawing.
int renderMeters (const juce::File& destination)
{
    struct Case
    {
        const char* caption;
        float vuDb;
        float peakDb;
        float referenceDb;
        bool  scale;
    };

    // The last one is deliberately over its ceiling: that is the state the
    // readout exists for, and the one worth looking at hardest.
    const Case cases[] {
        { "silent",        -100.0f, -100.0f,  0.0f, true  },
        { "quiet",          -28.0f,  -22.4f,  0.0f, false },
        { "working",        -12.0f,   -6.2f,  0.0f, false },
        { "hot",             -4.0f,   -0.4f,  0.0f, false },
        { "over full scale", -2.0f,    2.7f,  0.0f, false },
        { "over ceiling",    -6.0f,   -0.1f, -1.0f, true  },
        { "hovering",        -6.0f,   -0.1f, -1.0f, false },
        { "cleared",         -6.0f,   -0.1f, -1.0f, false },
    };

    // The last two are driven through the component's own event handlers rather
    // than set up by hand, because that is the part worth checking: a hover that
    // never repaints and a click that never clears would both look correct in a
    // static screenshot. Xvfb with no window manager delivers no enter events at
    // all -- a knob's tooltip does not appear either -- so this is the only way
    // to exercise them without a desktop.


    constexpr int width = 66;
    constexpr int height = 320;
    constexpr int gap = 10;

    const int total = static_cast<int> (std::size (cases));
    const int hoveringIndex = total - 2;
    const int clearedIndex  = total - 1;

    // Painted on the background the plugins use, not on nothing. A snapshot of a
    // transparent component comes out white, and every judgement about contrast
    // made against that is a judgement about the wrong picture.
    struct Holder : juce::Component
    {
        explicit Holder (juce::Colour c) : colour (c) {}
        void paint (juce::Graphics& g) override { g.fillAll (colour); }
        juce::Colour colour;
    };

    Holder holder { tezla::ui::Palette{}.background };
    holder.setSize (total * (width + gap) + gap, height + 26);

    std::vector<std::unique_ptr<tezla::ui::LevelMeter>> meters;
    tezla::ui::Palette palette;

    for (int i = 0; i < total; ++i)
    {
        auto meter = std::make_unique<tezla::ui::LevelMeter> (palette);

        meter->setReferenceDb (cases[i].referenceDb);
        meter->setScaleVisible (cases[i].scale);
        meter->setValues (cases[i].vuDb, cases[i].peakDb);
        meter->setBounds (gap + i * (width + gap), 4, width, height);

        holder.addAndMakeVisible (*meter);
        meters.push_back (std::move (meter));
    }

    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();

        const auto eventFor = [&] (juce::Component* c)
        {
            return juce::MouseEvent { source, {}, juce::ModifierKeys{},
                                      juce::MouseInputSource::defaultPressure,
                                      juce::MouseInputSource::defaultOrientation,
                                      juce::MouseInputSource::defaultRotation,
                                      juce::MouseInputSource::defaultTiltX,
                                      juce::MouseInputSource::defaultTiltY,
                                      c, c, now, {}, now, 0, false };
        };

        auto* hovering = meters[static_cast<std::size_t> (hoveringIndex)].get();
        hovering->mouseEnter (eventFor (hovering));

        auto* cleared = meters[static_cast<std::size_t> (clearedIndex)].get();
        cleared->mouseDown (eventFor (cleared));
    }

    const auto image = holder.createComponentSnapshot (holder.getLocalBounds(), false, 2.0f);

    // The captions are drawn onto the snapshot rather than added as components,
    // so the meters are photographed exactly as a plugin would show them.
    juce::Graphics g (const_cast<juce::Image&> (image));
    g.addTransform (juce::AffineTransform::scale (2.0f));
    g.setColour (palette.dimText);
    g.setFont (juce::FontOptions (11.0f));

    for (int i = 0; i < total; ++i)
        g.drawText (cases[i].caption,
                    juce::Rectangle<int> { gap + i * (width + gap), height + 6, width, 16 },
                    juce::Justification::centred);

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

    for (int i = 0; i < total; ++i)
        std::printf ("  %-16s vu %7.1f  peak %7.1f  reference %5.1f  held %7.1f\n",
                     cases[i].caption, cases[i].vuDb, cases[i].peakDb,
                     cases[i].referenceDb, meters[static_cast<std::size_t> (i)]->getHeldDb());

    // The click has to have actually cleared the hold, not merely repainted.
    const float clearedHold = meters[static_cast<std::size_t> (clearedIndex)]->getHeldDb();

    std::printf ("\nclick cleared the hold: %s (held %.1f, was %.1f)\n",
                 clearedHold < -99.0f ? "yes" : "NO",
                 clearedHold, cases[clearedIndex].peakDb);

    return 0;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: tezla-ui-preview <spectrum|focus-drag|meters|goniometer|image-hold> <out.png>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String what { argv[1] };
    const juce::File destination = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);

    if (what == "spectrum")
        return renderSpectrum (destination);

    if (what == "focus-drag")
        return renderFocusDrag (destination);

    if (what == "meters")
        return renderMeters (destination);

    if (what == "image-hold")
        return renderImageHold (destination);

    if (what == "goniometer")
        return renderGoniometer (destination);

    std::fprintf (stderr, "unknown preview '%s'\n", argv[1]);
    return 2;
}
