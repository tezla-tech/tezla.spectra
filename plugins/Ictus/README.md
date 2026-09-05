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
on every drum, the shared **tuning page** behind them all, and **five stereo
outputs** with an **Output** per pad, so the kit can be split across mixer
tracks (see *Outputs* below). The punch chain, humanise and the sample layer
are declared in the engine and arrive phase by phase; nothing already saved
will move when they do (parameters are append-only, CLAUDE.md §8).

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
| **Under**, Interval, Under decay, Under attack | A clean sine an interval below the landed pitch (an octave by default), locked to the body's own phase increment so it drops and sighs with it and can never beat against it, joining *after* Harmonics and Tone: the 808-under-a-hard-kick layering built in. Its own attack lets the sub bloom in behind the punch; its decay is a multiple of *Decay*. Exact off at 0. |
| **Knock**, Knock tone, Knock time | A second, lower contact resonator, 150–800 Hz with its own ring time: the beater landing on the head, which sampled kicks have and the 3 kHz click cannot give. Cut exactly after four ring times. Velocity moves it with the click. Exact off at 0. |
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
| **Ring** | The upper two modes' decay against the fundamental's, scaled by up to three times either way: −100 is a dead thud, +100 a rimshot ring. 0 is exactly the drum as measured. |
| **Thump**, Thump tone, Thump decay | A low mode under the shell, 40–200 Hz with its own decay — the shell-and-air resonance a drum has below its head's fundamental, and the body a DnB snare gets when a kick is layered under it. Not dropped with the shell, does not throw the wires. Exact off at 0. |
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

**Wires hold** keeps the wires at full level before the decay starts, 0 to
300 ms. A snare's wires do not begin dying at the instant the stick lands:
they are thrown against a head that is still moving and stay there for a
moment. Without it the only way to get a long buzz is a long decay, which
washes; with it the buzz has a length of its own and then falls at whatever
**Wires decay** says. 0 is exactly the behaviour before it existed, so no
saved project moves.

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

### The fifth round: the field, the rooms, the rattle's own controls

Every one of these is exact at its neutral setting; a project saved before
them reopens bit for bit.

- **Air stereo** and **Metal stereo** (HATS), **Wires stereo** (SNARE, GHOST),
  **Stereo** (CLAP): the drum itself makes a side signal. The metal and the
  bursts are placed left and right without touching the mid, so a mono fold is
  the old hat or clap exactly; the hiss and the wires get a second, independent
  stream on the side and their mid falls by 1 / sqrt (1 + s^2), so a channel
  holds its level and the mono fold of a full spread is 3 dB down on that
  layer -- the price of decorrelation, and the MIX page's readout shows it.
- **Width** and **Mono below**, one per pad on the MIX page: a gain on the side
  alone (100 % exact, 0 folds to mono, 200 doubles) and a second-order
  high-pass on the side (150 Hz by default, Off at 0) so the low end stays
  centred whatever is spread above it. Both grey on a pad with no side.
- **Field**, on the MIX page: the output's correlation over the last 400 ms,
  full band and under 120 Hz, with a lamp while the low band would survive a
  fold to mono.
- **Room**, **Size**, **Tone** on the kick, the snare, the ghost and the clap:
  early reflections of the pad's own mid (48 taps a side, random, falling
  30 dB), returned as mid and side. Mono under Mono below.
- **Wash** (HATS): noise driving the plate's modes for half the pad's decay;
  at 100 the plate gets the strike's energy again -- a noisier strike, a
  shimmer in the tail.
- **Head** (SNARE): the upper modes from the snare's ratios (1.6, 2.2) to a
  tom's (2.16, 3.14, the drum-physics literature's measurement). **Wires tilt**
  and **Bed**: the wires' slope about Snappy, and six resonances under them.
- **Rattle decay**, **Rattle tone**, **Tension** on a plate of their own with
  Rattle: the shell's throw on the wires can now end before the shell does
  (Shell at 0 is the old behaviour), have a corner of its own (down is a
  low, pitched chatter; up is sizzle), and lift only while the head's motion
  exceeds a threshold that rises with tension -- a train of strikes at the
  head's period that stops early and that a soft hit reaches less of.
- **Drop curve** (KICK): the drop as the exponential (0), a straight line to
  the landing (the laser, -100), or a hold-then-snap (+100).
- **Clap** and **Offset** on the SNARE page: the CLAP page's sound under the
  snare, a few milliseconds behind, at the snare's pan and through its room.
- Presets appended: Wide Kit, Room Kit, Chatter Snare, Clap Snare, Laser Kick.

