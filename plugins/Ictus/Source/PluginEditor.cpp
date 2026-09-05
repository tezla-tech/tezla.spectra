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

    mixTab_.setComponentID ("page-mix");
    mixTab_.setTooltip ("Where the eight pads sit in the field: a Pan per pad on a balance law, so "
                        "the centre is the dual mono the instrument always rendered and a pad "
                        "hard left leaves the right channel exactly empty. Width and a mono-below "
                        "corner arrive with the next round, once the pads make a side signal of "
                        "their own. Click a pad to go back to its page.");
    mixTab_.onClick = [this]
    {
        if (currentPage_ != 6)
            showPage (6);
        else
            selectPad (currentPad_);
    };
    addAndMakeVisible (mixTab_);

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

    buildHatPage();
    buildClapPage();
    buildMixPage();
    buildTuningPage();

    // The plate pages live in one viewport: a page taller than the window
    // scrolls rather than losing its last rows off the bottom.
    pageView_.setScrollBarsShown (true, false);
    pageView_.setScrollBarThickness (14);
    addAndMakeVisible (pageView_);

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

    page.addKnob (ids::k1DropCurve, "Drop curve",
        "The drop's shape. 0 is the exponential as it always was, exactly: fast "
        "at first, then easing in over the stated time. Towards -100 it "
        "straightens into a LINE that lands at the stated time -- the laser, "
        "falling at one rate from the first sample to the last (measured: half "
        "the height left at half the time, against 8 % for the curve). Towards "
        "+100 it holds near the start and then snaps down. Costs nothing.",
        Emphasis::trim);

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

    // ---- under ----
    page.beginPlate ("Under", "a clean sub beneath the punch", kTintPitch);

    page.addKnob (ids::k1Under, "Under",
        "A second sine an interval below the landed pitch -- an octave by "
        "default -- locked to the body's own phase increment, so it follows the "
        "drop and the sigh at a fixed ratio and can never beat against the body. "
        "It joins AFTER Harmonics and Tone: a clean sub under a dirty punch, the "
        "layering trick built in. At 100 it is as loud as the body. 0 is exact "
        "off and costs nothing.", Emphasis::lead);

    page.addKnob (ids::k1UnderInterval, "Interval",
        "How far below the landed pitch the sub sits, 0 to 24 semitones. 12 is "
        "the octave -- a 50 Hz kick puts it at 25 Hz, which is felt rather than "
        "heard and wants headroom; 7 is a fifth below, which reads as a note; 0 "
        "doubles the body.");

    page.addKnob (ids::k1UnderDecay, "Under decay",
        "The sub's decay as a percentage of Decay, 25 to 400%. Over 100 the sub "
        "outlives the punch and fills the bar behind it, the way an 808 under a "
        "short kick does; under 100 it is weight in the attack only.");

    page.addKnob (ids::k1UnderAttack, "Under attack",
        "The sub's own rise, 0 to 200 ms. A few tens of milliseconds lets the "
        "punch land clean and the sub bloom in behind it -- the two arrive as "
        "one hit but the low end does not smear the transient. 0 is instant.",
        Emphasis::trim);

    // ---- knock ----
    page.beginPlate ("Knock", "the beater on the head", kTintClick, true);

    page.addKnob (ids::k1Knock, "Knock",
        "A second, lower contact resonator -- the beater landing on the head, "
        "150 to 800 Hz, which sampled kicks have and the 3 kHz click cannot give. "
        "Struck with the hit, ringing for Knock time, cut exactly after four ring "
        "times. Velocity moves it with the click (Vel > Click). 0 is exact off.",
        Emphasis::lead);

    page.addKnob (ids::k1KnockTone, "Knock tone",
        "The knock's pitch, 150 to 800 Hz. Around 300 is a felt beater on a "
        "tight head; lower is a boxier thud, higher a wooden tock.");

    page.addKnob (ids::k1KnockTime, "Knock time",
        "How long the knock rings to -60 dB, 5 to 80 ms. Short is a tick under "
        "the click; long is a boxy body of its own.", Emphasis::trim);

    // ---- room ----
    page.beginPlate ("Room", "the kick's early reflections", kTintAmplitude, true);

    page.addKnob (kRoomIds[static_cast<int> (RoomIndex::kick1)].level, "Room",
        "Early reflections of the kick, 0 to 100%: 48 taps a side, random signs, "
        "falling 30 dB over the room's length, added back as mid AND side -- so "
        "the room is what puts a kick in a stereo space without a send. Kept mono "
        "under Mono below on the MIX page, which is what keeps the sub centred. "
        "Smoothed, so it can be ridden. 0 runs no room at all.", Emphasis::lead);

    page.addKnob (kRoomIds[static_cast<int> (RoomIndex::kick1)].size, "Size",
        "The reflections' span, 10 to 250 ms: a booth at 20, a live room at 80, "
        "a hall's first wall at 200. Redrawn at the next hit, not under a ringing "
        "one.");

    page.addKnob (kRoomIds[static_cast<int> (RoomIndex::kick1)].tone, "Tone",
        "A low-pass on the reflections, 500 Hz to 20 kHz -- a room is duller than "
        "the drum that fills it. 20 kHz is off, exactly.", Emphasis::trim);

    page.setValueText (kRoomIds[static_cast<int> (RoomIndex::kick1)].tone, [] (double hz)
    {
        return hz >= 19999.0 ? juce::String ("Off") : juce::String (juce::roundToInt (hz));
    });

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
            "Lit: the ghost is SNARE's drum -- Tune, Key, Note, Spread, Tone, Head, "
            "Snappy, Shape, Wires tilt and Bed follow the main snare and are greyed here "
            "-- and only the stroke and its placement are its own: Decay, Start, Drop, "
            "Body, Wires, Wires decay, the rattle and its controls, Wires stereo, the "
            "crack, the room, Level, Gate and velocity. That is what a ghost note is: the "
            "same drum, hit lighter and shorter between the backbeats. Dark: a fully "
            "separate second snare.");

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

    page.addKnob (ids.ring, "Ring",
        "The upper two modes' decay against the fundamental's. They sit at 0.7 "
        "and 0.5 of Decay by measurement; this scales both by up to three "
        "times either way. -100 is a dead thud with the upper pair gone in a "
        "third of the time; +100 a rimshot ring three times as long. 0 is "
        "exactly the drum as measured and costs nothing.");

    page.addKnob (ids.head, "Head",
        "Where the two upper modes sit: at 0 the snare's set, 1.6 and 2.2 times "
        "the fundamental; at 100 a tom's, 2.16 and 3.14 -- the ratios measured on "
        "a real tom-tom in the drum-physics literature (Fletcher & Rossing, Table "
        "18.7). A glide between them, geometric, so the shell goes from snare to "
        "tom without a step. The Perc pad is this engine on a tom's defaults; "
        "this is how the snare gets there too. 0 is exact.");

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

    page.addKnob (ids.wiresHold, "Wires hold",
        "How long the wires stay at FULL level before they start to fall, 0 to "
        "300 ms. A snare's wires do not begin dying the instant the stick "
        "lands -- they are thrown against a head that is still moving and stay "
        "there for a moment. Without this the only way to get a long buzz is a "
        "long decay, which washes; with it the buzz has a length of its own "
        "and then falls at whatever Wires decay says. 0 is the old behaviour "
        "exactly.");

    page.addKnob (ids.wiresDecay, "Wires decay",
        "How long the stick's burst on the wires takes to land, 50 to 400 ms. With "
        "Rattle up the wires also follow the shell, past this.");

    views.wires = static_cast<WiresView*> (
        page.addDisplay (std::make_unique<WiresView> (ictus_, palette_, page.tintOf (kTintColour), ids), 3));

    // ---- the rattle: the shell's throw on the wires ----
    page.beginPlate ("Rattle", "the shell throwing the wires", kTintColour);

    page.addKnob (ids.rattle, "Rattle",
        "How much the shell's own motion drives the wires, 0 to 100%: the one "
        "nonlinearity kept from the physical models, so the wires buzz for as "
        "long as the drum rings. At 100 they start 1.8 times louder and are still "
        "there at 100 ms where the plain burst has ended, 29 dB down with the "
        "shell. 0 runs no follower and is exact.", Emphasis::lead);

    page.addKnob (ids.rattleDecay, "Rattle decay",
        "How long the shell's throw lasts, on top of the shell's own fall. SHELL "
        "(the knob at 0) is the old behaviour exactly: the buzz lasts as long as "
        "the drum rings -- and Ring made the drum ring. A time here is a fall of "
        "its own to -60 dB, 10 ms to 2 s, multiplied in, so a long-ringing shell "
        "can carry a short buzz: the wires stop before the drum does, as they do "
        "on a real snare. Cut exactly once 120 dB down.");

    page.addKnob (ids.rattleTone, "Rattle tone",
        "The throw's own corner against the snap's, -24 to +24 semitones. 0 is the "
        "same filter as the stick's burst, exactly. Up, the throw's high-pass rises "
        "above Snappy -- sizzle. Down, the corner falls AND the filter narrows to a "
        "band-pass on the way, so two octaves down is the wires' own low chatter, a "
        "buzz with a pitch (measured centroid 3.7 kHz against 9.5 flat and 13.4 two "
        "up). The dark end is made up by 2 per octave, so it is judged as tone, not "
        "loudness. Costs a second filter per stream while it is off 0.");

    page.addKnob (ids.rattleTension, "Tension",
        "The snares' tension, 0 to 100%. On a real drum the snares only leave the "
        "head and strike it again above a certain head amplitude, and that "
        "amplitude rises with their tension -- a soft blow at high tension does not "
        "rattle at all (Fletcher & Rossing 18.13). 0 is the smooth follower as it "
        "always was. Up, the drive becomes the head's lift above that threshold: "
        "a train of strikes at the head's period that stops when the shell has "
        "fallen under it -- 117, 73 and 28 ms into a 250 ms snare at 25, 50 and "
        "100 % -- and that a soft hit reaches less of (by Vel > Wires). The knob "
        "is squared, so the first half is subtle. Costs one compare a sample.");

    // ---- the wires' colour and placement ----
    page.beginPlate ("Wire colour", "slope, bed and field", kTintColour, true);

    page.addKnob (ids.wiresTilt, "Wires tilt",
        "The wires' slope about the Snappy corner, -100 (dark) to +100 (bright): "
        "a low shelf and a high shelf of opposite sign, 12 dB at the ends, so it "
        "is a slope and not a volume (measured centroids 6.3, 9.5 and 11.2 kHz). "
        "Snappy could only thin the wires; this can dull them. 0 runs no shelf "
        "and is exact.");

    page.addKnob (ids.bed, "Bed",
        "The wires rung through six resonances at fixed ratios about the Snappy "
        "corner (0.71 to 2.31 times it, Q 8), crossfaded against the plain noise: "
        "the metal bed under a real set of snares, which is what makes wires "
        "sound like wires and not like static. 0 runs no bank and is exact.");

    page.addKnob (ids.wiresStereo, "Wires stereo",
        "The wires placed across the field, 0 to 100%: a second, independent "
        "stream of wires becomes the SIDE, through the same filter, tilt and bed, "
        "and the mid's share falls by 1 / sqrt (1 + s^2) so a channel's wires stay "
        "at the same level whatever the spread. The shell, the crack and the "
        "thump stay in the centre, so the snare still hits with the kick and only "
        "its top opens. The mono fold of a full spread is 3 dB down on the wires "
        "-- the price of decorrelation, and the MIX page's readout shows it. 0 "
        "runs no second stream and is exact.");

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

    // ---- thump ----
    page.beginPlate ("Thump", "the body under the head", kTintPitch, true);

    page.addKnob (ids.thump, "Thump",
        "A low mode under the shell, 40 to 200 Hz with its own decay -- the "
        "shell-and-air resonance a drum has below its head's fundamental, and "
        "the body a DnB snare gets when a kick is layered under it. It is not "
        "dropped with the shell and does not throw the wires. At 100 it is "
        "struck as hard as the fundamental. 0 is exact off and runs no mode.",
        Emphasis::lead);

    page.addKnob (ids.thumpTone, "Thump tone",
        "Where the thump sits, 40 to 200 Hz. 80 to 110 is a snare with weight; "
        "below 60 it is a kick hiding in the snare, and the two will fight "
        "unless the kick has moved.");

    page.addKnob (ids.thumpDecay, "Thump decay",
        "How long the thump rings to -60 dB, 30 to 500 ms. Short is a knock in "
        "the attack; long is a tom's body under the snare.", Emphasis::trim);

    // ---- room, and the clap under the snare ----
    {
        const int room = static_cast<int> (ghost ? RoomIndex::snare2 : RoomIndex::snare1);

        page.beginPlate ("Room", "early reflections of the whole hit", kTintAmplitude);

        page.addKnob (kRoomIds[room].level, "Room",
            "Early reflections of " + drum + ", 0 to 100%: 48 taps a side, random signs, "
            "falling 30 dB over the room's length, added back as mid AND side. The "
            "clap layer goes through it too. Mono under Mono below on the MIX page. "
            "Smoothed, so it can be ridden. 0 runs no room at all.", Emphasis::lead);

        page.addKnob (kRoomIds[room].size, "Size",
            "The reflections' span, 10 to 250 ms: a booth at 20, a live room at 80, "
            "a hall's first wall at 200. Redrawn at the next hit.");

        page.addKnob (kRoomIds[room].tone, "Tone",
            "A low-pass on the reflections, 500 Hz to 20 kHz. 20 kHz is off, exactly.",
            Emphasis::trim);
    }

    if (! ghost)
    {
        page.beginPlate ("Layer", "the clap under the snare", kTintClick, true);

        page.addKnob (ids::s1Clap, "Clap",
            "The CLAP page's sound played under this snare, 0 to 100%, with the "
            "snare's velocity, at the snare's pan and through the snare's room -- the "
            "snare-plus-clap of the classic sampled drum machines as one pad. This "
            "is the layer's own level: muting the clap pad does not mute it. 0 "
            "starts no layer at all.", Emphasis::lead);

        page.addKnob (ids::s1ClapOffset, "Offset",
            "How far behind the snare the clap lands, 0 to 50 ms. 0 is together; "
            "5 to 12 ms is a flam that reads as one thicker hit; more is two. "
            "Counted in internal samples on the control grid, so the same at every "
            "block size and rate.", Emphasis::trim);
    }

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

    page.setValueText (ids.rattleDecay, [] (double ms)
    {
        return ms <= 0.0 ? juce::String ("Shell") : juce::String (juce::roundToInt (ms));
    });

    page.setValueText (ids.rattleTone, [] (double semitones)
    {
        return (semitones > 0.0 ? "+" : "") + juce::String (semitones, 1);
    });

    page.setValueText (kRoomIds[static_cast<int> (ghost ? RoomIndex::snare2 : RoomIndex::snare1)].tone, [] (double hz)
    {
        return hz >= 19999.0 ? juce::String ("Off") : juce::String (juce::roundToInt (hz));
    });


    return views;
}

