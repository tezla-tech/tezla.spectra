# Tezla Crossbar

Every tone a telephone network ever made, playable from a keyboard, through a
line that sounds like a line.

**Code `Tzcb` · vendor `Tzla` · bundle `tech.tezla.Crossbar` · schema v1 ·
instrument**

Build plan and phase history: [`PLAN.md`](PLAN.md).

A **crossbar switch** is the electromechanical matrix that connected one line
to another in a telephone exchange from the 1930s onward: horizontal bars,
vertical bars, and a call is the point where one of each crosses. DTMF is the
same idea moved into the audio band — four row tones, four column tones, and a
key is the pair that crosses. The name is the mechanism and the encoding at
once, and the panel draws it.

---

## The two halves

### 1. The tones are the standards, exactly

Not approximations of a telephone: the published figures, taken deliberately.
CLAUDE.md §9 says derive by default and copy only what measurement cannot
check, and this is the clearest case of the exception in the repository — **no
measurement can tell you that 941 Hz should have been 940.**

Measured at the engine's output, all sixteen DTMF pairs land on their ITU-T
Q.23 frequencies with a **worst error of 0.000 Hz over all 32 tones**, against
the 1.5% a real receiver tolerates.

| set | what it gives |
|---|---|
| **DTMF** (ITU-T Q.23) | rows 697 / 770 / 852 / 941 Hz, columns 1209 / 1336 / 1477 / 1633 Hz. All sixteen keys including A B C D, the fourth column that never appeared on a domestic telephone |
| **North America** (Bell Precise Tone Plan) | dial 350+440 · ringback 440+480 at 2 s on / 4 s off · busy 480+620 at 0.5/0.5 · reorder the same pair at 0.25/0.25 |
| **United Kingdom** (BT) | dial 350+450, whose 100 Hz beat is what people mean by "British dial tone" · engaged 400 at 0.375/0.375 · the double ring 0.4 / 0.2 / 0.4 / 2.0 · number unobtainable, continuous 400 |
| **Both** | the receiver-off-hook howler, 1400+2060+2450+2600 at 0.1/0.1 · the three-tone intercept SIT · fax CNG 1100 · modem CED 2100 · the 2600 Hz supervision tone · a rotary dial's loop-break clicks |

The Precise Tone Plan's own level differences are honoured, so a busy tone
really is quieter than a dial tone: **0.00 / −6.00 / −11.00 dB**, from its
−13 / −19 / −24 dBm. BT's congestion tone steps exactly 6.00 dB louder on its
second burst, because BT specified it that way.

Cadences are stated in seconds, so they are the same at every session rate.
Measured:

| host rate | busy burst | busy period | UK ring gap |
|---|---|---|---|
| 44 100 Hz | 0.4965 s | 0.9999 s | 0.2013 s |
| 48 000 Hz | 0.4965 s | 0.9999 s | 0.2013 s |
| 96 000 Hz | 0.4965 s | 0.9999 s | 0.2013 s |
| 192 000 Hz | 0.4965 s | 0.9999 s | 0.2013 s |

Identical to four decimal places at all four. The 3.5 ms the burst falls short
of half a second is the gate's fade at each end, and it is the same 3.5 ms
everywhere.

### 2. The line is where the sound comes from

A telephone did not sound like a telephone because of its tones. It sounded
like one because of what the network did to them.

```
voices → level → + noise → BAND → RATE → CODEC → out
```

in that order, because that is the order the network does it. **Band-limiting
before rate reduction is not a detail — it is the anti-alias filter**, and it
is why real telephone audio is grubby rather than crunchy. Turn BAND off and
leave RATE at 8 kHz and the images come back:

| setting | image at 2523 Hz from a 1477 Hz tone |
|---|---|
| rate off | −164.4 dB |
| 4 kHz | −6.7 dB |

That is CLAUDE.md §7's documented aliasing exception, asserted rather than
assumed. Everything upstream is pure sines, which have no harmonics to fold —
measured, the loudest thing anywhere but the two tones of a held key is
**−109.5 dB**, which is the analysis window's own skirt. So the instrument
oversamples nowhere.

#### Eight bits, and why they are not a bit crusher

"8 bit, 8 kHz" is the usual answer for what an old phone sounded like, and
taken literally it is wrong in a way you can hear. G.711's eight bits are
**logarithmic**: the quantisation step grows with the signal, so the noise
rides up and down with it and the ratio between them stays put. A linear 8-bit
quantiser's noise floor is fixed, so it is fine at the top and unusable in a
decay — which is the fizz of a sampler, not the grubbiness of a line.

Measured on a 997 Hz sine, signal-to-noise against level:

| level | μ-law | A-law | linear 8-bit |
|---|---|---|---|
| 0 dBFS | 38.92 dB | 38.99 dB | 50.02 dB |
| −10 dBFS | 36.92 | 36.39 | 39.75 |
| −20 dBFS | 38.06 | 38.39 | 29.78 |
| −30 dBFS | 34.65 | 36.75 | 20.61 |
| −40 dBFS | 35.30 | 34.20 | 11.11 |

**Spread over the 40 dB: 4.27 dB companded, 38.91 dB linear.** That gap is the
whole difference, and both are on the Codec control so you can hear it.

The few decibels of ripple on the companded curves is the segment structure
showing, not error: G.711 approximates the log curve with eight straight
pieces, so the ratio dips slightly wherever a peak sits just above a segment
boundary. A smooth log law would not ripple, and would not be G.711.

#### The controls

