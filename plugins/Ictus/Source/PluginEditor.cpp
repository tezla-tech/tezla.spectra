// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <cmath>

#include <tezla/ui/PanelDesign.hpp>

namespace tezla::ictus {

namespace
{
using Emphasis = ui::design::Emphasis;

// Ictus's own accent: a hot vermilion -- the beat, the stroke -- against
// Malleus's bone and Anvil's steel. The bypass glow is the same in every
// plugin, though an instrument has nothing to bypass and never shows it.
const ui::Palette kPalette {
    juce::Colour { 0xff141212 },   // background
    juce::Colour { 0xff1c1918 },   // panel
    juce::Colour { 0xffdcd6d0 },   // text
    juce::Colour { 0xff8a827c },   // dim text
    juce::Colour { 0xffe0533c },   // accent: vermilion
    juce::Colour { 0xffff7a5c },   // accent bright
    juce::Colour { 0xff56b8d6 },   // secondary: a cool blue for readings that are not a level
    juce::Colour { 0xffff7a18 }    // bypass glow, the same in every plugin
};

constexpr int kStripHeight = 46;

/// The plates' tints, by group family: the page accent, then 18 degrees a
/// step round the wheel (PanelDesign.hpp, "A hue per group").
constexpr int kTintPitch = 0;      // pitch / shell
constexpr int kTintColour = 1;     // colour / wires
constexpr int kTintClick = 2;      // click / strike
constexpr int kTintAmplitude = 3;  // amplitude, gate, level
constexpr int kTintVelocity = 4;

juce::String noteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note) + ")";
}
} // namespace

// ---------------------------------------------------------------------------
// IctusEditor
// ---------------------------------------------------------------------------