void IctusEditor::buildHatPage()
{
    hatPage_ = std::make_unique<PlatePage> (ictus_.getState(), palette_);
    auto& page = *hatPage_;

    // ---- the metal ----
    page.beginPlate ("Metal", "six oscillators that do not agree", kTintPitch);

    page.addKnob (ids::htTune, "Tune",
        "Where the lowest of the six partials sits, 60 Hz to 1.2 kHz, and the "
        "whole set moves with it -- and where the plate's lowest mode sits too. "
        "A cymbal has no pitch, so this is a size control rather than a note: "
        "low is a big dark lid, high a small tight one.", Emphasis::lead);

    page.addKnob (ids::htPlate, "Plate",
        "The six pulses crossfaded against a plate of metal: 64 modes placed by "
        "the law the cymbal literature fits real cymbals to, struck all at once, "
        "each dying at its own rate with the high ones first. At 0 this is the "
        "classic six-oscillator hat exactly; at 100 it is the plate alone, and "
        "that is where a chunky hat lives -- lower Colour, open Width and drop "
        "Highpass to let its body through. Costs 64 modes while it is up; "
        "nothing at 0.", Emphasis::lead);

    page.addKnob (ids::htHarmonics, "Harmonics",
        "Which set of six ratios, and everything between them. Metal is the "
        "published measurement of a classic analogue cymbal circuit; Bell is "
        "near-harmonic and rings with a pitch; Trash is wider and deliberately "
        "incommensurate; Wide spreads the six over three octaves. The morph is "
        "geometric and rank by rank, so a partial glides from one set's place to "
        "the next rather than jumping. Four more positions are reserved, so a set "
        "can be added later without moving anything you have saved.", Emphasis::lead);

    page.addKnob (ids::htSpread, "Spread",
        "Pulls the six apart against each other, up to a tone and a half either "
        "way along a fixed pattern that sums to zero -- so the set loosens "
        "without moving. 0 is the set exactly and runs no arithmetic at all.");

    page.addKnob (ids::htRing, "Ring",
        "The low three oscillators multiplied by the high three. A ring "
        "modulator's output holds the SUM AND DIFFERENCE of every pair of "
        "harmonics in its inputs, so this one multiply turns two sparse combs "
        "into a dense inharmonic wash -- which is what a plate of metal actually "
        "sounds like, and the single biggest difference between a thin hat and a "
        "lush one. Both sides are band-limited first, so it cannot alias at any "
        "setting. 0 runs none of it.", Emphasis::lead);

    page.addKnob (ids::htDrive, "Drive",
        "An antialiased soft clip over the metal and the hiss together -- the "
        "overdrive the cymbal patch in the Nord Modular chapter ends with. It "
        "fills the gaps between partials with intermodulation and glues the two "
        "layers into one instrument. 0 is exact.");

    page.addKnob (ids::htGrit, "Grit",
        "The steps of a low-resolution sample path: everything before Drive "
        "quantised from 16 bits at 0 down to 4 at 100, with about six bits -- the "
        "depth the classic sampled drum machines kept their cymbals at -- two "
        "thirds of the way up. It is quantisation used as a saturator, not a "
        "bit-crusher: the images are removed by the oversampling and what stays "
        "is the crunch, which is signal-correlated and fills the gaps between "
        "partials. 0 is exact.");

    page.addKnob (ids::htMetalStereo, "Metal stereo",
        "The metal placed across the field, 0 to 100%: the six pulses and the "
        "plate's modes each given a place, left or right, and read out as a "
        "SIDE while the mid stays exactly the hat it was -- so a mono fold of "
        "the pair is the old hat bit for bit. Width on the MIX page scales it. 0 "
        "runs no side at all.");

    page.addKnob (ids::htWash, "Wash",
        "Noise fed into the plate's modes for half the pad's decay, 0 to 100%. "
        "The strike alone leaves 64 pure lines each falling at its own rate; a "
        "real pair is fed and re-fed for as long as it moves. At 100 the wash "
        "puts as much energy into the plate as the strike did (the hit is about "
        "3 dB louder, measured x1.9 on a 100 ms pad and x1.8 on 500 ms) -- a "
        "noisier strike and a shimmer in the tail. Does nothing with Plate at 0; "
        "costs one noise draw and 64 weights a sample while it runs, and is over "
        "exactly at half the decay.");

    partialsView_ = static_cast<PartialsView*> (
        page.addDisplay (std::make_unique<PartialsView> (ictus_, palette_, page.tintOf (kTintPitch)), 3));

    // ---- the hiss ----
    page.beginPlate ("Hiss", "the plate rung by noise rather than struck", kTintColour);

    page.addKnob (ids::htAir, "Air",
        "The noise layer's level. On a real cymbal the sizzle is not something "
        "added to the metal -- it IS the metal, its own modes excited chaotically "
        "rather than struck cleanly. That is why a sampled hat sounds like one "
        "object. 0 draws no noise at all.", Emphasis::lead);

    page.addKnob (ids::htSizzle, "Sizzle",
        "How much of the hiss is rung through the metal rather than left flat. "
        "At 0 it is raw filtered noise beside the metal -- two things at once. "
        "At 100 every bit of it rings at six of the partials' harmonics, the "
        "ones nearest the two bands, so the noise and the metal are one "
        "instrument where it is actually heard. Costs six band-passes while it "
        "is up, and none at 0.", Emphasis::lead);

    page.addKnob (ids::htAirTone, "Air tone",
        "The hiss's own high-pass, 200 Hz to 12 kHz, before it meets the metal. "
        "Low is a broad spray, high a thin sizzle sitting over the top.");

    page.addKnob (ids::htAirDecay, "Air decay",
        "How long the hiss lasts as a percentage of the pad's own decay. Under "
        "100 is a fast chiff on the front of a longer metal ring; over 100 is a "
        "shimmer that outlives it, which is most of what a long open hat is.");

    page.addKnob (ids::htAirStereo, "Air stereo",
        "The hiss placed across the field, 0 to 100%: a second, independent "
        "stream of hiss becomes the SIDE, through its own copy of the tone, the "
        "tilt and the sizzle bank, and the mid's share falls by 1 / sqrt (1 + s^2) "
        "so a channel's hiss stays at the same level whatever the spread. The "
        "mono fold of a full spread is 3 dB down on the hiss -- the price of "
        "decorrelation, and the MIX page's readout shows it. Costs a second "
        "filter chain while it is up; 0 runs none and is exact.");

    // ---- the hiss's texture ----
    page.beginPlate ("Texture", "what kind of hiss", kTintColour, true);

    page.addKnob (ids::htAirTilt, "Air tilt",
        "The hiss's slope about 6 kHz, dark to bright. Air tone is a high-pass "
        "and could only ever thin the hiss; this can dull it. A low shelf and a "
        "high shelf of opposite sign, 12 dB each at the ends, designed per hit. "
        "0 is exact and runs nothing.");

    page.addKnob (ids::htAirAttack, "Air attack",
        "The hiss's own rise, 0 to 500 ms. The metal is struck and the wash "
        "comes up behind it -- the swell of a real open hat's first 50 ms, and "
        "the reverse-hat feel further up. 0 is instant, as it always was.");

    page.addKnob (ids::htGrain, "Grain",
        "The hiss's density, from an event every sample at 0 to a sparse "
        "crackle of 300 a second at 100 -- per second, so it is the same "
        "texture at every session rate. A cymbal's sizzle is chaotic rather "
        "than Gaussian, and the sandy, gritty hat lives at the sparse end; with "
        "Sizzle up each event rings the partials, a metallic crackle rather "
        "than a click. Quieter as it thins, by about 15 dB at the far end. 0 is "
        "exact.");

    // ---- the band ----
    page.beginPlate ("Colour", "the band it is heard through", kTintClick, true);

    page.addKnob (ids::htColour, "Colour",
        "The lower band-pass's centre, 800 Hz to 12 kHz; the upper follows at "
        "2.06 times it -- the spacing of the two bands in the circuit the Metal "
        "set was measured from. 3.4 kHz is the classic place; higher is thinner "
        "and more modern.", Emphasis::lead);

    page.addKnob (ids::htWidth, "Width",
        "How wide those two bands are, from a narrow whistle you can pick one "
        "partial out of to a band open enough to hear the whole plate at once. "
        "Wide is where a lush hat lives.");

    page.addKnob (ids::htHighpass, "Highpass",
        "What is taken off underneath, 200 Hz to 8 kHz. A hat should have no "
        "body: this is what keeps the oscillators' own low fundamentals out of "
        "it. Raise it for a thin tick, lower it to let some clank through.");

    page.addKnob (ids::htDamp, "Damp",
        "How far the top closes as the hit decays. On a real cymbal the high "
        "modes die first, and that fall from bright to dark over the ring is "
        "most of what makes a hat sound played rather than triggered. Retuned "
        "once every 32 samples, so it costs nothing per sample. 0 leaves the "
        "filter out of the path entirely.", Emphasis::lead);

    // ---- the envelope ----
    page.beginPlate ("Envelope", "the stick and the ring", kTintAmplitude);

    page.addKnob (ids::htStrike, "Strike",
        "The stick: a short loud transient over the body envelope, falling in "
        "6 ms. With Damp up it is automatically the brightest part of the hit, "
        "because Damp is still wide open when it lands. This is what a hat "
        "without an attack was missing.", Emphasis::lead);

    page.addKnob (ids::hcDecay, "Closed decay",
        "How long the CLOSED pad rings, 10 to 400 ms -- F#1 by default.",
        Emphasis::lead);

    page.addKnob (ids::hoDecay, "Open decay",
        "How long the OPEN pad rings, 100 ms to 3 s -- A#1 by default. Same "
        "cymbals, longer stroke.", Emphasis::lead);

    page.addKnob (ids::htHold, "Hold",
        "A plateau before the decay starts, 0 to 200 ms: the hit stays at full "
        "level for this long first. A little of it is the difference between a "
        "tick and a chick. Both pads, while Link is lit.", Emphasis::trim);

    page.addKnob (ids::hoHold, "Open hold",
        "The OPEN pad's own plateau, 0 to 1 s, in force while Link is dark. A "
        "long hold on the open hat and none on the closed is a pattern's "
        "sustain without touching the key; on a plate hat the envelope holds "
        "while the plate darkens underneath, its high modes dying first, which "
        "is how a real open hat behaves -- the six pulses hold flat.",
        Emphasis::trim);

    page.addLamp (ids::htHoldLink, "Hold link", "Link",
        "Lit: the open pad's hold is Hold, as it always was, and Open hold is "
        "greyed. Dark: the open pad holds for Open hold and the closed pad for "
        "Hold. A project saved before this existed opens lit and sounds the same.");

    page.addKnob (ids::htShape, "Shape",
        "The decay's curve: 0 exponential, the way a struck thing falls; 100 "
        "linear, which reads as gated and deliberate.", Emphasis::trim);

    // ---- stopping it ----
    page.beginPlate ("Stop", "how a hat is silenced", kTintAmplitude, true);

    page.addLamp (ids::htChoke, "Choke", "Choke",
        "Lit: a closed-hat hit silences whatever the open pad is ringing, fading "
        "it over 5 ms -- the foot coming down on the pedal, which is how a hi-hat "
        "works and how a break sounds. Dark: the two ring past each other. "
        "Skipped when both pads are on the same key, where you have asked for "
        "both.");

    page.addLamp (ids::htGate, "Gate", "Gate",
        "Lit: a note-off fades the WHOLE hit out over Release -- metal, hiss and "
        "the filters' ring alike -- from wherever it is, on BOTH pads. That is "
        "how you play a long open hat and stop it by lifting the key rather than "
        "waiting for a closed hit to choke it. Dark: a one-shot that ignores "
        "note-off, and the HIT button always plays the whole hit either way.");

    page.addKnob (ids::htRelease, "Release",
        "How long a gated hat takes to fade after the key lifts, 0 to 2 s. 0 is "
        "a 1 ms ramp, the shortest that does not click. Does nothing with Gate "
        "dark.");

    // ---- out ----
    page.beginPlate ("Out", "level and what velocity does", kTintVelocity);

    page.addKnob (ids::htLevel, "Level",
        "Both hat pads' level before the output trim, 0 to 100%.", Emphasis::lead);

    page.addKnob (ids::htVelLevel, "Vel > Level",
        "How much velocity moves the level, 0 to 100%.", Emphasis::trim);

    page.addKnob (ids::htVelDecay, "Vel > Decay",
        "How much velocity shortens a soft hit, 0 to 100%: a light tap does not "
        "ring as long as a hard one.", Emphasis::trim);

    page.addKnob (ids::htVelColour, "Vel > Colour",
        "How much velocity moves Colour, 0 to 100%: a soft hit is darker as well "
        "as quieter, which is most of what makes programmed sixteenths sound "
        "played.", Emphasis::trim);

    page.addKnob (ids::htVelStrike, "Vel > Strike",
        "How much velocity moves the stick, 0 to 100%: harder hits get more "
        "attack, softer ones almost none.", Emphasis::trim);

    page.addKnob (ids::htVelAir, "Vel > Air",
        "How much velocity moves the hiss, 0 to 100%: a soft tap with less "
        "spray, a hard one with more. 0 -- the default -- gives every hit the "
        "same hiss.", Emphasis::trim);

    page.setNote ("One pair of cymbals, struck two ways: the closed pad (F#1) and the open one "
                  "(A#1) share every control but their decay. Six band-limited pulses or a "
                  "64-mode plate (Plate), a ring modulator, an overdrive, the steps of a "
                  "low-resolution sample path (Grit) and a noise layer rung through the same six "
                  "partials, all inside the oversampled section. Measured: inharmonic energy in "
                  "the audible band at -74 to -77 dB at the rate Auto runs, against -12 to -17 dB "
                  "for the same six pulses generated naively; oversampling Off at 48 kHz costs "
                  "about 40 dB of that. Plate 0 and Grit 0 are the hat exactly as it was.");

    page.setValueText (ids::htHarmonics, [] (double position)
    {
        const double clamped = juce::jlimit (0.0, HatEngine::kMaxHarmonicsPosition, position);
        const int lower = std::min (static_cast<int> (clamped), HatEngine::kSetCount - 1);
        const double fraction = lower >= HatEngine::kSetCount - 1 ? 0.0 : clamped - lower;

        if (fraction <= 0.005)
            return juce::String (HatEngine::kSetNames[lower]);

        return juce::String (HatEngine::kSetNames[lower]) + " "
             + juce::String (100.0 * fraction, 0) + "%";
    });

}