| control | what it does |
|---|---|
| **Band** | Off · toll 300–3400 (G.712) · wideband 50–7000 (G.722) · handset 500–2800 · speaker 700–2200. Fourth-order at both edges: the toll band reads −0.001 dB at 941 Hz and −0.456 dB at 2600, where a Butterworth 2600/3400 of the way to its corner should be −0.48 |
| **Rate** | Off down to 1 kHz, with 8 kHz (G.711) the default and 16 kHz (G.722) named. Sample-and-hold, at the host rate, aliasing included |
| **Codec** | Off · μ-law · A-law · Linear. The first three are exact; Linear is the comparison |
| **Bits** | 1–16. In a companding mode this masks the low bits of the code word, which is what a T1 span did when it stole them for signalling — **7-bit μ-law is a sound the network really made**. About 7 dB per bit |
| **Line noise** | Hiss on the loop, band-limited with everything else. Exactly zero at zero; −35 dBFS at one |

---

## Playing it

**Drum-sampler layout**, from a movable root (default C1 = MIDI 36):

| offset | what |
|---|---|
| 0–11 | the keypad: `1 2 3 4 5 6 7 8 9 * 0 #` — **one octave is the phone** |
| 12–15 | DTMF `A B C D` |
| 16–27 | dial, busy, ringback, congestion, unobtainable, howler, call waiting, SIT, fax, modem, 2600, rotary pulse |
| 28–35 | the eight DTMF frequencies on their own, for sound design |
| 36 | dial the stored number |

Thirty-seven keys. Anything outside the range is silent rather than wrapped.

**Cadence** decides how a key relates to the tone: *From key* starts the
cadence when you press; *Free running* joins one already in progress, as an
exchange does — a key pressed a quarter of a second late gets 0.2465 s of first
burst where from-key gets 0.4965; *Steady* removes the cadence entirely, so a
busy tone becomes a drone and the call-progress row becomes a chord.

**Twist** is ITU-T Q.24's level difference between a DTMF pair's high and low
tones. Real transmitters send +2 dB to survive line loss; a receiver must
accept 8 dB of it. It moves the balance and never the loudness — the pair's
gains always sum to exactly 1.0, so no key can clip.

**The dialler** plays a written number: type it however you like, because
spaces, dashes and brackets are skipped rather than dialled. In **Tone** mode
every digit is the same length. In **Pulse** mode a rotary dial breaks the loop
ten times a second, so **a digit lasts as long as it is worth** — measured, '1'
takes exactly 4800 samples at 48 kHz and '0' takes 48000, a ratio of ten. That
is why short emergency numbers were chosen.

The number lives with the project but is **not a parameter**: a phone number is
text, and there is no honest way to automate one.

## The panel

The keypad, with the row frequencies down the left and the column frequencies
along the top. Press a key and the key, its row and its column all light — the
encoding drawn rather than described. Every pad has its own tooltip carrying
its frequencies, its cadence and the MIDI note that plays it.

Beside it, the twelve call-progress tones on the same grid, relabelled by
region: switch to the UK set and BUSY becomes ENGAGED, REORDER becomes CONGEST,
and the long intercept becomes N.U.

## Presets

*Handset* (G.711 as it comes) · *Clean* · *Payphone* · *Bad line* · *Wideband*
(G.722) · *Speakerphone* · *Answering machine* (A-law) · *Rotary* ·
*Held tones* · *Crunch* · *Idle reference*

*Clean* is the genuinely transparent one §7 asks for: codec, rate and band all
off is **bit-exact identity**, not a transparent-sounding approximation of it.
*Crunch* is the deliberate opposite — no band limit in front of the rate
reduction, so the images fold back — and *Idle reference* is named for what it
does rather than what would read better in a list: nothing played is bit-exact
zero, and a key through it is 40 dB down. Quiet, not silent.

A preset never changes the dial string. A phone number is content, not a
setting.

## Cost

Measured at 48 kHz, as a percentage of one core:

| | |
|---|---|
| one tone, line off | 0.18% |
| sixteen tones, line off | 1.52% |
| sixteen tones, full line (toll, 8 kHz, μ-law, hiss) | 1.87% |
| idle, full line | 0.23% |

A voice whose key is up and whose envelope has reached zero is retired and
contributes bit-exact zero. Measured: sixteen voices are gone 0.0200 s after
note off at a 20 ms release and 0.2000 s at 200 ms — **a ratio of exactly
10.00**, which is activity asserted rather than silence (the Sonitus lesson
that cost the user a pinned CPU meter).

## Measuring it

```
tezla-measure crossbar [--fs 48000]
```

DTMF accuracy against Q.23, what else is in the output, cadence timing at four
sample rates, the codec's SNR against level for both laws and a linear
quantiser, the band edges, the deliberate image, and CPU.

## Sourcing, honestly

Every figure here is a published standard, attributed at the point of use and
in `docs/DSP-REFERENCES.md`. **None of the primary documents could be fetched**
from the container this was built in — the ITU-T texts, the Bell practice and
BT's specification are all refused by the egress proxy — so they arrived
through secondary technical sources that agree with one another. That is a
weaker claim than "the standard says", and DSP-REFERENCES states it in those
words, along with the two figures worth re-checking first if the primary
sources become reachable.

Nothing is taken from any commercial plugin, sample library or recording. A
telephone tone *is* its specification, which is the only way this could have
been done anyway.

## What has not been verified

This plugin has never been loaded into a DAW. It builds, it passes Steinberg's
validator 47/47, its DSP is measured — and nobody has yet played it on the rig
it was written for. That is the acceptance test, and it has not happened.