### The fourth round: the hiss, and the open pad's own hold

Four more controls on the noise layer and one on the envelope, every one exact
at its neutral setting:

- **Air tilt** slopes the hiss about 6 kHz, dark to bright — a low shelf and a
  high shelf of opposite sign, 12 dB each at the ends. Air tone is a high-pass
  and could only ever thin the hiss; this can dull it. Measured on a wide-open
  hiss: centroid **5.4 kHz** dark, **10.0 kHz** flat, **12.9 kHz** bright.
- **Air attack** is the hiss's own rise, 0 to 500 ms: the metal is struck and
  the wash comes up behind it, which is what an open hat does in its first
  50 ms. Measured with a 200 ms rise: the first 20 ms fall from 0.627 to
  0.041, and at 200 ms the swelled hiss is at 0.671 where the instant one has
  fallen to 0.062.
- **Grain** thins the hiss from an event every sample to a sparse crackle of
  **300 a second** — per second, so it is the same texture at every rate — with
  the level following by p^−0.25 (measured **−15 dB** at the far end, the crest
  factor rising from 7 to 50). Through Sizzle's band-passes each event rings the
  partials: a metallic crackle rather than a click.
- **Vel > Air** lets velocity reach the hiss; it is off by default.
- **Open hold** is the open pad's own plateau, up to a second, behind a
  **Link** lamp lit by default. Lit, both pads share Hold as they always did;
  dark, the open pad holds for its own time. Measured: with a 0.5 s Open hold
  on a 0.3 s open decay the pad reads 0.296 at 50 ms and 0.297 at 400 ms;
  linked, 0.000000 at 400 ms. The closed pad's render is byte for byte the
  same either way.

### Plate — the cymbal the six pulses could never be

The six-pulse circuit is thin by construction: open its bands and you hear a
pulse chord, not a cymbal, because the source has no body that survives them.
A real 14-inch plate has a mode every 20–30 Hz — some 900 below 20 kHz — so
above 2 kHz it is structured noise and below that it has body. That density is
what a chunky hat is.

**Plate** crossfades the six pulses against a bank of **64 modes** placed by
the law the cymbal literature fits real cymbals to, `f = c (m + 7.4 n)^1.47`:
the exponent is Rossing's measured fit for a 14-inch thick cymbal (Fletcher &
Rossing, Table 20.1), the lowest mode sits exactly at **Tune**, a fixed ±1.2 %
jitter keeps every pair of modes incommensurate the way real cymbals' split
doublets do, and each mode dies at its own rate — the high ones first, by the
damping law measured on real cymbals (Ducceschi & Touzé). The strike hits every
mode at once, in phase. At 0 this is the classic hat **exactly** (the bank is
never built); at 100 it is the plate alone. To hear its body, lower **Colour**,
open **Width** and drop **Highpass** — the *Fat Hats* preset does. Measured: at
Plate 100 the energy off the six pulses' harmonic series above 4 kHz is
**−0.8 dB** against **−73 dB** for the bare pulses — it is a plate of metal, not
a chord. Costs 64 modes while it is up, about 3.5 % of a core at 192 kHz
against 2.3 % without.

### Grit — the steps of a six-bit sample path

**Grit** quantises everything before Drive, from 16 bits at 0 (exact) down to 4
at 100, geometrically, with about six bits — where the classic sampled drum
machines kept their cymbals — two thirds of the way up. It is quantisation used
as a saturator rather than a bit-crusher: the images are removed by the
oversampling, and what stays is the signal-correlated crunch that fills the gaps
between partials. Measured at 4 bits: the energy off the plate's own modes rises
from **−64.6 to −2.5 dB** while the level moves **0.13 dB**. Texture, not
volume.

### The hiss is the plate, not something beside it

On a real cymbal the sizzle is not noise added to the metal: it *is* the
metal, its own modes excited chaotically rather than struck cleanly. That is
why a sampled hat sounds like one object and an oscillator bank with noise
laid over it sounds like two things glued together.

**Sizzle** runs the noise through six band-passes at the frequencies the metal
is *heard* at — for each partial, its harmonic nearest one of the two bands —
so the hiss rings where the metal already is. (The first version rang it at the
six fundamentals, 205–800 Hz, which sit 15–30 dB under the 1.2 kHz high-pass;
measured at the default chain that cut the hiss by 16.5 dB and put none of it
on a partial. Fixed at I4.3, with the numbers in `PLAN.md`.) **Air** is its
level, **Air tone** its own high-pass, and **Air decay** its length as a
percentage of the pad's — under 100 is a chiff on the front of a long ring,
over 100 a shimmer that outlives it.

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