IctusEditor::IctusEditor (IctusProcessor& owner)
    : juce::AudioProcessorEditor (&owner),
      ictus_ (owner),
      palette_ (kPalette)
{
    setLookAndFeel (&knobLook_);

    // No bypass parameter: an instrument that is bypassed is an instrument
    // that is silent, which is what muting the track already does.
    header_ = std::make_unique<ui::HeaderBar> (
        ictus_.getState(), "ICTUS", "Drum synthesiser for drum and bass", nullptr, palette_);

    header_->onSwapRequested = [this]
    {
        ictus_.getAbCompare().swapSlots();
        header_->setActiveSlot (ictus_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (ictus_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        ictus_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (ictus_.getAbCompare().otherSlotFilled());
    };

    header_->onTooltipsToggled = [this] (bool enabled)
    {
        ictus_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setActiveSlot (ictus_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (ictus_.getAbCompare().otherSlotFilled());
    header_->setTooltipsEnabled (ictus_.getTooltipsEnabled());
    header_->attachSuiteControls (ictus_.getState(), nullptr, ids::output, ids::oversampling,
                                  ids::renderOversampling);
    addAndMakeVisible (*header_);

    // ---- the strip ----
    hitButton_.setComponentID ("hit");
    hitButton_.setColour (juce::TextButton::buttonColourId, palette_.accent.withAlpha (0.35f));
    hitButton_.setColour (juce::TextButton::textColourOffId, palette_.text);
    hitButton_.onClick = [this] { ictus_.triggerHit (currentPad_); };
    addAndMakeVisible (hitButton_);

    padStrip_ = std::make_unique<PadStrip> (ictus_, palette_);
    padStrip_->onPadSelected = [this] (PadIndex pad) { selectPad (pad); };
    addAndMakeVisible (*padStrip_);

    // BASS: the whole keyboard plays Kick 1 at the key's pitch. A global, so
    // it lives in the strip rather than on the kick's page. Its tooltip is
    // live -- which scale, and what C1 plays through it.
    bassButton_.setComponentID ("bass");
    bassButton_.setClickingTogglesState (true);
    addAndMakeVisible (bassButton_);
    bassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        ictus_.getState(), ids::bassMode, bassButton_);

    tuningTab_.setComponentID ("page-tuning");
    tuningTab_.setTooltip ("The tuning that Bass mode, Follow key and Note snap play through: built-in "
                           "scales, Scala .scl and .kbm files, concert pitch. It travels with the "
                           "project as text. Costs nothing: a hit reads one frequency. Click a pad "
                           "to go back to its page.");
    tuningTab_.onClick = [this]
    {
        if (currentPage_ != 3)
            showPage (3);
        else
            selectPad (currentPad_);
    };
    addAndMakeVisible (tuningTab_);

    hitsLabel_.setFont (juce::FontOptions (11.0f));
    hitsLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    hitsLabel_.setJustificationType (juce::Justification::centredRight);
    hitsLabel_.setTooltip ("Hits sounding right now, counted rather than heard: a hit retires the "
                           "sample its envelope lands, and an idle instrument costs nothing "
                           "(measured 0.001% of a core).");
    addAndMakeVisible (hitsLabel_);

    buildKickPage();

    snarePage_ = std::make_unique<PlatePage> (ictus_.getState(), palette_);
    snareViews_ = buildSnarePage (*snarePage_, kSnare1Ids, PadIndex::snare1);

    ghostPage_ = std::make_unique<PlatePage> (ictus_.getState(), palette_);
    ghostViews_ = buildSnarePage (*ghostPage_, kGhostIds, PadIndex::snare2);

    buildTuningPage();
    showPage (0);

    tooltips_.setEnabled (ictus_.getTooltipsEnabled());
    refreshHeaderTooltips();
    refreshKeyTooltips();
    updateGreying();
    refreshDisplays();

    setResizable (true, true);
    setResizeLimits (880, 620, 1600, 1100);
    setSize (1000, 720);

    startTimerHz (15);
}

IctusEditor::~IctusEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
// The kick page
// ---------------------------------------------------------------------------

void IctusEditor::buildKickPage()
{
    auto& state = ictus_.getState();

    kickPage_ = std::make_unique<PlatePage> (state, palette_);
    auto& page = *kickPage_;

    // ---- pitch ----
    page.beginPlate ("Pitch", "where the kick lands, and how it gets there", kTintPitch);

    page.addKnob (ids::k1Tune, "Tune",
        "The pitch the kick LANDS on, 20 to 400 Hz. Everything about the sweep is "
        "relative to this: Start sits above it, Sigh drifts around it. 50 Hz is a "
        "sub kick; 60 to 70 sits up where a break lives. With Follow key lit the "
        "MIDI note sets this instead; with Note lit it snaps to the tuning's "
        "nearest note and reads as that note.", Emphasis::lead);

    // Both lamps' texts are replaced with live ones (which scale, what the
    // pad's note plays through it, what Tune snaps to) the moment the editor
    // is up.
    page.addLamp (ids::k1FollowKey, "Follow key", "Key",
        "Lit: the landed pitch comes from the MIDI note through the TUNING page's "
        "scale. Dark: Tune sets it.");

    page.addLamp (ids::k1NoteSnap, "Note snap", "Note",
        "Lit: Tune snaps to the nearest note of the TUNING page's scale, so the "
        "kick sits in the key of the bass line. Dark: Tune is free.");

    page.addKnob (ids::k1Start, "Start",
        "How far above the landed pitch the hit starts, 0 to 60 semitones. This is "
        "the punch: the body begins high and drops. 0 is a pure sine with no "
        "sweep at all, bit-exactly. Velocity scales it by the amount on Vel > Drop.");

    page.addKnob (ids::k1Drop, "Drop",
        "How long the drop takes to land, 2 to 200 ms -- the stated time is where "
        "less than 1% of the start height remains. 10 to 30 ms is a tight, "
        "clicky kick; 60 ms and up is a laser. The pitch is exact to 0.016 cents "
        "against the ideal curve at every session rate.");

    page.addKnob (ids::k1Sigh, "Sigh",
        "A second, slower drop: signed semitones the pitch drifts through the "
        "decay, -12 to +12. Positive starts sharp and settles, the way a real kick "
        "shell relaxes (about 1.5 st for a drum, more for an 808); negative rises. "
        "0 is exact.");

    page.addKnob (ids::k1SighTime, "Sigh time",
        "How long the sigh takes to land, 100 ms to 2 s. Long times against a "
        "short Decay are inaudible as pitch and heard as weight; short times "
        "against a long tail are the classic bend.", Emphasis::trim);

    pitchView_ = static_cast<PitchView*> (
        page.addDisplay (std::make_unique<PitchView> (ictus_, palette_, page.tintOf (kTintPitch)), 3));

    // ---- colour ----
    page.beginPlate ("Colour", "harmonics and the tracking tone", kTintColour);

    page.addKnob (ids::k1Harmonics, "Harmonics",
        "Adds harmonics to the body in PARALLEL -- the small-speaker control. "
        "Both curves have no linear term, so a quiet body gains nothing but "
        "overtones and the fundamental is never turned up in the process. 0 is "
        "bit-exact off. Runs inside the oversampled section; measured -155 dB of "
        "aliasing at full.", Emphasis::lead);

    page.addKnob (ids::k1Even, "Even",
        "Which harmonics: 0 is the odd curve only (3rd, 5th -- hollow, hard), 100 "
        "the even curve only (2nd, 4th -- warm, thick), and in between a blend. "
        "The even curve leaves a DC pedestal, so a 10 Hz blocker follows it "
        "whenever this is above 0: -20 dB of bump re peak, and 0.17 dB off a "
        "50 Hz body.");

    page.addLamp (ids::k1ToneOn, "Tone on", "Tone",
        "Lit: a low-pass filter tracks the body's pitch so the attack is bright "
        "and the landed tail pure. Dark: no filter in the path at all -- exact, "
        "and decided per hit so switching it never clicks.");

    page.addKnob (ids::k1Tone, "Tone",
        "The tracking low-pass's cutoff as a multiple of the CURRENT pitch, 1 to "
        "64. At 8, a body dropping from 400 Hz to 50 Hz opens to 3.2 kHz and "
        "settles at 400 Hz. Low values dull the harmonics as the drop lands; high "
        "ones leave them. Retuned every 32 internal samples.");

    // ---- click ----
    page.beginPlate ("Click", "the beater", kTintClick, true);

    page.addKnob (ids::k1Click, "Click",
        "The beater: one resonant mode struck with the hit, ringing 3 ms. This is "
        "what velocity is most about (Vel > Click), and what makes a kick read on "
        "a small speaker before the body arrives. 0 is exact off.", Emphasis::lead);

    page.addKnob (ids::k1ClickTone, "Click tone",
        "The click's pitch, 200 Hz to 8 kHz, and the corner of the noise burst's "
        "high-pass. 2 to 4 kHz is a beater on a head; above 6 kHz it is a tick.");

    page.addKnob (ids::k1Noise, "Noise",
        "A burst of high-passed noise with the hit -- the 909-style attack knob. "
        "Every hit uses its own random stream, seeded from the hit count, so a "
        "roll never repeats a burst and a render is still reproducible.");

    page.addKnob (ids::k1NoiseTime, "Noise time",
        "How long the noise burst takes to fall 60 dB, 0.5 to 8 ms. Below 2 ms it "
        "is part of the click; above 5 ms it starts to sound like a snare's wires.",
        Emphasis::trim);

    // ---- amplitude ----
    page.beginPlate ("Amplitude", "the hit's shape", kTintAmplitude);

    page.addKnob (ids::k1Phase, "Phase",
        "Where the body starts its cycle, 0 to 90 degrees. 0 starts from silence "
        "and rises -- the softest onset. 90 starts at the peak: a full-scale step "
        "on the first sample, which is a click by construction and the punchiest "
        "attack there is. Costs nothing.", Emphasis::trim);

    page.addKnob (ids::k1Attack, "Attack",
        "The body's rise, 0 to 20 ms. 0 is instant -- the drum this is meant to "
        "be. A few milliseconds softens the click of a Phase setting above 0.",
        Emphasis::trim);

    page.addKnob (ids::k1Hold, "Hold",
        "Full level for this long before the decay begins, 0 to 50 ms. A little "
        "hold keeps the drop at full level while it lands.", Emphasis::trim);

    page.addKnob (ids::k1Decay, "Decay",
        "How long the body takes to fall to silence, 20 ms to 2 s. The hit "
        "retires EXACTLY when it lands -- no tail below hearing, no CPU after. "
        "Velocity can shorten it (Vel > Decay).", Emphasis::lead);

    page.addKnob (ids::k1Shape, "Shape",
        "The decay's curve: 0 is exponential, the natural ring-down, fast then "
        "slow; 100 is a straight line, which holds the body up longer and then "
        "stops. In between bends between them.");

    page.addKnob (ids::k1Tail, "Tail",
        "Mixes in a second, longer envelope on the SAME body, so the landed pitch "
        "keeps ringing after the punch has gone. Its attack is the Drop time, so "
        "it carries only the landed pitch and never the sweep. 0 is exact off.");

    page.addKnob (ids::k1TailTime, "Tail time",
        "How long the tail rings, 100 ms to 4 s. Long tails on a 40 Hz body are "
        "the sub that fills a bar; keep an eye on how it meets the next hit -- a "
        "retrigger crossfades the old one out over 1 ms.", Emphasis::trim);

    kickEnvelope_ = static_cast<EnvelopeView*> (
        page.addDisplay (std::make_unique<EnvelopeView> (ictus_, palette_, page.tintOf (kTintAmplitude),
                                                         EnvelopeView::Drum::kick), 3));

    // ---- out ----
    page.beginPlate ("Out", "", kTintAmplitude);

    page.addKnob (ids::k1Level, "Level",
        "The kick's own level before the output trim, 0 to 100%. At 100 a Phase "
        "90 hit peaks at full scale; the defaults sit at 80 so the click and "
        "harmonics have room.", Emphasis::lead);

    page.addLamp (ids::k1Gate, "Gate", "Gate",
        "Lit: a note-off RELEASES the hit from wherever its envelope is, over "
        "Release -- so a fast fill does not pile each hit's tail onto the next, "
        "and in Bass mode a note ends when the key lifts. Hold and Decay still "
        "shape the hit. Dark: a one-shot that plays its whole length whatever "
        "the key does. The HIT button always plays the whole hit.");

    page.addKnob (ids::k1Release, "Release",
        "How long a gated hit takes to fall silent after the key lifts, 0 to 2 s. "
        "0 is a 1 ms cut, the shortest that does not click (measured: the largest "
        "output step equals the body's own, 0.013). 20 to 60 ms is a natural stop; "
        "long values are a second decay that starts when the key lifts. Does "
        "nothing with Gate dark, and nothing at all once the hit has landed.");

    // ---- velocity ----
    page.beginPlate ("Velocity", "what a harder hit does", kTintVelocity, true);

    page.addKnob (ids::k1VelLevel, "Vel > Level",
        "How much velocity moves the level, 0 to 100%. At 100 the level IS the "
        "velocity; at 0 every hit is as loud as the last. Programmed DnB usually "
        "wants this low and Vel > Click high.", Emphasis::trim);

    page.addKnob (ids::k1VelClick, "Vel > Click",
        "How much velocity moves the click and the noise, 0 to 100%: harder hits "
        "get more beater. The body is untouched, which is how a real drum works.",
        Emphasis::trim);

    page.addKnob (ids::k1VelDrop, "Vel > Drop",
        "How much velocity moves the Start height, 0 to 100%: harder hits start "
        "higher and drop further -- more punch at the top of the pattern, less "
        "on the ghosts.", Emphasis::trim);

    page.addKnob (ids::k1VelDecay, "Vel > Decay",
        "How much velocity moves the decay, 0 to 100%: at 100 a half-velocity "
        "hit decays in half the time. 0 keeps every hit the same length.",
        Emphasis::trim);

    page.setNote ("Every knob is read when the hit starts and held for its whole length: a knob "
                  "moved mid-hit changes the next one, never the one sounding. Measured: the "
                  "pitch within 0.016 cents of the ideal curve at 44.1, 48, 96 and 192 kHz; "
                  "two kicks with everything on at 4 to 7% of a core; the plain body bit-exact.");

    // The Tune readout: the note it snaps to, once it does.
    page.setValueText (ids::k1Tune, [this] (double hz)
    {
        if (ictus_.getState().getRawParameterValue (ids::k1NoteSnap)->load() > 0.5f)
            return ictus_.noteNameFor (ictus_.previewSnappedHz (hz)).upToFirstOccurrenceOf (" +0c", false, false);

        return juce::String (hz, 1);
    });

    addChildComponent (page);
}

// ---------------------------------------------------------------------------
// The snare page
// ---------------------------------------------------------------------------

IctusEditor::SnareViews IctusEditor::buildSnarePage (PlatePage& page, const SnareIds& ids, PadIndex pad)
{
    const bool ghost = ids.link != nullptr;
    const juce::String drum = ghost ? "the ghost" : "the snare";
    SnareViews views;

    // ---- shell ----
    page.beginPlate ("Shell", ghost ? "the main snare's drum while LINK is lit, else its own"
                                    : "three modes at the published ratios", kTintPitch);

    if (ghost)
        page.addLamp (ids.link, "Link", "Link",
            "Lit: the ghost is SNARE's drum -- Tune, Key, Note, Spread, Tone, Snappy and "
            "Shape follow the main snare and are greyed here -- and only the stroke is its "
            "own: Decay, Start, Drop, Body, Wires, Wires decay, Rattle, the crack, Level, "
            "Gate and velocity. That is what a ghost note is: the same drum, hit lighter "
            "and shorter between the backbeats. Dark: a fully separate second snare.");

    page.addKnob (ids.tune, "Tune",
        "The shell's fundamental, 60 to 800 Hz. A snare sits near 180 to 240; a "
        "tom lower. With Follow key lit the MIDI note sets it; with Note lit it "
        "snaps to the tuning's nearest note and reads as that note. In Bass mode "
        "this pad is silent -- every key plays the kick.", Emphasis::lead);

    page.addLamp (ids.followKey, "Follow key", "Key",
        "Lit: the fundamental comes from the MIDI note through the TUNING page's "
        "scale. Dark: Tune sets it.");

    page.addLamp (ids.noteSnap, "Note snap", "Note",
        "Lit: Tune snaps to the nearest note of the TUNING page's scale, so " + drum
        + " sits in the key of the bass line. Dark: Tune is free.");

    page.addKnob (ids.spread, "Spread",
        "How far the two upper modes sit above the fundamental: 0 puts all three "
        "on one pitch (a tom), 100 is the snare's set at 1.6 and 2.2 times it -- "
        "the fast-decaying pair a real shell has. Measured 1.601 and 2.201. "
        "Costs nothing.");

    page.addKnob (ids.tone, "Tone",
        "How hard the upper two modes are struck, 0 to 100%. 0 strikes the "
        "fundamental alone and runs one mode instead of three.");

    page.addKnob (ids.decay, "Decay",
        "The fundamental's ring-down to -60 dB, 50 ms to 2 s; the upper modes die "
        "at 0.7 and 0.5 times this, as they do on the drum. The shell is cut "
        "exactly once it is 120 dB down, so a 250 ms snare is gone by 500 ms and "
        "costs nothing after." + juce::String (ghost ? " A ghost is usually shorter than the main hit." : ""),
        Emphasis::lead);

    page.addKnob (ids.body, "Body",
        "The shell's level, 0 to 100%, before Level. Body and Wires are a plain "
        "sum: with Rattle at 0 there is nothing between them, bit for bit."
        + juce::String (ghost ? " A ghost is mostly wire, so this sits low." : ""));

    views.modes = static_cast<ModesView*> (
        page.addDisplay (std::make_unique<ModesView> (ictus_, palette_, page.tintOf (kTintPitch), ids, pad), 3));

    // ---- wires ----
    page.beginPlate ("Wires", "noise that the shell can throw", kTintColour);

    page.addKnob (ids.wires, "Wires",
        "The wires' level, 0 to 100%: seeded white noise through the Snappy "
        "filter under its own decay, a new stream every hit. 0 runs no noise at "
        "all. Velocity moves this and Snappy together by Vel > Wires.", Emphasis::lead);

    page.addKnob (ids.snappy, "Snappy",
        "The wires' filter corner, 1 to 8 kHz. High-passed, everything above it: "
        "2 kHz is a fat, papery snare, 6 kHz a tight hiss (measured centroids 7.8 "
        "to 8.9 kHz). Band-passed, the buzz sits at it (2.6 to 6.7 kHz).");

    page.addKnob (ids.snap, "Shape",
        "The wires' filter shape: 0 is a high-pass above Snappy -- open hiss; "
        "100 a band-pass at it -- a pitched, focused buzz. A crossfade that is "
        "exact at both ends.");

    page.addKnob (ids.wiresDecay, "Wires decay",
        "How long the stick's burst on the wires takes to land, 50 to 400 ms. With "
        "Rattle up the wires also follow the shell, past this.");

    page.addKnob (ids.rattle, "Rattle",
        "How much the shell's own motion drives the wires, 0 to 100%: the one "
        "nonlinearity kept from the physical models, so the wires buzz for as "
        "long as the drum rings. At 100 they start 1.8 times louder and are still "
        "there at 100 ms where the plain burst has ended, 29 dB down with the "
        "shell. 0 runs no follower and is exact.");

    views.wires = static_cast<WiresView*> (
        page.addDisplay (std::make_unique<WiresView> (ictus_, palette_, page.tintOf (kTintColour), ids), 3));

    // ---- strike ----
    page.beginPlate ("Strike", "the drop and the stick", kTintClick);

    page.addKnob (ids.start, "Start",
        "The drop: the shell starts this far above its pitch, 0 to 24 semitones, "
        "and glides down over Drop. 4 to 8 is a snare's crack; more is a tom's "
        "bend. Velocity scales it by Vel > Drop. 0 is exact.");

    page.addKnob (ids.drop, "Drop",
        "How long the drop takes to land, 2 to 200 ms. The three modes are retuned "
        "once every 32 internal samples while it moves, with their ring intact, "
        "and never once it has landed (measured: 704 retunes for a 50 ms drop, "
        "then 0).");

    page.addKnob (ids.crack, "Crack",
        "The stick's contact: one resonant mode struck with the hit, ringing "
        "3 ms -- the kick's click, on the snare. This is what velocity is most "
        "about (Vel > Crack). 0 is exact off.", Emphasis::lead);

    page.addKnob (ids.crackTone, "Crack tone",
        "The crack's pitch, 200 Hz to 8 kHz, and the corner of the noise burst's "
        "high-pass. 3 to 5 kHz is a stick on a head.");

    page.addKnob (ids.noise, "Noise",
        "A burst of high-passed noise with the hit, seeded per hit -- the "
        "contact's own spit, shorter than the wires.");

    page.addKnob (ids.noiseTime, "Noise time",
        "How long the burst takes to fall 60 dB, 0.5 to 8 ms.", Emphasis::trim);

    // ---- out ----
    page.beginPlate ("Out", "the hit's shape", kTintAmplitude);

    page.addKnob (ids.level, "Level",
        "The drum's own level before the output trim, 0 to 100%."
        + juce::String (ghost ? " A ghost sits well under the main hit -- that is the shuffle." : ""),
        Emphasis::lead);

    page.addLamp (ids.gate, "Gate", "Gate",
        "Lit: a note-off fades the WHOLE hit out over Release -- shell, wires and "
        "crack -- from wherever it is; a snare has no envelope of its own, its "
        "shell simply rings down, so this is the only way a long tom stops early. "
        "Dark: a one-shot that ignores note-off. The HIT button always plays the "
        "whole hit.");

    page.addKnob (ids.release, "Release",
        "How long a gated hit takes to fade after the key lifts, 0 to 2 s. 0 is a "
        "1 ms ramp, the shortest that does not click (measured: no step larger "
        "than the strike's own). Does nothing with Gate dark.");

    views.envelope = static_cast<EnvelopeView*> (
        page.addDisplay (std::make_unique<EnvelopeView> (ictus_, palette_, page.tintOf (kTintAmplitude),
                                                         EnvelopeView::Drum::snare, ids), 3));

    // ---- velocity ----
    page.beginPlate ("Velocity", "what a harder hit does", kTintVelocity, true);

    page.addKnob (ids.velLevel, "Vel > Level",
        "How much velocity moves the level, 0 to 100%.", Emphasis::trim);

    page.addKnob (ids.velWires, "Vel > Wires",
        "How much velocity moves the wires' level AND their Snappy corner, 0 to "
        "100%: a soft hit is quieter and duller, the way the article has it.",
        Emphasis::trim);

    page.addKnob (ids.velCrack, "Vel > Crack",
        "How much velocity moves the crack and its noise, 0 to 100%: harder hits "
        "get more stick.", Emphasis::trim);

    page.addKnob (ids.velDrop, "Vel > Drop",
        "How much velocity moves the Start height, 0 to 100%: harder hits start "
        "higher and drop further.", Emphasis::trim);

    page.setNote (ghost
        ? "The ghost snare: the quiet, off-beat hits between the backbeats that give a break "
          "its shuffle. Its own key -- E1 by default -- so a pattern can place it on the "
          "16ths around the main hit. With LINK lit it is the main snare's drum played "
          "lighter and shorter; dark, it is any second snare you like."
        : "Every knob is read at the strike and held for the whole hit. Measured: the modes "
          "at 1 : 1.601 : 2.201 against the article's 1.6 and 2.2; a Spread 0 shell an exact "
          "200.000 Hz at 44.1, 48, 96 and 192 kHz; the landed drum retuned 0 times a second; "
          "15 ns a sample with everything on at 192 kHz. The Perc pad (D#1) is this engine "
          "as a tom on its defaults until its own page arrives.");

    const char* tuneId = ids.tune;
    const char* snapId = ids.noteSnap;

    page.setValueText (tuneId, [this, snapId] (double hz)
    {
        if (ictus_.getState().getRawParameterValue (snapId)->load() > 0.5f)
            return ictus_.noteNameFor (ictus_.previewSnappedHz (hz)).upToFirstOccurrenceOf (" +0c", false, false);

        return juce::String (hz, 1);
    });

    addChildComponent (page);

    return views;
}

void IctusEditor::buildTuningPage()
{
    // The shared microtuning panel: the processor is its TuningHost, exactly
    // as it is for Malleus, Sonitus and Svarayantra. Only the explanation is
    // this instrument's.
    tuningPage_ = std::make_unique<ui::TuningPanel> (ictus_, palette_,
        "Bass mode plays Kick 1 on every key through this tuning; Follow key reads it, and "
        "Note snap lands a drum's Tune on its nearest note. A kick is a sine that lands on a "
        "pitch, so a just fifth against the bass locks where a tempered one beats -- the sub "
        "either sits or it churns. The scale travels with the project as .scl text. Tune, "
        "Start and Sigh stay in Hz and semitones: they are the shape of the hit, not the scale.");

    addChildComponent (*tuningPage_);
}

// ---------------------------------------------------------------------------
// Pages and the strip
// ---------------------------------------------------------------------------

void IctusEditor::styleTab (juce::TextButton& tab, bool active)
{
    tab.setColour (juce::TextButton::buttonColourId,
                   active ? palette_.accent : palette_.panel.brighter (0.18f));
    tab.setColour (juce::TextButton::textColourOffId,
                   active ? palette_.background : palette_.accent);
}

void IctusEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, 3, index);

    kickPage_->setVisible (currentPage_ == 0);
    snarePage_->setVisible (currentPage_ == 1);
    ghostPage_->setVisible (currentPage_ == 2);
    tuningPage_->setVisible (currentPage_ == 3);

    if (currentPage_ == 0)
        currentPad_ = PadIndex::kick1;
    else if (currentPage_ == 1)
        currentPad_ = PadIndex::snare1;
    else if (currentPage_ == 2)
        currentPad_ = PadIndex::snare2;

    styleTab (tuningTab_, currentPage_ == 3);
    padStrip_->setSelected (currentPad_);
    refreshPadStrip();

    if (currentPage_ == 3)
        tuningPage_->refresh();

    resized();
}

void IctusEditor::selectPad (PadIndex pad)
{
    if (pad == PadIndex::kick1)
        showPage (0);
    else if (pad == PadIndex::snare1)
        showPage (1);
    else if (pad == PadIndex::snare2)
        showPage (2);
}

void IctusEditor::refreshPadStrip()
{
    const bool kick = currentPad_ == PadIndex::kick1;
    const bool ghost = currentPad_ == PadIndex::snare2;

    hitButton_.setTooltip (juce::String ("Strikes ") + (kick ? "the kick" : ghost ? "the ghost snare" : "the snare") + " -- "
                           + noteName (ictus_.getPadNote (currentPad_))
                           + " -- at full velocity, at the top of the next audio block, so a patch can "
                             "be auditioned without a keyboard. A hit while one is still sounding "
                             "crossfades over 1 ms rather than cutting it. Click a pad to change "
                             "which drum this strikes.");
}

void IctusEditor::refreshHeaderTooltips()
{
    header_->setOversamplingTooltip (ictus_.describeOversampling());
    header_->setRenderTooltip (ictus_.describeRenderQuality());
}

void IctusEditor::refreshKeyTooltips()
{
    bassButton_.setTooltip (ictus_.describeKeying());
    kickPage_->setTooltip (ids::k1FollowKey, ictus_.describeFollowKey (PadIndex::kick1));
    kickPage_->setTooltip (ids::k1NoteSnap, ictus_.describeNoteSnap (PadIndex::kick1));
    kickPage_->refreshValueText (ids::k1Tune);

    snarePage_->setTooltip (ids::s1FollowKey, ictus_.describeFollowKey (PadIndex::snare1));
    snarePage_->setTooltip (ids::s1NoteSnap, ictus_.describeNoteSnap (PadIndex::snare1));
    snarePage_->refreshValueText (ids::s1Tune);

    ghostPage_->setTooltip (ids::g1FollowKey, ictus_.describeFollowKey (PadIndex::snare2));
    ghostPage_->setTooltip (ids::g1NoteSnap, ictus_.describeNoteSnap (PadIndex::snare2));
    ghostPage_->refreshValueText (ids::g1Tune);
}

void IctusEditor::refreshDisplays()
{
    for (auto* display : { static_cast<DrumDisplay*> (pitchView_), static_cast<DrumDisplay*> (kickEnvelope_),
                           static_cast<DrumDisplay*> (snareViews_.modes), static_cast<DrumDisplay*> (snareViews_.wires),
                           static_cast<DrumDisplay*> (snareViews_.envelope),
                           static_cast<DrumDisplay*> (ghostViews_.modes), static_cast<DrumDisplay*> (ghostViews_.wires),
                           static_cast<DrumDisplay*> (ghostViews_.envelope) })
        if (display != nullptr)
            display->refresh();
}

void IctusEditor::updateSnareGreying (PlatePage& page, const SnareIds& ids, SnareShown& shown)
{
    const auto read = [this] (const char* id)
    {
        if (auto* raw = ictus_.getState().getRawParameterValue (id))
            return raw->load();

        return 0.0f;
    };

    SnareShown now;
    now.wires = read (ids.wires) > 0.0f;
    now.crack = read (ids.crack) > 0.0f;
    now.noise = read (ids.noise) > 0.0f;
    now.gate = read (ids.gate) > 0.5f;
    now.linked = ids.link != nullptr && read (ids.link) > 0.5f;
    now.keyed = (now.linked ? read (kSnare1Ids.followKey) : read (ids.followKey)) > 0.5f;

    if (now.wires == shown.wires && now.crack == shown.crack && now.noise == shown.noise
        && now.gate == shown.gate && now.keyed == shown.keyed && now.linked == shown.linked)
        return;

    shown = now;

    // The wires' controls with the wires, the crack's tone with the crack,
    // the noise time with the noise, Release with Gate, Tune with its own
    // Key -- and on the ghost, everything LINK borrows from the main snare.
    page.setControlEnabled (ids.snappy, now.wires && ! now.linked);
    page.setControlEnabled (ids.snap, now.wires && ! now.linked);
    page.setControlEnabled (ids.wiresDecay, now.wires);
    page.setControlEnabled (ids.rattle, now.wires);
    page.setControlEnabled (ids.crackTone, now.crack || now.noise);
    page.setControlEnabled (ids.noiseTime, now.noise);
    page.setControlEnabled (ids.release, now.gate);
    page.setControlEnabled (ids.tune, ! now.keyed && ! now.linked);
    page.setControlEnabled (ids.noteSnap, ! now.keyed && ! now.linked);
    page.setControlEnabled (ids.followKey, ! now.linked);
    page.setControlEnabled (ids.spread, ! now.linked);
    page.setControlEnabled (ids.tone, ! now.linked);
}

void IctusEditor::updateGreying()
{
    const auto read = [this] (const char* id)
    {
        if (auto* raw = ictus_.getState().getRawParameterValue (id))
            return raw->load();

        return 0.0f;
    };

    const bool toneOn = read (ids::k1ToneOn) > 0.5f;
    const bool harmonics = read (ids::k1Harmonics) > 0.0f;
    const bool tail = read (ids::k1Tail) > 0.0f;
    const bool gate = read (ids::k1Gate) > 0.5f;

    // Tune does nothing while the key sets the pitch -- Follow key lit, or
    // Bass mode, which keys every hit.
    const bool keyed = read (ids::k1FollowKey) > 0.5f || read (ids::bassMode) > 0.5f;

    if (toneOn != shownToneOn_ || harmonics != shownHarmonics_ || tail != shownTail_
        || gate != shownGate_ || keyed != shownKeyed_)
    {
        shownToneOn_ = toneOn;
        shownHarmonics_ = harmonics;
        shownTail_ = tail;
        shownGate_ = gate;
        shownKeyed_ = keyed;

        kickPage_->setControlEnabled (ids::k1Tone, toneOn);
        kickPage_->setControlEnabled (ids::k1Even, harmonics);
        kickPage_->setControlEnabled (ids::k1TailTime, tail);
        kickPage_->setControlEnabled (ids::k1Release, gate);
        kickPage_->setControlEnabled (ids::k1Tune, ! keyed);
        kickPage_->setControlEnabled (ids::k1NoteSnap, ! keyed);
    }

    updateSnareGreying (*snarePage_, kSnare1Ids, shownSnare_);
    updateSnareGreying (*ghostPage_, kGhostIds, shownGhost_);
}

void IctusEditor::timerCallback()
{
    const int hits = ictus_.getActiveHitCount();
    hitsLabel_.setText (juce::String (hits) + " sounding", juce::dontSendNotification);

    padStrip_->refresh();

    // The header's live tooltips are rebuilt only when what they describe
    // has moved, not fifteen times a second.
    const auto read = [this] (const char* id)
    {
        if (auto* raw = ictus_.getState().getRawParameterValue (id))
            return static_cast<int> (std::lround (raw->load()));

        return 0;
    };

    const int factor = ictus_.getOversamplingFactor();
    const int render = read (ids::renderOversampling);
    const int oversampling = read (ids::oversampling);
    const int rateHz = static_cast<int> (std::lround (ictus_.getPreparedRate()));
    const bool offline = ictus_.isNonRealtime();

    if (factor != shownFactor_ || render != shownRender_ || oversampling != shownOversampling_
        || rateHz != shownRateHz_ || offline != shownOffline_)
    {
        shownFactor_ = factor;
        shownRender_ = render;
        shownOversampling_ = oversampling;
        shownRateHz_ = rateHz;
        shownOffline_ = offline;
        refreshHeaderTooltips();
    }

    // The key tooltips and the Tune readouts name the scale and what a key
    // or a Tune lands on; the tuning page's table reads the same host. All
    // follow a change made anywhere -- the panel, a preset, a state load.
    const auto readRaw = [this] (const char* id)
    {
        if (auto* raw = ictus_.getState().getRawParameterValue (id))
            return raw->load();

        return 0.0f;
    };

    const juce::String scale = ictus_.getScaleName() + " @ " + juce::String (ictus_.getConcertPitch(), 3);
    const bool bass = read (ids::bassMode) > 0;
    const int padNote = (ictus_.getPadNote (PadIndex::kick1) * 128 + ictus_.getPadNote (PadIndex::snare1)) * 128
                      + ictus_.getPadNote (PadIndex::snare2);
    const juce::int64 snapKey = (static_cast<juce::int64> (std::lround (10.0f * readRaw (ids::k1Tune))) * 100000
                                 + static_cast<juce::int64> (std::lround (10.0f * readRaw (ids::s1Tune)))) * 100000
                              + static_cast<juce::int64> (std::lround (10.0f * readRaw (ids::g1Tune))) * 16
                              + (read (ids::k1NoteSnap) > 0 ? 8 : 0) + (read (ids::s1NoteSnap) > 0 ? 4 : 0)
                              + (read (ids::g1NoteSnap) > 0 ? 2 : 0) + (read (ids::g1Link) > 0 ? 1 : 0);

    if (scale != shownScale_ || bass != shownBass_ || padNote != shownPadNote_ || snapKey != shownSnapKey_)
    {
        const bool scaleMoved = scale != shownScale_;

        shownScale_ = scale;
        shownBass_ = bass;
        shownPadNote_ = padNote;
        shownSnapKey_ = snapKey;
        refreshKeyTooltips();
        refreshPadStrip();

        if (scaleMoved && tuningPage_->isVisible())
            tuningPage_->refresh();
    }

    updateGreying();
    refreshDisplays();
}

void IctusEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

void IctusEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));

    auto strip = bounds.removeFromTop (kStripHeight).reduced (10, 5);
    hitButton_.setBounds (strip.removeFromLeft (64));
    strip.removeFromLeft (8);

    hitsLabel_.setBounds (strip.removeFromRight (84));
    tuningTab_.setBounds (strip.removeFromRight (84).reduced (2, 2));
    strip.removeFromRight (6);
    bassButton_.setBounds (ui::LampButton::sized (64, 26).withCentre (strip.removeFromRight (72).getCentre()));
    strip.removeFromRight (6);

    padStrip_->setBounds (strip);

    bounds.reduce (8, 4);
    kickPage_->setBounds (bounds);
    snarePage_->setBounds (bounds);
    ghostPage_->setBounds (bounds);
    tuningPage_->setBounds (bounds);
}

} // namespace tezla::ictus
