<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# Ictus — the drum synthesiser for drum and bass

**Code `Tzic` · "Tezla Ictus" · `tech.tezla.Ictus` · instrument · in progress.**

*Ictus* is Latin for the stroke, the blow — and in music, the beat itself.
A drum synthesiser built for the rig's drum and bass: purpose-built engines
for the kick, snare, hats and clap, a punch chain on every pad, per-hit
humanise and velocity, a sample layer, and per-pad outputs for FL Studio's
*Auto map outputs*. The plan, with every source and every measured number,
is [`PLAN.md`](PLAN.md).

**What ships today (phase I4): the kick, the snare, the ghost snare, both
hats and the clap.** Kick 1 on General MIDI note 36 (C1), Snare 1 on 38
(D1), the **ghost snare** on 40 (E1), the **closed hat** on 42 (F#1), the
**open hat** on 46 (A#1) and the **clap** on 39 (D#1), each with its page;
the same snare engine also plays the Perc pad on 37 as a tom on its defaults
until its page arrives; a pad strip that lights on every hit and opens a
pad's page; a HIT button that strikes the selected pad so a drum can be
auditioned without a keyboard; six presets; the shared header with output
trim, oversampling and render quality; **Bass mode** (every key plays the
kick at the key's pitch), **Note snap** (a drum's Tune lands on the tuning's
nearest note, for tuning drums to a bass line), a **Gate** with a **Release**
on every drum, and the shared **tuning page** behind them all. The punch
chain, humanise, the sample layer and the per-pad outputs are declared in the
engine and arrive phase by phase; nothing already saved will move when they
do (parameters are append-only, CLAUDE.md §8).

## The kick

Built from the mechanisms the published analyses describe, never from a
circuit's values (`docs/DSP-REFERENCES.md`, "Drum synthesis — Ictus"):

| control | what it does |
|---|---|
| **Tune** / Follow key / Note | The pitch the kick lands on, 20–400 Hz, or the MIDI note through the tuning page's scale. With the pad on one note, Follow key is a fixed transposition — the pad only sounds on its own note; the keyboard is Bass mode's. **Note** snaps Tune to the nearest note of the tuning (12-TET until a scale is loaded), so the kick sits in the key of the bass line; its tooltip names the note it lands on. |
| **Start**, **Drop** | The punch: the body starts *Start* semitones above and drops over *Drop* ms. The TR-808 analysis's fast attack jump; the 909's Tune Depth and Tune Decay. |
| **Sigh**, Sigh time | A second, slow drop through the decay — the 808's pitch sigh, the "couple of semitones" a real shell relaxes by. |
| **Phase** | Where the body starts its cycle. 90° is a full-scale step: a click by construction. |
| **Harmonics**, Even | Overtones added in parallel, odd or even curves through ADAA; no linear term, so nothing but harmonics is added. Bit-exact off at 0. |
| Tone on, **Tone** | A low-pass that tracks the pitch — bright attack, pure tail. Off is exact. |
| **Click**, Click tone, **Noise**, Noise time | The beater: a 3 ms resonant mode plus a seeded noise burst — the 909's Attack. |
| Attack, Hold, **Decay**, Shape | The amplitude. A hit retires *exactly* when the decay lands. |
| **Tail**, Tail time | A longer envelope on the same body so the landed pitch rings on. |
| **Gate**, Release | Lit, a note-off releases the hit from wherever its envelope is, over *Release* (0–2 s), so a fast fill never piles each tail onto the next. Release 0 is a 1 ms cut — the shortest that does not click. Dark, the pad is a one-shot that ignores note-off; the HIT button always plays the whole hit. |
| Level, Vel > Level / Click / Drop / Decay | Level and what velocity moves. |

Every knob is read when the hit starts and held for its whole length. Every
stage renders at the internal (oversampled) rate, so the click and the
harmonics never alias, and the block size cannot bend a sweep: 64-, 97- and
512-sample blocks are bit-identical.

## The snare

Built from Reid's published analysis of the instrument, never from a
product (`docs/DSP-REFERENCES.md`): two quasi-harmonic series plus the (0,1)
pair that decays far faster, wires as noise under an envelope, and velocity
into the noise filter's cutoff and the tuned/noise mix.

| control | what it does |
|---|---|
| **Tune** / Follow key / Note | The shell's fundamental, 60–800 Hz, or the MIDI note through the tuning; **Note** snaps it to the tuning's nearest note, the upper modes keeping their ratios to it. |
| **Spread**, **Tone** | Three modes at 1 : 1.6 : 2.2 of the fundamental at Spread 100 (measured 1.601 and 2.201), one tone at 0; Tone is how hard the upper two are struck, and at 0 only one mode runs. |
| **Decay** | The fundamental's ring-down; the upper modes die at 0.7 and 0.5 of it, as on the drum. The shell is cut exactly at −120 dB. |
| **Start**, **Drop** | The crack of the strike: the shell starts *Start* semitones up and glides down over *Drop*, the three modes retuned every 32 internal samples with their ring intact and never once it has landed. |
| **Body**, **Wires** | Two levels, a plain sum. |
| **Snappy**, **Shape**, Wires decay | The wires' filter corner, its shape from high-pass (open hiss) to band-pass (a pitched buzz), and the stick's burst on them. |
| **Rattle** | The shell's own motion driving the wires — the one nonlinearity kept from the physical models — so with it up the wires buzz for as long as the drum rings. Exact off at 0. |
| **Crack**, Crack tone, **Noise**, Noise time | The stick's contact: the kick's click pair. |
| Level, **Gate**, Release | Level, and the note-off fade of the whole hit (a snare has no envelope of its own). |
| Vel > Level / Wires / Crack / Drop | What velocity moves; Vel > Wires moves the wires' level and their corner together, the article's recipe. |

**Note snap, measured** (`tezla-tests note_snap`): a kick at 52 Hz lands on
G#1, 51.913 Hz; a snare at 205 Hz on G#3, 207.652 Hz; with a five-tone scale
loaded the same kick lands on that scale's degree at 55.000 Hz instead —
the snap is the tuning's, so a drum snapped in a microtuned project sits in
it.

**Measured** (`tezla-tests snare`, `tezla-measure ictus` table 2): a Spread 0
shell an exact **200.0000 Hz** at 44.1 / 48 / 96 / 192 kHz; a 12-semitone
drop over 50 ms retunes the bank **704 times and then never again**, landing
on exactly its pitch; at Rattle 0 the hit is the shell plus the wires **bit
for bit** (a leak of one part in 10¹² is caught), at Rattle 1 the wires start
×1.79 and are still there at 100 ms where the plain burst has ended; a hit
with everything on retires at 0.60 s with **0 hits sounding**; the engine
costs **15 ns a sample** with everything on at 192 kHz, and the whole kit —
two kicks and three snares busy — **4.8–5.2 % of a core** at 48 kHz ×4.

## The ghost snare

In a break the ghost notes are the quiet hits between the backbeats, the
drummer's left hand, and they are what gives a roller its shuffle. In
programmed drum and bass they are usually a separate, quieter, shorter snare
placed on the sixteenths around the main hit — so here the ghost is a pad of
its own, on E1 by default, with the full snare engine and its own page.
**LINK**, lit by default, makes it the main snare's drum: Tune, Follow key,
Note snap, Spread, Tone, Snappy and Shape follow SNARE and grey on the ghost's
page, and only the stroke is the ghost's — Decay, Start, Drop, Body, Wires,
Wires decay, Rattle, the crack, Level, Gate and velocity. Its defaults are a
ghost's: shorter, mostly wire, well under the main hit. Dark, it is any
second snare you like. The three kits each carry a ghost.

## The hats

One pair of cymbals, struck two ways. The closed pad (F#1) and the open one
(A#1) share every control but their decay, because they are the same metal.

A cymbal has no harmonic series — its partials are set by the shape of a
stiff, irregular plate — so the engine is **six band-limited pulses at
deliberately incommensurate ratios**, summed and then filtered rather than
tuned. That alone is a sparse comb and sounds like a metallic chord; five
more things make it a cymbal.

**Tune** moves the whole set. **Harmonics** slides along a list of ratio
tables and morphs *between* them, geometrically and rank by rank, so a partial
glides from one table's place to the next rather than jumping:

| set | what it is |
|---|---|
| **Metal** | the six oscillator frequencies of a classic analogue cymbal circuit, taken from the published analysis (attributed in `HatEngine.hpp` and `docs/DSP-REFERENCES.md`). Tight, dense, the ordinary closed hat. |
| **Bell** | near-harmonic, so it rings with a pitch — a ride's bell. |
| **Trash** | wider and deliberately incommensurate: a china, a lid. |
| **Wide** | spread over three octaves. Thin, bright, glassy. |

Four more positions on the knob are reserved, so a set can be added later
without moving anything already saved. **Spread** pulls the six apart along a
fixed pattern that sums to zero, so the set loosens without moving.

### What makes it lush rather than thin

- **Ring** multiplies the low three oscillators by the high three. A ring
  modulator's output holds the sum *and* difference of every pair of harmonics
  in its inputs, so one multiply turns two sparse combs into a dense
  inharmonic wash — which is what a plate of metal actually sounds like.
  Measured: a bare bank has **−77 dB** of its energy off its own harmonic
  series; at Ring 100 it is **−0.7 dB**. Both operands are low-passed at a
  fixed 20 kHz first, so no product can reach Nyquist and it cannot alias at
  any setting.
- **Drive** is the overdrive the Nord Modular cymbal patch ends with, as an
  antialiased soft clip over the metal and the hiss together. It fills the
  gaps between partials and glues the two layers into one instrument.
- **Damp** closes a low-pass as the hit decays. On a real cymbal the high
  modes die first, and that fall from bright to dark is most of what "lush"
  means. Measured: at Damp 100 the corner runs from **17.9 kHz at the strike
  to 2.2 kHz at 200 ms**; at 0 the filter is out of the path entirely.
- **Strike** is the stick — a short loud transient over the body envelope.
  With Damp up it is automatically the brightest part of the hit.
- **Hold** and **Shape** give the envelope a plateau and a curve, exponential
  through to linear.

### The hiss is the plate, not something beside it

On a real cymbal the sizzle is not noise added to the metal: it *is* the
metal, its own modes excited chaotically rather than struck cleanly. That is
why a sampled hat sounds like one object and an oscillator bank with noise
laid over it sounds like two things glued together.

**Sizzle** runs the noise through a band-pass at each of the six partials, so
the hiss rings at the frequencies the metal already has. Measured as the share
of the hiss landing within a semitone of a partial: **16.6 % at Sizzle 0,
40.9 % at 100**. **Air** is its level, **Air tone** its own high-pass, and
**Air decay** its length as a percentage of the pad's — under 100 is a chiff
on the front of a long ring, over 100 a shimmer that outlives it.

**Colour** is the lower of two band-passes; the upper follows at 2.06 times it
— the spacing of the two bands in the analysed circuit — with **Width** from a
narrow whistle to a band wide enough to hear the whole plate, and **Highpass**
taking off what should not be there underneath. The page's picture draws every
partial's whole harmonic series against those bands, because nothing you hear
is a fundamental.

**Choke**, lit by default, is the foot on the pedal: a closed hit fades
whatever the open pad is ringing over 5 ms. **Gate** and **Release** are the
kick's, on both pads at once: with Gate lit a note-off fades the whole hit —
metal, hiss and the filters' ring — so a long open hat can be stopped by
lifting the key rather than by waiting for a closed hit.

Velocity moves the level, the decay, Colour and the stick.

Measured: inharmonic energy in the audible band is **−74 to −77 dB** at the
rate Auto runs, against **−12 to −17 dB** for the same six pulses generated
naively. Choosing oversampling *Off* at 48 kHz costs about 40 dB of that. The
engine costs **112 ns a sample** at 192 kHz, 2.2 % of a core.

## The clap

A hand clap is not one event: several people clap at almost the same time, so
the ear hears a handful of bursts a few milliseconds apart and then the room
answering all of them at once. The engine is that structure — the recipe from
the Nord Modular percussion chapter, read first-hand.

- **Bursts** is how many, two to six. Two is a pair of hands; six is a room.
- **Flam** is the first gap, **Skew** whether they crowd together or spread
  out as they go — each gap a fixed fraction of the one before. 0 is exactly
  even, which is the one thing real people never manage.
- **Snap** is how fast each burst falls: a slap you can count, or a smear.
- **Noise** and **Noise tone** are the hiss and its own high-pass.
- **Body**, **Pitch** and **Ring** are the part the first version had none of.
  Cupped hands are a cavity and it rings, which is what gives a real clap a
  pitch under the hiss: three inharmonic modes struck by every burst. Measured
  to land within 0.3 Hz of where Pitch puts them.
- **Colour**, **Width**, **Tail**, **Tail tone** and **Drive** place the whole
  thing: the band it is heard through, the room after the last burst, and how
  much duller that room is than the hands filling it.

The bursts are counted in samples, converted once at note-on, so the pattern
lands on the same instants at 44.1 and at 192 kHz — measured to the sample.
The engine costs **16 ns a sample** at 192 kHz.

Humanising the spacing, which is what makes a real clap different every time,
arrives with every other pad's humanise control at I6.

## The panel

Each page is a set of plates, one per group, each with its own colour and a
spine down its left edge: pitch or shell on the vermilion, then the wires or
colour, the strike or click, the amplitude, and velocity. The control a group
is about is drawn larger; the ones set once and left are drawn smaller.
Four pictures are drawn from the knobs themselves, so they are right before
the first hit: the kick's pitch trajectory on log axes with the tuning's
notes as a ruler and the landed note named; the snare's three modes as bars
with the drop's start ghosted; an envelope for each drum, computed with the
engine's own envelope; and the wires' filter response from the engine's own
filter. The strip along the top is the eight pads: each names its drum and
its note (with the MIDI number, since DAWs disagree on octave names), lights
when struck as bright as the hit was hard, and opens its page; HIT strikes
whichever pad is selected. With Note snap lit, a Tune knob reads as the note
it lands on.

## Bass mode and the tuning page

**BASS**, in the strip, turns the instrument into a tuned sub-bass made of the
kick: every key plays Kick 1 at the key's pitch, the other pads are silent,
and Kick 1's own note is no longer special. With **Gate** lit the note ends
when the key lifts, over *Release*; a bass line played legato releases only
the key that lifted, never the one still held. The pitch comes through the
**TUNING** page — the same microtuning panel Sonitus, Svarayantra and Malleus
carry: built-in scales, Scala `.scl` and `.kbm` files, concert pitch, the
degree table with its Hz column. It travels with the project as text. A kick
is a sine that lands on a pitch, so a just fifth against the bass locks where
a tempered one beats.

The BASS lamp's and the Key switch's tooltips are live: they name the scale
in force and what C1, C2 and G2 play through it, in Hz.

**Measured** (`tezla-measure ictus`, `tezla-tests kick`): the pitch within
**0.016 cents** of the ideal curve at 44.1 / 48 / 96 / 192 kHz, 1.95 µs of
spread between rates; the plain body **bit-exact** against sin × envelope; a
retrigger's largest step equal to a single hit's own (0.0347 against a cut's
0.8163); two kicks with everything on **4–7 % of a core** at 48 kHz ×4; idle
0.001 %. The declared latency is the measured one: 24 / 32 / 36 samples at
×2 / ×4 / ×8, 0 with oversampling off. Bass mode lands notes 36 / 43 / 48 on
65.406 / 97.999 / 130.813 Hz, within **0.001 cents** of the tuning; a gated
hit with a 50 ms release is exactly silent 51 ms after the note-off with its
largest step equal to the body's own (0.0133), and Release 0 is gone 2.3 ms
after the note-off (the 1 ms cut plus the decimator's delay) with the same
step; a note-off after the hit has landed changes nothing, bit for bit.

## Presets

*Init Kit* (the plain body), *DnB Tight*, *Sub Long*, *Jungle Snap* — each a
kit now, with its own snare, ghost, hats and clap — then *Lush Hats* (a long
open hat with the noise rung through the metal, damped and gated, and a soft
wide clap under it) and *Bass Keys* (Bass mode, gated, a 40 ms release, no
sigh so the pitch holds). A preset resets every parameter to its default
first, so it is a complete kit, not a patch over the last one.

## Building and installing

Windows: `scripts\build.bat Ictus -install` (an elevated prompt for the
install) and FL Studio finds it on the next scan. The full guide is
[`docs/BUILD.md`](../../docs/BUILD.md). The plugin builds and passes
Steinberg's validator on Linux, and the I2 kick **has been built and played on
the rig** — the first ear round is what Bass mode, Gate and Release came from.
Nothing since that round has been loaded there: not I2.1, not the snare, not
the ghost, not the hats or the clap.