Velocity moves the level, the decay, Colour and the stick. **Drive** is
trimmed by the gain it adds — measured, the hat's level moves 2.7 dB across
the whole control while its peak falls by more than half.

Measured: inharmonic energy in the audible band is **−74 to −77 dB** at the
rate Auto runs, against **−12 to −17 dB** for the same six pulses generated
naively. Choosing oversampling *Off* at 48 kHz costs about 40 dB of that. The
engine costs **122 ns a sample** at 192 kHz (2.3 % of a core) as the six-pulse
hat, **183 ns** (3.5 %) with the plate up. Plate 0 and Grit 0 are the hat as it
was, byte for byte — a project saved before they existed reopens unchanged.

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
  much duller that room is than the hands filling it. Drive is trimmed by the
  gain it adds, so it buys harmonics and not loudness — measured across the
  whole control the clap's level moves 2.1 dB while its peak falls by more
  than half.
- **Gate** and **Release** are the kick's, on the clap too: a note-off fades
  the whole hit from wherever it is, so a long clap stops when the key lifts
  rather than when the room finishes.

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

The **MIX** page carries a **Pan** per pad — the first control every pad has
whether or not its own page exists yet — on a **balance law**: the near channel
stays at unity and the far one falls to nothing, so the centre is both channels
at unity, the dual mono this instrument always rendered, bit for bit, and a pad
hard left leaves the right channel exactly empty. Smoothed over 20 ms, so it can
be automated; jumped to on the first block after a project loads, so a pad saved
hard left does not drift across the field for its first hits. The fifth round
gave the pads a side of their own -- the spreads, the rooms, a **Width** and a
**Mono below** per pad and the **Field** readout, all described under the hats
above -- and the same page carries each pad's **Output**.

## Outputs — the kit across mixer tracks

Every pad has an **Output** on the MIX page: which of the instrument's five
stereo outputs -- **Main, Kick, Snare, Hats, Perc** -- it is rendered to. The
names are labels, nothing more: any pad can go to any output, and several can
share one. Every pad is on Main by default, which is the one-output instrument
every saved project was mixed against, bit for bit.

In FL Studio: open the plugin's wrapper settings and, on the *Processing* tab,
make sure the extra outputs are active (*Process inactive inputs and outputs*
if they do not appear). Then right-click the instrument's mixer track and choose
**Auto map outputs**: FL lays Kick, Snare, Hats and Perc onto the mixer tracks
following the one the plugin is on -- or set each by hand in the wrapper's
**Mixer-track offsets**. From there every drum has its own channel strip, its
own effects and its own sends: a compressor on the kick alone, a reverb on the
snare alone, a bus of hats.

What it costs, measured: an output nobody is routed to is silent and, once its
last hit has gone, skipped exactly -- with everything on Main the other four
cost nothing. Each output in use costs one more decimation: the busy eight-pad
kit at 48 kHz ×4 reads 3.1 % of a core more on five outputs than on one.
Splitting changes no sound: the five outputs summed at unity are the one-output
render to within 1.8e-15, and a drum alone on an output is bit-identical to the
same drum on Main.

**Not yet loaded in FL Studio.** How FL lays out the five outputs of a JUCE
VST3 -- which types every instrument output as a main bus -- is the one thing
that could not be checked from here. The worst case is the extra outputs not
appearing, never silence: a bus the host does not take is folded into Main.

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
sigh so the pitch holds), *Fat Hats* (the plate), and from the fourth round
*Sub Kick* (a punch with a clean octave-down sub blooming in behind it and a
knock under the click), *Thump Snare* (a low thump under the shell, the upper
pair damped, a ghost carrying a little of the same thump), *Wash Hats* (the
hiss swelling and tilted dark, the open pad holding a quarter of a second on
its own) and *Crackle Hats* (the hiss thinned to a crackle and rung hard through
the partials). A preset resets every parameter to its default first, so it is a
complete kit, not a patch over the last one.

## Building and installing

Windows: `scripts\build.bat Ictus -install` (an elevated prompt for the
install) and FL Studio finds it on the next scan. The full guide is
[`docs/BUILD.md`](../../docs/BUILD.md). The plugin builds and passes
Steinberg's validator on Linux, and the I2 kick **has been built and played on
the rig** — the first ear round is what Bass mode, Gate and Release came from — as
have the rounds through I4.4 ("amazing stuff"). The fifth round and the
outputs have not been loaded there yet.
