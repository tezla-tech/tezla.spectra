<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# Phonoss — vocal channel strip

**`Tzps` · "Tezla Phonoss" · `tech.tezla.Phonoss` · effect · v0.1.0**

Gate, de-esser, two compressors and tone, in the order a vocal is actually
mixed. **Phonoss** is Greek φωνή, *voice*, with a second **s** on the end for
the de-esser — which is the stage the plugin is really about, and the one it
does differently from everything else on the market.

The suite already owned both **ends** of a vocal chain and none of the middle:
character three times over (Emberdrive, Ferrite, Anvil), the ceiling
(Capstone), the checking (Transpectus). Between the microphone and the limiter
there was nothing. Phonoss is that middle, as **one strip** rather than five
plugins, because a vocal chain is genuinely a chain: the stage order, the gain
staging between stages, and one preset set that makes them work together are
the deliverable.

Where the rest of the suite goes around it: [`docs/VOCAL-CHAIN.md`](../../docs/VOCAL-CHAIN.md).

---

## The chain, and why this order

```
in → trim → HPF → GATE → DE-ESS → COMP 1 (level) → COMP 2 (peak) → EQ → trim → out
```

- **HPF first.** Proximity and rumble should never reach a detector. A plosive
  that survives to the gate holds it open; one that survives to the compressor
  ducks the whole word.
- **Gate before de-ess.** No reason to analyse the sibilance of room tone, and
  a de-esser measuring a noise floor between lines finds plenty of
  high-frequency energy in it.
- **De-ess before compression.** This is the one usually got wrong. A
  compressor that ducks on an "s" pulls down the word *after* it too, which
  makes the sibilance louder in relative terms — so a strip that compresses
  first needs a harder de-esser to repair damage it caused itself, and that is
  most of why de-essers get blamed for lisping.

  Measured, and it is the test the order rests on: with the de-esser working,
  the leveller reduces by 8.201 dB on a sibilant burst; with its Range removed,
  **8.892 dB** — 0.691 dB of extra work the de-esser was taking off it. Put the
  compressor first instead and the difference is **exactly 0.000 dB**, because
  a de-esser downstream of a compressor cannot take any work off it at all.
- **Two compressors.** A slow, low-ratio leveller then a fast peak catcher is
  how a vocal is actually compressed, and two instances of one class is less
  code than one clever one.
- **EQ last.** Tone after dynamics, so it is not fighting them: a boost before
  a compressor is an instruction to the compressor. It also means lifting the
  air shelf does not feed the sibilance detector.

**Stereo is linked.** Every detector runs once, on the larger of the two
channels, and the gain it decides is applied to both. With the right channel
6 dB down and the strip compressing 8:1 then 10:1, the two hold their 6.000 dB
offset to within **9.2e-09 dB**. Give each channel its own detector and it
becomes **0.357 dB** — the image walking a third of a decibel toward the quiet
side and back on every syllable.

---

## The distinctive piece: sibilance is a ratio, not a level

Most de-essers threshold the absolute level of a high band. That fails twice: a
loud passage re-triggers it, so it over-esses when the singer pushes; and a
bright vowel triggers it, so it lisps.

An "s" is not *loud HF* — it is **HF energy high relative to the body of the
voice**. So the detector compares two fast RMS followers and thresholds their
ratio:

```
sibilance_dB = 20 log10( rms(5 kHz and up) / rms(200 Hz .. 3 kHz) )
```

`tezla-measure phonoss`, the same sibilant burst rendered at six levels across
30 dB:

| input level | reduction on an /s/ | reduction on a vowel |
|---|---|---|
| −36 dBFS | −13.929 dB | −0.397 dB |
| −30 dBFS | −13.929 dB | −0.409 dB |
| −24 dBFS | −13.929 dB | −0.417 dB |
| −18 dBFS | −13.929 dB | −0.422 dB |
| −12 dBFS | −13.929 dB | −0.423 dB |
| −6 dBFS  | −13.929 dB | −0.424 dB |

**0.000 dB of spread across 30 dB of input.** A level-thresholded de-esser
would track the level roughly one for one — 30 dB in, 30 dB of spread. The
0.4 dB the vowel does collect is the probe's 2.4 kHz partial leaking through
the crossover's skirt, and it drifts 0.027 dB across the whole range.

The reduction is applied **subtractively**:

```
out = x − (1 − g) · highBand      g == 1 → subtract exactly 0.0 → out == x
```

so the body band is untouched *by construction* — "it does not lisp" is a
bit-exact property rather than a hope — and zero reduction is the identity
rather than something very close to it.

**Listen** monitors what is being removed. You should hear esses and very
little else; vowels in there means it is lisping.

---

## The gate: two mechanisms, two problems

A gate with one threshold chatters, because a vocal tail sits at whatever
threshold you set it to — that is how you set it.

`tezla-measure phonoss`, a 400 Hz tone centred exactly on the threshold and
wobbling ±0.5 dB at 5 Hz, transitions counted over two seconds:

| hysteresis | hold | transitions |
|---|---|---|
| 0 dB | 0 ms | **1600** |
| 0 dB | 40 ms | 20 |
| 3 dB | 0 ms | **1** |
| 3 dB | 40 ms | 1 |

So **hysteresis is the mechanism that fixes threshold-sitting, and hold is
not** — a 40 ms hold cannot bridge a 100 ms excursion. They are two settings
for two different problems: hysteresis stops the flutter, and hold is what
carries the gate across the gap inside a word, where the signal really is gone.
Measured, a 100 ms hold crosses a 60 ms gap and still shuts across a 300 ms
one.

