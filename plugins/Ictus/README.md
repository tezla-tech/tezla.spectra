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

**What ships today (phase I3): the kick and the snare.** Kick 1 on General
MIDI note 36 (C1) and Snare 1 on 38 (D1), each with its page; the same snare
engine also plays the Perc pad on 37 as a tom and Snare 2 on 40, on their
defaults until their pages arrive; a HIT button that strikes the page's pad
so a drum can be auditioned without a keyboard; five presets; the shared
header with output trim, oversampling and render quality; and, from the
first round on the rig, **Bass mode** (every key plays the kick at the key's
pitch), a **Gate** with a **Release** on both drums, and the shared **tuning
page** behind them. The hats, the clap, the punch chain, humanise, the
sample layer and the per-pad outputs are declared in the engine and arrive
phase by phase; nothing already saved will move when they do (parameters
are append-only, CLAUDE.md §8).

## The kick

Built from the mechanisms the published analyses describe, never from a
circuit's values (`docs/DSP-REFERENCES.md`, "Drum synthesis — Ictus"):

| control | what it does |
|---|---|
| **Tune** / Follow key | The pitch the kick lands on, 20–400 Hz, or the MIDI note through the tuning page's scale. With the pad on one note, Follow key is a fixed transposition — the pad only sounds on its own note; the keyboard is Bass mode's. |
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
| **Tune** / Follow key | The shell's fundamental, 60–800 Hz, or the MIDI note through the tuning. |
| **Spread**, **Tone** | Three modes at 1 : 1.6 : 2.2 of the fundamental at Spread 100 (measured 1.601 and 2.201), one tone at 0; Tone is how hard the upper two are struck, and at 0 only one mode runs. |
| **Decay** | The fundamental's ring-down; the upper modes die at 0.7 and 0.5 of it, as on the drum. The shell is cut exactly at −120 dB. |
| **Start**, **Drop** | The crack of the strike: the shell starts *Start* semitones up and glides down over *Drop*, the three modes retuned every 32 internal samples with their ring intact and never once it has landed. |
| **Body**, **Wires** | Two levels, a plain sum. |
| **Snappy**, **Snap**, Wires decay | The wires' filter corner, its shape from high-pass (open hiss) to band-pass (a pitched buzz), and the stick's burst on them. |
| **Rattle** | The shell's own motion driving the wires — the one nonlinearity kept from the physical models — so with it up the wires buzz for as long as the drum rings. Exact off at 0. |
| **Crack**, Crack tone, **Noise**, Noise time | The stick's contact: the kick's click pair. |
| Level, **Gate**, Release | Level, and the note-off fade of the whole hit (a snare has no envelope of its own). |
| Vel > Level / Wires / Crack / Drop | What velocity moves; Vel > Wires moves the wires' level and their corner together, the article's recipe. |

**Measured** (`tezla-tests snare`, `tezla-measure ictus` table 2): a Spread 0
shell an exact **200.0000 Hz** at 44.1 / 48 / 96 / 192 kHz; a 12-semitone
drop over 50 ms retunes the bank **704 times and then never again**, landing
on exactly its pitch; at Rattle 0 the hit is the shell plus the wires **bit
for bit** (a leak of one part in 10¹² is caught), at Rattle 1 the wires start
×1.79 and are still there at 100 ms where the plain burst has ended; a hit
with everything on retires at 0.60 s with **0 hits sounding**; the engine
costs **15 ns a sample** with everything on at 192 kHz, and the whole kit —
two kicks and three snares busy — **4.8–5.2 % of a core** at 48 kHz ×4.

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

*Init Kit* (the plain body), *DnB Tight*, *Sub Long*, *Jungle Snap* — each
with its own snare since I3 — and *Bass Keys* (Bass mode, gated, a 40 ms
release, no sigh so the pitch holds). A preset resets every parameter to its
default first, so it is a complete kit, not a patch over the last one.

## Building and installing

Windows: `scripts\build.bat Ictus -install` (an elevated prompt for the
install) and FL Studio finds it on the next scan. The full guide is
[`docs/BUILD.md`](../../docs/BUILD.md). The plugin builds and passes
Steinberg's validator on Linux, and the I2 kick **has been built and played on
the rig** — the first ear round is what Bass mode, Gate and Release came from.
Neither that round's build nor the snare has been loaded there yet.
