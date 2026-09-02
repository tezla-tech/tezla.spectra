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

**What ships today (phase I2): the kick.** One pad, one page, General MIDI
note 36 (C1), a HIT button to audition it without a keyboard, four presets,
the shared header with output trim, oversampling and render quality. The
rest of the instrument is declared in the engine and arrives phase by phase;
nothing already saved will move when it does (parameters are append-only,
CLAUDE.md §8).

## The kick

Built from the mechanisms the published analyses describe, never from a
circuit's values (`docs/DSP-REFERENCES.md`, "Drum synthesis — Ictus"):

| control | what it does |
|---|---|
| **Tune** / Follow key | The pitch the kick lands on, 20–400 Hz, or the MIDI note. |
| **Start**, **Drop** | The punch: the body starts *Start* semitones above and drops over *Drop* ms. The TR-808 analysis's fast attack jump; the 909's Tune Depth and Tune Decay. |
| **Sigh**, Sigh time | A second, slow drop through the decay — the 808's pitch sigh, the "couple of semitones" a real shell relaxes by. |
| **Phase** | Where the body starts its cycle. 90° is a full-scale step: a click by construction. |
| **Harmonics**, Even | Overtones added in parallel, odd or even curves through ADAA; no linear term, so nothing but harmonics is added. Bit-exact off at 0. |
| Tone on, **Tone** | A low-pass that tracks the pitch — bright attack, pure tail. Off is exact. |
| **Click**, Click tone, **Noise**, Noise time | The beater: a 3 ms resonant mode plus a seeded noise burst — the 909's Attack. |
| Attack, Hold, **Decay**, Shape | The amplitude. A hit retires *exactly* when the decay lands. |
| **Tail**, Tail time | A longer envelope on the same body so the landed pitch rings on. |
| Level, Vel > Level / Click / Drop / Decay | Level and what velocity moves. |

Every knob is read when the hit starts and held for its whole length. Every
stage renders at the internal (oversampled) rate, so the click and the
harmonics never alias, and the block size cannot bend a sweep: 64-, 97- and
512-sample blocks are bit-identical.

**Measured** (`tezla-measure ictus`, `tezla-tests kick`): the pitch within
**0.016 cents** of the ideal curve at 44.1 / 48 / 96 / 192 kHz, 1.95 µs of
spread between rates; the plain body **bit-exact** against sin × envelope; a
retrigger's largest step equal to a single hit's own (0.0347 against a cut's
0.8163); two kicks with everything on **4–7 % of a core** at 48 kHz ×4; idle
0.001 %. The declared latency is the measured one: 24 / 32 / 36 samples at
×2 / ×4 / ×8, 0 with oversampling off.

## Presets

*Init Kit* (the plain body), *DnB Tight*, *Sub Long*, *Jungle Snap*. A preset
resets every parameter to its default first, so it is a complete kit, not a
patch over the last one.

## Building and installing

Windows: `scripts\build.bat Ictus -install` (an elevated prompt for the
install) and FL Studio finds it on the next scan. The full guide is
[`docs/BUILD.md`](../../docs/BUILD.md). The plugin builds and passes
Steinberg's validator on Linux; **nobody has loaded it in FL Studio yet** —
that is the point of this phase, and the ear round after it decides what the
kick becomes before the snare exists.
