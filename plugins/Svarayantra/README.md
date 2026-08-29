# Tezla Svarayantra

<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

A SoundFont (.sf2) player with the full tezla.spectra microtuning engine —
the same 44 built-in scales, `.scl`/`.kbm` Scala files, concert pitch control
and tuning panel as Sonitus, applied to **sampled instruments**. Bohlen–Pierce
on a piano. 22 shruti on a flute. The Babylonian modes on anything.

**Name.** Sanskrit *svara-yantra* (स्वरयन्त्र), "the note-machine": *svara*
the musical note the shruti divide, *yantra* the instrument or engine, as in
Jantar Mantar. Plugin code `Tzsv`, bundle `tech.tezla.Svarayantra`.

---

## How the pitch works

A soundfont sample asserts what it holds: a recording of its root key's pitch
in 12-TET at A440 (`originalPitch`, plus correction cents and per-zone tuning
generators). The loaded **tuning** decides what frequency each MIDI key
*should* sound. Svarayantra plays every zone at

```
rate = targetHz(tuning, key) / rootHz(recording) x fileRate / hostRate
```

so the tuning is exact by construction, not a retuning approximation laid
over 12-TET. Two consequences worth knowing:

- **Concert pitch moves samples.** A432 plays every sample slower by exactly
  432/440. The A4 control, the per-scale pitch-standard lore and its Apply
  button work exactly as in Sonitus.
- **Drum zones ignore the tuning.** A zone with `scaleTuning 0` (the
  format's own "unpitched" marker, standard in drum kits) plays the
  recording untransposed on every key, whatever the scale or concert pitch —
  an unpitched drum has no pitch to move.

## The panel

- **FONT page** — load/clear the `.sf2`; the font's name, path, and the
  exact refusal reason when a file cannot be fully parsed (a soundfont that
  half-loads is never allowed to play). The preset list shows every
  `bank:program` in the font; click to choose, or send MIDI bank select
  (CC0) + program change. The host's own program list mirrors it, so FL
  Studio's preset arrows browse the font too. TRIM (dB, smoothed) and BEND
  (pitch-wheel range in semitones) knobs, and the live voice count.
- **TUNING page** — the shared microtuning panel: built-in scale menu,
  Scala loaders, the degree table with exact fractions and live Hz, each
  scale's construction, story and pitch standard.

## What the engine implements

64 voices, one per matching zone (layers and splits are zones). Per voice:
4-point Hermite resampling, the format's DAHDSR volume and modulation
envelopes in timecents/centibels, the per-zone lowpass (TPT state-variable,
skipped entirely when the font leaves it open), equal-power pan, vibrato LFO
(font depth plus up to 50 cents from the mod wheel), square-law velocity.
Exclusive-class choke through a ~10 ms quick release. Sustain pedal. Voices
retire by *activity* — a finished sample or an inaudible envelope frees its
slot immediately.

**The project saves the font's path, never its data.** A `.sf2` is tens or
hundreds of megabytes and lives on your disk like the DAW does; the tuning
(a few hundred bytes of Scala text) is embedded so the project reopens in
tune anywhere. A project whose font file is missing shows the path it
wanted and plays nothing rather than guessing.

## Measured

Resampling through the whole engine (test-built 480 Hz sine font, 48 kHz,
audible-band inharmonic energy relative to the tone):

| interval | aliasing |
|---|---|
| unison | −110.6 dB |
| up a fourth | −104.3 dB |
| up an octave | −101.4 dB |
| down a fifth | −110.9 dB |
| down an octave | −128.6 dB |

Bright-source worst cases (a 6 kHz-rich source shifted a fifth up / a fourth
down): −47.4 / −43.8 dB — the honest cost of Hermite, pinned in
`tests/test_SamplePlayer.cpp`, and the reason fonts map zones near their
root keys. A later windowed-sinc quality option is noted in the source.

CPU, one core at 48 kHz, held looped-sine voices, whole engine:

| voices | plain | filter + vibrato |
|---|---|---|
| 1 | 0.07% | 0.10% |
| 16 | 0.79% | 1.33% |
| 64 | 3.13% | 8.34% |

Reproduce both tables with `tezla-measure svarayantra`.

## Getting soundfonts

**Nothing is bundled** — this repository ships no sample data, and the test
suite builds its own fonts in memory. Free, well-made GM soundfonts to start
with (check each one's own licence before redistributing anything):

- **GeneralUser GS** (S. Christian Collins) — free, polished GM set with its
  own permissive licence text.
- **FluidR3_GM** (Frank Wen) — MIT licensed, the classic.
- **MuseScore_General** — MIT, FluidR3-derived, maintained for MuseScore.

## Honest limitations (v0.1.0)

- 16-bit sample data only: the optional 24-bit `sm24` chunk and compressed
  SF3 variants are not read.
- The two default modulators that matter are built in (velocity → level,
  mod wheel → vibrato); custom `pmod`/`imod` modulator lists are ignored.
- No modulation LFO (font tremolo/auto-wah), no chorus/reverb sends — dry
  output only; use the suite's effects after it.
- One preset sounds at a time (not multi-timbral), and no channel-10 drum
  convention: load a drum preset explicitly.
- Stereo-linked sample pairs play as their two zones with the pan the font
  gives them.

Fidelity notes: silence in → exact zeros out; the output does not depend on
the host's buffer size (verified bit-for-bit against off-grid block sizes
with vibrato running); every continuous control is smoothed.