void IctusEditor::buildClapPage()
{
    clapPage_ = std::make_unique<PlatePage> (ictus_.getState(), palette_);
    auto& page = *clapPage_;

    // ---- the pattern ----
    page.beginPlate ("Bursts", "several people, not quite together", kTintClick);

    page.addKnob (ids::cpBursts, "Bursts",
        "How many bursts the hit is made of, two to six. Two is a pair of hands; "
        "six is a room. The last one is also where the tail starts.",
        Emphasis::lead);

    page.addKnob (ids::cpFlam, "Flam",
        "The first gap between bursts, 4 to 30 ms. About 11 ms is a room full of "
        "people clapping at a signal; tighter reads as one pair of hands, wider "
        "as a crowd. Counted in samples from this, so the pattern lands on the "
        "same instants at 44.1 and at 192 kHz.", Emphasis::lead);

    page.addKnob (ids::cpSkew, "Skew",
        "Whether the bursts crowd together or spread out as the hit goes on: "
        "each gap is a fixed fraction of the one before it. 0 is an even pattern "
        "and is even exactly, which is the one thing real people never do -- a "
        "little either way is most of what stops a clap sounding programmed.");

    page.addKnob (ids::cpSnap, "Snap",
        "How fast each burst falls, 1 to 20 ms. Short is a slap you can count; "
        "long smears them into one gesture.");

    page.addKnob (ids::cpStereo, "Stereo",
        "The bursts placed across the field, 0 to 100%: each of the six lands a "
        "little left or right of the last, read out as a SIDE, and the room's "
        "tail gets a second stream on the side of its own -- several people are "
        "never in one place. The mid stays the clap it was. 0 runs no side and is "
        "exact.");

    burstView_ = static_cast<BurstView*> (
        page.addDisplay (std::make_unique<BurstView> (ictus_, palette_, page.tintOf (kTintClick)), 3));

    // ---- the hiss ----
    page.beginPlate ("Hiss", "the air between the hands", kTintColour);

    page.addKnob (ids::cpNoise, "Noise",
        "The noise layer's level, 0 to 100%. Turn it down and what is left is "
        "the body alone -- a pitched knock rather than a clap.", Emphasis::lead);

    page.addKnob (ids::cpNoiseTone, "Noise tone",
        "The hiss's own high-pass, 200 Hz to 8 kHz, before the band. Low is a "
        "full smack, high a dry papery snap.");

    // ---- the body ----
    page.beginPlate ("Body", "cupped hands are a cavity, and it rings", kTintPitch, true);

    page.addKnob (ids::cpBody, "Body",
        "The cavity's level. A clap is not only noise: the hands enclose air and "
        "it resonates, which is what gives a real one a pitch under the hiss. "
        "Three inharmonic modes, struck by every burst. 0 runs no bank at all.",
        Emphasis::lead);

    page.addKnob (ids::cpBodyPitch, "Pitch",
        "Where the cavity sits, 200 Hz to 2.5 kHz. Around 900 is a pair of hands; "
        "lower is cupped and hollow, higher is flat-palmed and sharp.");

    page.addKnob (ids::cpBodyRing, "Ring",
        "How long the cavity holds, 10 to 500 ms. Short is a knock inside the "
        "clap; long turns the pad into something closer to a rim or a block.");

    // ---- what it is heard through ----
    page.beginPlate ("Colour", "where the smack sits", kTintAmplitude);

    page.addKnob (ids::cpColour, "Colour",
        "The band-pass's centre, 300 Hz to 6 kHz. A clap is a mid-band event, "
        "all smack and no weight: 1 to 2 kHz is a hand, lower is a thump, higher "
        "is a snap.", Emphasis::lead);

    page.addKnob (ids::cpWidth, "Width",
        "How wide that band is: narrow enough to place the smack precisely, or "
        "open enough to keep every burst a separate slap.");

    page.addKnob (ids::cpTail, "Tail",
        "The room's answer, starting with the last burst and falling over 30 ms "
        "to 1 s. This is what makes a clap a clap rather than a row of noise "
        "bursts.", Emphasis::lead);

    page.addKnob (ids::cpTailTone, "Tail tone",
        "Where the room sits as a percentage of Colour. A room is duller than "
        "the hands that fill it, so under 100 is the honest direction.");

    page.addKnob (ids::cpDrive, "Drive",
        "An antialiased soft clip over the whole clap, for a harder, flatter "
        "one that sits in front of a mix. Trimmed by the gain it adds, so it "
        "buys harmonics and not loudness: measured across the whole range the "
        "clap's RMS moves 1.7 dB while its peak falls by more than half, which "
        "is the clipping doing its job. 0 is exact.");

    // ---- out ----
    page.beginPlate ("Out", "level and velocity", kTintVelocity, true);

    page.addKnob (ids::cpLevel, "Level",
        "The clap's level before the output trim, 0 to 100%. Under a snare "
        "rather than instead of one is where this usually sits.", Emphasis::lead);

    page.addKnob (ids::cpVelLevel, "Vel > Level",
        "How much velocity moves the level, 0 to 100%.", Emphasis::trim);

    page.addLamp (ids::cpGate, "Gate", "Gate",
        "Lit: a note-off fades the WHOLE clap out over Release from wherever "
        "it is -- the bursts still to come, the cavity's ring and the room "
        "alike. That is how you stop a long clap by lifting the key rather "
        "than waiting for the room to finish. Dark: a one-shot that ignores "
        "note-off, and the HIT button always plays the whole hit either way.");

    page.addKnob (ids::cpRelease, "Release",
        "How long a gated clap takes to fade after the key lifts, 0 to 2 s. 0 "
        "is a 1 ms ramp, the shortest that does not click. Does nothing with "
        "Gate dark.");

    // ---- room ----
    page.beginPlate ("Room", "a clap is mostly its room", kTintAmplitude);

    page.addKnob (kRoomIds[static_cast<int> (RoomIndex::clap)].level, "Room",
        "Early reflections of the clap, 0 to 100%: 48 taps a side, random signs, "
        "falling 30 dB over the room's length, added back as mid AND side. Tail "
        "is the clap's own answer; this is the space around it. Mono under Mono "
        "below on the MIX page. Smoothed. 0 runs no room at all.", Emphasis::lead);

    page.addKnob (kRoomIds[static_cast<int> (RoomIndex::clap)].size, "Size",
        "The reflections' span, 10 to 250 ms. Redrawn at the next hit.");

    page.addKnob (kRoomIds[static_cast<int> (RoomIndex::clap)].tone, "Tone",
        "A low-pass on the reflections, 500 Hz to 20 kHz. 20 kHz is off, exactly.",
        Emphasis::trim);

    page.setValueText (kRoomIds[static_cast<int> (RoomIndex::clap)].tone, [] (double hz)
    {
        return hz >= 19999.0 ? juce::String ("Off") : juce::String (juce::roundToInt (hz));
    });

    page.setNote ("The clap is on D#1 by default. Bursts of noise a Flam apart with their "
                  "envelopes summed, a cavity struck by each of them, and then the room: the "
                  "pattern is the recipe from the Nord Modular percussion chapter, which is "
                  "where the four pulses about 11 ms apart come from; the body and the skew are "
                  "what make it more than one sound. Humanising the spacing arrives with every "
                  "other pad's humanise control.");

    page.setValueText (ids::cpBursts, [] (double count)
    {
        return juce::String (juce::roundToInt (count));
    });

}