**Range attenuates rather than mutes.** A gate that closes to silence removes
the room as well as the noise, and the result breathes in a vacuum between
lines. 10 to 20 dB is the useful setting on a vocal. Range 0 is a bit-exact
bypass of the stage.

---

## Compressors

Both are the same class: detector → soft-knee curve with a ratio →
program-dependent envelope → makeup → parallel mix, with a sidechain high-pass.
Measured ratio against asked, well above the knee:

| asked | measured | error |
|---|---|---|
| 2:1 | 1.998:1 | −0.002 |
| 4:1 | 3.987:1 | −0.013 |
| 8:1 | 7.940:1 | −0.060 |
| 16:1 | 15.744:1 | −0.256 |

**1:1 is a bit-exact bypass**, makeup at 0 dB is exact, and mix at 0.0 is the
dry path exactly.

`AUTO REL` is program-dependent release — fast for a transient, slow for a
sustained passage. On by default on the leveller, where a voice usually wants
it; off on the peak catcher, which wants a release you chose.

---

## Everything neutral is the identity

A channel strip is by definition permanently in the path, so CLAUDE.md §7's
rule bites hardest here: seven stages, each of which has to be the identity
function at its neutral setting or the strip is "nearly" transparent seven
times over.

- 40001 sample values through both channels: **40001 bit-identical, worst
  difference 0.000e+00**.
- The *Neutral* preset renders **byte-for-byte identical to a hard bypass** on
  a second of audio.
- Each stage's switch, when off, forces that stage to its own neutral value
  rather than branching around it — one definition of "off", and it is the one
  under test.

Worth stating what this rules out: setting the high-pass to 1 Hz instead of OFF
— a filter nobody could hear — opens the null against bypass to **−33.8 dB**. A
first-order pole that close to unity takes a very long time to settle. "Off
means a very low corner" is not a harmless simplification, which is why 0
removes the filter from the path entirely.

---

## Controls

**IN** — Trim, HPF (0 = off, not 1 Hz), plus peak in and peak out.

**GATE** — Threshold, Hysteresis, Range, Attack, Hold, Release, Sidechain HPF.

**DE-ESS** — Corner, Threshold (a *ratio*, −40 to +20 dB), Ratio, Knee, Range,
Attack, Release, Listen.

**LEVELLER** / **PEAK** — Threshold, Ratio, Knee, Attack, Release, Makeup, Mix,
Sidechain HPF, Auto Release.

**EQ** — Low shelf (Hz, dB), mid bell (Hz, dB, Q), high shelf (Hz, dB).

Output trim and bypass live in the header, as everywhere in the suite. Every
control has a tooltip saying what it does and what it costs.

The panel is laid out **as the chain**: six boxes left to right in signal
order, each with its own switch and its own gain-reduction bar, so the question
a strip's display exists to answer — which stage is doing the work — is
answered by looking. Underneath, full width, the de-esser's sibilance history
with its threshold drawn across it.

---

## Presets

Rap first, with sung settings included.

| preset | for |
|---|---|
| **Rap Lead** | The defaults with the gate opened up. Sits forward and stays there. |
| **Rap Ad-lib** | Harder, brighter, gated tighter — a short shout with room tone either side. |
| **Rap Double** | The layer under the lead: levelled hard, rolled off on top so it thickens rather than competes. |
| **Aggressive Forward** | Fast, flat and loud, for a take that has to survive a busy drop. |
| **Sung Verse** | Gentle and slow, gate out of the way — a sung verse has quiet phrase ends a rap gate would eat. |
| **Sung Chorus** | More levelling and weight low down, for a doubled part that has to stay one thing. |
| **Gentle Leveller** | One compressor, low ratio, nothing else. Where to start when a preset is doing too much. |
| **De-ess Only** | Everything off but the de-esser, worked hard. |
| **Gate Only** | Everything off but the gate, for cleaning a noisy take first. |
| **Neutral** | Every stage off. Bit-exact. The honest reference for any A/B. |

Every preset is a **complete parameter set**: applying one resets every control
to its default first, so nothing survives a preset change. With fifty controls
that is the difference between a preset and a trap. Verified — loading
*Aggressive Forward* then *Neutral* renders byte-identical to *Neutral* alone.

All ten peak under full scale on the render probe, the loudest being *Rap
Ad-lib* at 0.8832.

---

## Cost

`tezla-measure phonoss`, stereo, 480-sample blocks at 48 kHz:

| | % of a core |
|---|---|
| everything neutral | 0.77% |
| every stage working | 1.28% |

Eight vocal tracks — a lead, two doubles and five ad-libs — come to about 10%.

Nothing here is oversampled, and that is the correct design rather than a
saving: there is no nonlinearity in the chain to alias (CLAUDE.md §6), so
oversampling a dynamics-only path would buy latency and CPU for nothing.
**Latency is zero** and reported as zero.

---

## Status

Builds on Linux and passes Steinberg's validator **47/47**. The DSP suite passes
on x86-64. What has **not** happened: nobody has loaded it into FL Studio, or
any other DAW, from here. "Validates" means the validator — not that a host
scans it, and not that it sounds right on a rapper. That is the rig test, and
it is the acceptance test.

The qemu-aarch64 cross-check is deliberately not run; see CLAUDE.md §2.3.
