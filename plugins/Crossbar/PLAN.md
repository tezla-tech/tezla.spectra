# Crossbar — the telephone tone instrument

**Code `Tzcb` · "Tezla Crossbar" · `tech.tezla.Crossbar` · instrument.**

The crossbar switch is the electromechanical matrix that connected one line to
another in a telephone exchange from the 1930s onward: a grid of horizontal and
vertical bars, and a call is the point where one of each crosses. DTMF is the
same idea moved into the audio band — **four row tones and four column tones,
and a key is the pair that crosses**. The name is the mechanism and the
encoding at once.

## The thesis

Every tone a telephone network ever made, playable from a keyboard, through a
line that sounds like a line.

Two halves, and both matter:

1. **The tones are the standards, exactly.** DTMF's sixteen pairs from ITU-T
   Q.23, the Bell System Precise Tone Plan's dial/busy/ringback/reorder, the
   BT set for the United Kingdom, the receiver-off-hook howler, the three-tone
   intercept SIT, the fax and modem answer tones. Numbers, not approximations
   — a DTMF pair a real decoder would reject is a bug here.
2. **The line is the character.** A telephone did not sound like a telephone
   because of the tones; it sounded like one because of what the network did
   to them — 300–3400 Hz of bandwidth, 8000 samples a second, and eight bits
   of *companded* PCM whose quantisation noise rides up and down with the
   signal. That chain is the second half of this plugin and it is where the
   sound actually comes from.

For this rig: DTMF melodies over a beat, a dial tone as a drone, the howler as
a riser, a dialled number as a rhythmic phrase, and the LINE section used on
its own as a lo-fi colour on anything routed into it later.

## The instrument

**Drum-sampler layout.** One key, one tone, laid out from a movable root
(default C1 = MIDI 36):

| offset | keys | what |
|---|---|---|
| 0–11 | C1–B1 | the keypad: `1 2 3 4 5 6 7 8 9 * 0 #` — one octave *is* the phone |
| 12–15 | C2–D#2 | DTMF `A B C D`, the fourth column (1633 Hz) |
| 16–27 | E2–D#3 | call progress: dial, busy, ringback, congestion, unobtainable, howler, call waiting, SIT, fax CNG, modem CED, SF 2600, rotary pulse |
| 28–35 | E3–B3 | the eight DTMF constituent frequencies alone, for sound design |
| 36 | C4 | dial the stored number |

**A tone is a program**, not a waveform: a list of steps, each holding up to
four simultaneous frequencies for a stated time, looping or not. A DTMF digit
is one endless step; a busy tone is two steps of half a second; the UK
congestion tone is four steps with the second burst 6 dB louder because BT
specified it that way; the SIT is three ascending steps and then silence. One
mechanism, every tone in the book, and the cadences are stated in seconds so
they are identical at 44.1, 48, 96 and 192 kHz by construction.

**A simple ADSR holds them**, as asked — `dsp::Adsr`, the same envelope
Sonitus and Malleus use.

**The dialler** takes a number typed into the editor and plays it: each digit
for its own time, with the inter-digit gap, in **Tone** (DTMF) or **Pulse**
(rotary, 10 pulses per second, break/make clicks) mode. That is the "all the
phone numbers" half of the request, and it is one key.

## The line

```
voices -> level -> + noise -> BAND -> RATE -> CODEC -> out
```

in that order because that is the order the network does it: the local loop
band-limits and adds its own hiss, the channel bank samples what is left, and
the codec quantises the samples.

- **BAND** — `Off` · toll 300–3400 (G.712) · wideband 50–7000 (G.722) ·
  handset · speaker. Fourth-order edges, coefficients from the actual rate.
- **RATE** — `Off` down to 1 kHz, with 8 kHz (G.711) the default and 16 kHz
  (G.722) named. Sample-and-hold through the existing `dsp::Downsampler`.
- **CODEC** — `Off` · μ-law · A-law · Linear. The two companding laws are
  **G.711 proper**: eight segments, sixteen steps each, reconstruction at the
  interval midpoint. Not a smooth log curve — the segments are what the codec
  actually is, and the flat signal-to-noise ratio across 40 dB of level is the
  audible consequence.
- **Bits** — 1 to 16. In a companding mode it masks the low bits *of the code
  word*, which is exactly what a T1 span did when it stole them for
  signalling: 7-bit μ-law is a real thing that real networks sounded like.
- **Noise** — line hiss, exactly zero at zero, band-limited with everything
  else because that is where it comes from.

**This section is the documented aliasing exception** (CLAUDE.md §7): bit
crushing and rate reduction run at the host rate with no oversampling and no
ADAA, because the folded images are the sound. Everything upstream is pure
sine tones, which have no harmonics to alias in the first place — so the
instrument oversamples nowhere and a test says why.

## Phases

Each phase is one commit: tests written and run in the same commit, every
mechanism seen red (or break-checked), numbers quoted, whole tree built, "the
qemu-aarch64 cross-check was not run" noted per §2.3.

- **D0 — plan, registry claim `Tzcb`, references.** This file with its
  Continuity section, `plugins/README.md`, the DSP-REFERENCES rows for Q.23,
  the Precise Tone Plan, the BT set, SIT and G.711, and CLAUDE.md's active-plan
  pointer.