// ---------------------------------------------------------------------------
// The mix page: where the pads sit in the field
// ---------------------------------------------------------------------------

void IctusEditor::buildMixPage()
{
    mixPage_ = std::make_unique<PlatePage> (ictus_.getState(), palette_);
    auto& page = *mixPage_;

    page.beginPlate ("Pan", "where each pad sits, left to right", kTintAmplitude);

    struct PanEntry
    {
        PadIndex pad;
        const char* name;
        const char* note;
    };

    static const PanEntry entries[] {
        { PadIndex::kick1,     "Kick",   "The kick belongs in the centre, and so does anything under 100 Hz: a sub that leans is a sub that halves in mono." },
        { PadIndex::kick2,     "Kick 2", "The second kick, on B0 by default." },
        { PadIndex::snare1,    "Snare",  "The snare is the second anchor: its body and its attack stay centred so it hits with the kick." },
        { PadIndex::snare2,    "Ghost",  "The ghost can sit a little off the main snare, which is where a drummer's hand would be." },
        { PadIndex::perc,      "Perc",   "Percussion is usually placed opposite the hats, for balance." },
        { PadIndex::hatClosed, "Hat C",  "A closed hat a little off centre -- 10 to 25% -- is where a drummer's hat is, and where a break sounds played." },
        { PadIndex::hatOpen,   "Hat O",  "The open hat the same side as the closed one, or a touch wider." },
        { PadIndex::clap,      "Clap",   "A clap's width is in its bursts, which the next round spreads; its centre of mass can sit anywhere." },
    };

    for (const auto& entry : entries)
    {
        const int index = static_cast<int> (entry.pad);

        page.addKnob (kPanIds[index], entry.name,
            juce::String (entry.name) + " pan, -100 (hard left) to +100 (hard right), on a balance law: "
            "the near channel stays at unity and the far one falls to nothing, so the centre is "
            "both channels at unity -- the dual mono this instrument always rendered, bit for bit -- "
            "and nothing gets 3 dB quieter for being centred. Smoothed over 20 ms, so it can be "
            "automated. " + juce::String (entry.note), Emphasis::trim);

        page.setValueText (kPanIds[index], [] (double value)
        {
            const int amount = juce::roundToInt (std::abs (value));

            if (amount == 0)
                return juce::String ("C");

            return juce::String (value < 0.0 ? "L " : "R ") + juce::String (amount);
        });
    }

    // ---- width ----
    page.beginPlate ("Width", "how far each pad's side reaches", kTintColour);

    for (const auto& entry : entries)
    {
        const int index = static_cast<int> (entry.pad);

        page.addKnob (kWidthIds[index], entry.name,
            juce::String (entry.name) + " width, 0 to 200%: a gain on the pad's SIDE signal alone. 100 is "
            "the field the pad's own spread controls make (Air stereo, Metal stereo, Wires stereo, the "
            "clap's Stereo, a Room), exactly; 0 folds the pad to mono; 200 doubles the spread. A pad "
            "with nothing spread and no room has no side, and this does nothing -- it is greyed then. "
            "Smoothed over 20 ms.", Emphasis::trim);
    }

    // ---- mono below ----
    page.beginPlate ("Mono below", "the corner under which each pad is centred", kTintPitch);

    for (const auto& entry : entries)
    {
        const int index = static_cast<int> (entry.pad);

        page.addKnob (kMonoBelowIds[index], entry.name,
            juce::String (entry.name) + ": the pad's side is high-passed here, 40 to 500 Hz, second "
            "order, so whatever is spread above it the low end stays in the centre and a fold to mono "
            "loses nothing there. 150 Hz by default, because a club system cannot place anything "
            "below it. OFF (the knob at 0) leaves the side whole. Moves under a ringing pad without a "
            "step, and costs nothing on a pad with no side.", Emphasis::trim);

        page.setValueText (kMonoBelowIds[index], [] (double hz)
        {
            return hz <= 0.0 ? juce::String ("Off") : juce::String (juce::roundToInt (hz));
        });
    }

    // ---- the readout ----
    page.beginPlate ("Field", "what the output's correlation is doing", kTintAmplitude);

    fieldView_ = static_cast<FieldView*> (
        page.addDisplay (std::make_unique<FieldView> (ictus_, palette_, page.tintOf (kTintAmplitude)), 4));

    page.setNote ("The pads in the field, so the kit does not need a mixer to be placed. Pan is a balance: "
                  "the centre is exactly the mono render every saved project was mixed against, and a pad "
                  "hard left leaves the right channel exactly empty. The side of a pad comes from the drum "
                  "itself -- the hats' Air stereo and Metal stereo, the snares' Wires stereo, the clap's "
                  "Stereo, and a Room on the kick, the snares and the clap -- and Width scales it, Mono "
                  "below keeps the low end of it centred, and the readout shows the result. A pad with "
                  "nothing spread has no side and is the mono render bit for bit. For a drum and bass "
                  "mix: the kick centred and mono, the snare's body centred with only its wires and room "
                  "opening, the hats 10 to 25 % off and spread, the clap wide -- the readout's low band "
                  "lit throughout.");

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
    currentPage_ = juce::jlimit (0, 6, index);

    PlatePage* const pages[] = { kickPage_.get(), snarePage_.get(), ghostPage_.get(), nullptr,
                                 hatPage_.get(), clapPage_.get(), mixPage_.get() };

    if (auto* page = pages[currentPage_])
    {
        if (pageView_.getViewedComponent() != page)
        {
            pageView_.setViewedComponent (page, false);
            pageView_.setViewPosition (0, 0);
        }

        page->setVisible (true);
        pageView_.setVisible (true);
    }
    else
    {
        pageView_.setVisible (false);
    }

    tuningPage_->setVisible (currentPage_ == 3);

    if (currentPage_ == 0)
        currentPad_ = PadIndex::kick1;
    else if (currentPage_ == 1)
        currentPad_ = PadIndex::snare1;
    else if (currentPage_ == 2)
        currentPad_ = PadIndex::snare2;
    else if (currentPage_ == 5)
        currentPad_ = PadIndex::clap;
    else if (currentPage_ == 4
             && currentPad_ != PadIndex::hatClosed && currentPad_ != PadIndex::hatOpen)
        // One page, two pads: opening it from anywhere else lands on the
        // closed hat, but selecting the open pad keeps HIT on the open pad.
        currentPad_ = PadIndex::hatClosed;

    styleTab (tuningTab_, currentPage_ == 3);
    styleTab (mixTab_, currentPage_ == 6);
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
    else if (pad == PadIndex::hatClosed || pad == PadIndex::hatOpen)
    {
        currentPad_ = pad;
        showPage (4);
    }
    else if (pad == PadIndex::clap)
        showPage (5);
}

