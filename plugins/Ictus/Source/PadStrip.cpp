// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PadStrip.h"

namespace tezla::ictus {

namespace
{
/// The strip's order: the pads that have pages first, in the order a break
/// is built -- kick, snare, the ghost -- then the perc and the second kick
/// on their engines' defaults, then the three that arrive with I4.
struct PadEntry
{
    PadIndex pad;
    const char* name;
    bool hasPage;
};

constexpr PadEntry kPads[kPadCount] {
    { PadIndex::kick1,     "KICK",    true },
    { PadIndex::snare1,    "SNARE",   true },
    { PadIndex::snare2,    "GHOST",   true },
    { PadIndex::perc,      "PERC",    false },
    { PadIndex::kick2,     "KICK 2",  false },
    { PadIndex::hatClosed, "HAT C",   false },
    { PadIndex::hatOpen,   "HAT O",   false },
    { PadIndex::clap,      "CLAP",    false },
};

/// The glow left after one 15 Hz tick: a hit is a quarter-second flash.
constexpr float kFade = 0.72f;
} // namespace

// ---------------------------------------------------------------------------
// PadLamp
// ---------------------------------------------------------------------------

PadLamp::PadLamp (PadIndex pad, const juce::String& name, ui::Palette palette, bool hasPage)
    : juce::Button (name), pad_ (pad), name_ (name), palette_ (palette), hasPage_ (hasPage)
{
    setComponentID ("pad-" + name.toLowerCase().replaceCharacter (' ', '-'));
}

void PadLamp::setNote (int note)
{
    // The note's name AND its number: DAWs disagree on which octave is which
    // (FL Studio calls 60 C5, Ableton C3), and the number is what they agree on.
    const auto label = juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " · " + juce::String (note);

    if (label == noteText_)
        return;

    noteText_ = label;
    repaint();
}

void PadLamp::setSelected (bool selected)
{
    if (selected_ == selected)
        return;

    selected_ = selected;
    repaint();
}

void PadLamp::flash (float velocity)
{
    glow_ = juce::jmax (glow_, 0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, velocity));
    repaint();
}

bool PadLamp::fade()
{
    if (glow_ <= 0.0f)
        return false;

    glow_ *= kFade;

    if (glow_ < 0.02f)
        glow_ = 0.0f;

    return true;
}

void PadLamp::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto lit = palette_.accent;

    // The plate, lifting slightly under the mouse for the pads that do
    // something when clicked.
    g.setColour (palette_.panel.brighter (down ? 0.05f : highlighted && hasPage_ ? 0.32f : 0.18f));
    g.fillRoundedRectangle (bounds, 4.0f);

    // The hit: the plate floods with the accent, as hard as the hit was.
    if (glow_ > 0.0f)
    {
        g.setColour (lit.withAlpha (0.75f * glow_));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (lit.withAlpha (0.35f * glow_));
        g.drawRoundedRectangle (bounds.expanded (1.5f), 5.0f, 2.0f);
    }

    if (selected_)
    {
        g.setColour (palette_.accentBright);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.4f);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawLine (bounds.getX() + 4.0f, bounds.getY() + 0.5f, bounds.getRight() - 4.0f, bounds.getY() + 0.5f);
    }

    const auto nameColour = glow_ > 0.5f ? palette_.background
                          : hasPage_ ? palette_.text
                                     : palette_.dimText.withAlpha (0.75f);

    g.setColour (nameColour);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (name_, bounds.withTrimmedBottom (bounds.getHeight() * 0.42f), juce::Justification::centred, false);

    g.setColour (glow_ > 0.5f ? palette_.background.withAlpha (0.8f) : palette_.dimText.withAlpha (hasPage_ ? 0.9f : 0.55f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (noteText_, bounds.withTrimmedTop (bounds.getHeight() * 0.5f), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------
// PadStrip
// ---------------------------------------------------------------------------

PadStrip::PadStrip (IctusProcessor& processor, ui::Palette palette)
    : processor_ (processor)
{
    for (const auto& entry : kPads)
    {
        auto lamp = std::make_unique<PadLamp> (entry.pad, entry.name, palette, entry.hasPage);

        lamp->setNote (processor_.getPadNote (entry.pad));
        lamp->setTooltip (juce::String (entry.name) + " on "
                          + juce::MidiMessage::getMidiNoteName (processor_.getPadNote (entry.pad), true, true, 3)
                          + " (" + juce::String (processor_.getPadNote (entry.pad)) + "). Lights when the pad is "
                            "struck, as bright as the hit was hard"
                          + (entry.hasPage ? juce::String ("; click to open its page.")
                                           : juce::String (". Its page arrives with a later phase; until then it plays "
                                                           "its engine's defaults, or nothing if it has no engine yet.")));

        const PadIndex pad = entry.pad;
        lamp->onClick = [this, pad]
        {
            if (onPadSelected != nullptr)
                onPadSelected (pad);
        };

        addAndMakeVisible (*lamp);
        lamps_.push_back (std::move (lamp));
    }

    for (int pad = 0; pad < kPadCount; ++pad)
        seenHits_[static_cast<std::size_t> (pad)] = processor_.getPadHitCount (static_cast<PadIndex> (pad));
}

void PadStrip::refresh()
{
    for (auto& lamp : lamps_)
    {
        const auto index = static_cast<std::size_t> (lamp->pad());
        const std::uint32_t hits = processor_.getPadHitCount (lamp->pad());

        if (hits != seenHits_[index])
        {
            seenHits_[index] = hits;
            lamp->flash (processor_.getPadLastVelocity (lamp->pad()));
        }
        else if (lamp->fade())
        {
            lamp->repaint();
        }

        lamp->setNote (processor_.getPadNote (lamp->pad()));
    }
}

void PadStrip::setSelected (PadIndex pad)
{
    for (auto& lamp : lamps_)
        lamp->setSelected (lamp->pad() == pad);
}

void PadStrip::resized()
{
    auto bounds = getLocalBounds();
    const int width = bounds.getWidth() / static_cast<int> (lamps_.size());

    for (auto& lamp : lamps_)
        lamp->setBounds (bounds.removeFromLeft (width).reduced (2, 0));
}

} // namespace tezla::ictus