- **D1 — `Dsp/ToneTables.hpp`.** The 4×4 DTMF matrix, both regional
  call-progress sets as tone programs, the constituent frequencies, and the key
  map. Pure data and lookup, no state. Tests: every DTMF pair is exactly its
  Q.23 frequencies; every program's period is the documented one; the key map
  is total and injective over its range; the two regions differ only where
  documented.
- **D2 — `shared/tezla-dsp/Companding.hpp`** plus `Bitcrusher::setBits`. G.711
  μ-law and A-law as an eight-segment quantiser. Tests: reconstruction lands on
  the interval midpoint for every one of the 256 code words; **the SNR is flat
  across 40 dB of level while a linear 8-bit quantiser's tracks the level**
  (the defining property, break-checked by swapping one in); robbed bits
  coarsen exactly; `Off` and 16-bit linear are bit-exact identity.
- **D3 — `Dsp/ToneVoice.hpp` + `Dsp/CrossbarEngine.hpp`.** The sine bank, the
  program clock, the envelope, sixteen voices, the dialler, and the LINE chain.
  Tests: a DTMF key measures two peaks and only two, each within a fraction of
  a Hz of Q.23; twist measured; cadence timing identical at four sample rates;
  block-size independence; silence in is exact silence out; **voices measurably
  die** (activity, not silence — the Sonitus lesson); LINE with everything off
  is bit-exact identity end to end; the dialler's digit and gap timing.
- **D4 — JUCE layer.** Schema-v1 parameters, choice lists append-only from
  birth, the dial string as a state property (not a parameter — strings are
  not automatable), MIDI, and presets.
- **D5 — editor.** The panel is a **telephone keypad**, because that is what
  the user asked for and it is also the honest picture of the instrument: press
  a key and the two crossing frequencies light up. Plus the dial field, the
  ADSR, the LINE controls, and the note map printed so the drum layout is
  discoverable without the manual.
- **D6 — close-out.** `tezla-measure crossbar` (DTMF frequency accuracy against
  Q.23, companding SNR against level for both laws, the band responses, cadence
  timing across rates, CPU), `plugins/Crossbar/README.md`, registry flip,
  validator 47/47 on all ten bundles, docs current.

## Risks

- **The tone numbers are the product.** A wrong cadence or a mistyped frequency
  is not a subtle DSP bug, it is a wrong instrument, and no amount of listening
  finds it. Mitigation: every number is pinned by test against the published
  figure in the same commit that introduces it, with the source named in the
  test comment.
- **G.711's segment structure is easy to get subtly wrong** and a subtle error
  sounds like a slightly different lo-fi, not like a defect. Mitigation: the
  structure is derived (the segment ends are `(64 << s) - 1`, which is just
  where the octaves fall) and then pinned three ways — midpoint reconstruction
  for all 256 codes, the flat-SNR property against a linear quantiser, and the
  standard's own full-scale ceilings.
- **Scope.** This is meant to be simple. The line is drawn at: no reverb, no
  room, no speech codec beyond G.711, no external sidechain, no modulation
  layer. If the LINE section turns out to be wanted on other material it
  becomes its own effect later, not a second mode here.

Syrinx is **paused at V2** for this and resumes after D6. Sonitus P4 and Prism
remain parked. This plan does not touch them.

## Continuity — how any session resumes this work

This section is the handoff. It is updated **in the same commit as each
phase**, so whichever assistant session picks the work up — after a context
loss, a model change, or a fresh clone — needs nothing beyond this file and
CLAUDE.md.

**Phase status** (flip `pending` → `done` in the phase's own commit):

| phase | status |
|---|---|
| D0 plan + registry + references | done |
| D1 ToneTables | pending |
| D2 Companding + setBits | pending |
| D3 voice + engine + dialler + line | pending |
| D4 JUCE layer + presets | pending |
| D5 editor — the keypad | pending |
| D6 close-out | pending |

**To resume**: read CLAUDE.md in full, then this file; take the first `pending`
phase. The non-negotiables, in one place:

- One phase = one commit. Tests written and RUN in that commit; every
  mechanism seen red first or break-checked (edit → red → revert), with the
  measured numbers pinned in the test comments and quoted in the commit
  message.
- Build the whole tree before pushing (`./scripts/build.sh NONE --test` or the
  cmake equivalent with no `--target`), run all tests, and run Steinberg's
  validator on any plugin whose bundle changed.
- The qemu-aarch64 cross-check is **deliberately not run** (CLAUDE.md §2.3
  gate); say so in every commit message rather than implying coverage.
- New first-party files carry the six-line licence header copied from a
  neighbour. No model identifiers in anything pushed. The commit footer comes
  from the harness — use whatever the current session mandates.
- Every published telephony number is attributed at the point of use AND in
  `docs/DSP-REFERENCES.md` (CLAUDE.md §9). These are *standards*, which is the
  case §9 says to take rather than derive: measurement cannot tell you that
  941 Hz should have been 940.
- Setters that clear or re-aim state carry no-op guards (`dsp::isExactly`).
  Continuous parameters are smoothed; discrete switches crossfade. Silence in →
  exact zeros out. Voices must measurably die.
- The prior art to copy patterns from: Malleus (`plugins/Malleus/`) for the
  instrument shape, voice manager and phase discipline; Ferrite for the
  `ControlPage` grid; Sonitus for polyphony; Capstone/Ferrite editors for the
  panel.