void IctusEditor::refreshPadStrip()
{
    const auto drumName = [this]
    {
        switch (currentPad_)
        {
            case PadIndex::kick1:     return "the kick";
            case PadIndex::snare1:    return "the snare";
            case PadIndex::snare2:    return "the ghost snare";
            case PadIndex::hatClosed: return "the closed hat";
            case PadIndex::hatOpen:   return "the open hat";
            case PadIndex::clap:      return "the clap";
            case PadIndex::perc:      return "the perc";
            case PadIndex::kick2:     return "the second kick";
            case PadIndex::count:
            default:                  return "the pad";
        }
    };

    hitButton_.setTooltip (juce::String ("Strikes ") + drumName() + " -- "
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
                           static_cast<DrumDisplay*> (ghostViews_.envelope),
                           static_cast<DrumDisplay*> (partialsView_),
                           static_cast<DrumDisplay*> (burstView_),
                           static_cast<DrumDisplay*> (fieldView_) })
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
    now.thump = read (ids.thump) > 0.0f;
    now.rattle = read (ids.rattle) > 0.0f;
    now.room = read (kRoomIds[static_cast<int> (ids.link != nullptr ? RoomIndex::snare2 : RoomIndex::snare1)].level) > 0.0f;
    now.clap = ids.link != nullptr || read (ids::s1Clap) > 0.0f;

    if (now.wires == shown.wires && now.crack == shown.crack && now.noise == shown.noise
        && now.gate == shown.gate && now.keyed == shown.keyed && now.linked == shown.linked
        && now.thump == shown.thump && now.rattle == shown.rattle && now.room == shown.room
        && now.clap == shown.clap)
        return;

    shown = now;

    // The wires' controls with the wires, the crack's tone with the crack,
    // the noise time with the noise, Release with Gate, Tune with its own
    // Key -- and on the ghost, everything LINK borrows from the main snare.
    page.setControlEnabled (ids.snappy, now.wires && ! now.linked);
    page.setControlEnabled (ids.snap, now.wires && ! now.linked);
    page.setControlEnabled (ids.wiresHold, now.wires);
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
    page.setControlEnabled (ids.thumpTone, now.thump);
    page.setControlEnabled (ids.thumpDecay, now.thump);

    // I4.5: the rattle's controls with the rattle, the wires' colour with the
    // wires (and, on the ghost, with LINK dark), the room's size and tone with
    // the room, the clap's offset with the clap.
    page.setControlEnabled (ids.rattleDecay, now.wires && now.rattle);
    page.setControlEnabled (ids.rattleTone, now.wires && now.rattle);
    page.setControlEnabled (ids.rattleTension, now.wires && now.rattle);
    page.setControlEnabled (ids.head, ! now.linked);
    page.setControlEnabled (ids.wiresTilt, now.wires && ! now.linked);
    page.setControlEnabled (ids.bed, now.wires && ! now.linked);
    page.setControlEnabled (ids.wiresStereo, now.wires);

    const int room = static_cast<int> (ids.link != nullptr ? RoomIndex::snare2 : RoomIndex::snare1);
    page.setControlEnabled (kRoomIds[room].size, now.room);
    page.setControlEnabled (kRoomIds[room].tone, now.room);

    if (ids.link == nullptr)
        page.setControlEnabled (ids::s1ClapOffset, now.clap);
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
    const bool under = read (ids::k1Under) > 0.0f;
    const bool knock = read (ids::k1Knock) > 0.0f;
    const bool kickRoom = read (kRoomIds[static_cast<int> (RoomIndex::kick1)].level) > 0.0f;

    // Tune does nothing while the key sets the pitch -- Follow key lit, or
    // Bass mode, which keys every hit.
    const bool keyed = read (ids::k1FollowKey) > 0.5f || read (ids::bassMode) > 0.5f;

    if (toneOn != shownToneOn_ || harmonics != shownHarmonics_ || tail != shownTail_
        || gate != shownGate_ || keyed != shownKeyed_ || under != shownUnder_ || knock != shownKnock_
        || kickRoom != shownKickRoom_)
    {
        shownToneOn_ = toneOn;
        shownHarmonics_ = harmonics;
        shownTail_ = tail;
        shownGate_ = gate;
        shownKeyed_ = keyed;
        shownUnder_ = under;
        shownKnock_ = knock;
        shownKickRoom_ = kickRoom;

        kickPage_->setControlEnabled (ids::k1Tone, toneOn);
        kickPage_->setControlEnabled (ids::k1Even, harmonics);
        kickPage_->setControlEnabled (ids::k1TailTime, tail);
        kickPage_->setControlEnabled (ids::k1Release, gate);
        kickPage_->setControlEnabled (ids::k1Tune, ! keyed);
        kickPage_->setControlEnabled (ids::k1NoteSnap, ! keyed);
        kickPage_->setControlEnabled (ids::k1UnderInterval, under);
        kickPage_->setControlEnabled (ids::k1UnderDecay, under);
        kickPage_->setControlEnabled (ids::k1UnderAttack, under);
        kickPage_->setControlEnabled (ids::k1KnockTone, knock);
        kickPage_->setControlEnabled (ids::k1KnockTime, knock);
        kickPage_->setControlEnabled (kRoomIds[static_cast<int> (RoomIndex::kick1)].size, kickRoom);
        kickPage_->setControlEnabled (kRoomIds[static_cast<int> (RoomIndex::kick1)].tone, kickRoom);
    }

    updateSnareGreying (*snarePage_, kSnare1Ids, shownSnare_);
    updateSnareGreying (*ghostPage_, kGhostIds, shownGhost_);

    // The hats: what the noise layer's controls and the gate's release do
    // nothing without.
    {
        const auto value = [this] (const char* id)
        {
            return static_cast<double> (ictus_.getState().getRawParameterValue (id)->load());
        };

        const bool air = value (ids::htAir) > 0.0;
        const bool hatGate = value (ids::htGate) > 0.5;
        const bool holdLink = value (ids::htHoldLink) > 0.5;

        if (air != shownHatAir_)
        {
            shownHatAir_ = air;
            hatPage_->setControlEnabled (ids::htSizzle, air);
            hatPage_->setControlEnabled (ids::htAirTone, air);
            hatPage_->setControlEnabled (ids::htAirDecay, air);
            hatPage_->setControlEnabled (ids::htAirTilt, air);
            hatPage_->setControlEnabled (ids::htAirAttack, air);
            hatPage_->setControlEnabled (ids::htGrain, air);
            hatPage_->setControlEnabled (ids::htVelAir, air);
            hatPage_->setControlEnabled (ids::htAirStereo, air);
        }

        const bool plate = value (ids::htPlate) > 0.0;

        if (plate != shownHatPlate_)
        {
            shownHatPlate_ = plate;
            hatPage_->setControlEnabled (ids::htWash, plate);
        }

        if (hatGate != shownHatGate_)
        {
            shownHatGate_ = hatGate;
            hatPage_->setControlEnabled (ids::htRelease, hatGate);
        }

        if (holdLink != shownHatHoldLink_)
        {
            shownHatHoldLink_ = holdLink;
            hatPage_->setControlEnabled (ids::hoHold, ! holdLink);
        }

        const bool body = value (ids::cpBody) > 0.0;
        const bool noise = value (ids::cpNoise) > 0.0;

        if (body != shownClapBody_)
        {
            shownClapBody_ = body;
            clapPage_->setControlEnabled (ids::cpBodyPitch, body);
            clapPage_->setControlEnabled (ids::cpBodyRing, body);
        }

        if (noise != shownClapNoise_)
        {
            shownClapNoise_ = noise;
            clapPage_->setControlEnabled (ids::cpNoiseTone, noise);
        }

        const bool clapRoom = value (kRoomIds[static_cast<int> (RoomIndex::clap)].level) > 0.0;

        if (clapRoom != shownClapRoom_)
        {
            shownClapRoom_ = clapRoom;
            clapPage_->setControlEnabled (kRoomIds[static_cast<int> (RoomIndex::clap)].size, clapRoom);
            clapPage_->setControlEnabled (kRoomIds[static_cast<int> (RoomIndex::clap)].tone, clapRoom);
        }

        // The MIX page: Width and Mono below act on a pad's side, which only
        // a spread control or a room makes. A pad with neither has no side,
        // and the two knobs say so by greying.
        const bool hatSide = value (ids::htAirStereo) > 0.0 || value (ids::htMetalStereo) > 0.0;
        const bool clapSide = value (ids::cpStereo) > 0.0 || clapRoom;

        const bool padSide[kPadCount] {
            value (kRoomIds[static_cast<int> (RoomIndex::kick1)].level) > 0.0,
            value (ids::s1WiresStereo) > 0.0 || value (kRoomIds[static_cast<int> (RoomIndex::snare1)].level) > 0.0
                || (value (ids::s1Clap) > 0.0 && value (ids::cpStereo) > 0.0),
            hatSide,
            hatSide,
            clapSide,
            false,
            false,
            value (ids::g1WiresStereo) > 0.0 || value (kRoomIds[static_cast<int> (RoomIndex::snare2)].level) > 0.0,
        };

        for (int pad = 0; pad < kPadCount; ++pad)
            if (padSide[pad] != shownPadField_[pad])
            {
                shownPadField_[pad] = padSide[pad];
                mixPage_->setControlEnabled (kWidthIds[pad], padSide[pad]);
                mixPage_->setControlEnabled (kMonoBelowIds[pad], padSide[pad]);
            }

        const bool clapGate = value (ids::cpGate) > 0.5;

        if (clapGate != shownClapGate_)
        {
            shownClapGate_ = clapGate;
            clapPage_->setControlEnabled (ids::cpRelease, clapGate);
        }
    }
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
    mixTab_.setBounds (strip.removeFromRight (64).reduced (2, 2));
    strip.removeFromRight (6);
    bassButton_.setBounds (ui::LampButton::sized (64, 26).withCentre (strip.removeFromRight (72).getCentre()));
    strip.removeFromRight (6);

    padStrip_->setBounds (strip);

    bounds.reduce (8, 4);
    pageView_.setBounds (bounds);
    layoutViewedPage();
    tuningPage_->setBounds (bounds);
}

void IctusEditor::layoutViewedPage()
{
    auto* page = dynamic_cast<PlatePage*> (pageView_.getViewedComponent());

    if (page == nullptr)
        return;

    const auto area = pageView_.getLocalBounds();
    const int needed = page->minimumHeight();
    const bool scrolls = needed > area.getHeight();
    const int width = area.getWidth() - (scrolls ? pageView_.getScrollBarThickness() : 0);

    page->setSize (juce::jmax (1, width), scrolls ? needed : area.getHeight());
}

} // namespace tezla::ictus
