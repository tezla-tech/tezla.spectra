# Tezla Anvil

A valve amplifier, the speaker it is driving, and the microphone in front of
it — modelled from the mechanisms that produce the sound rather than fitted to
a curve.

**Code `Tzan` · vendor `Tzla` · bundle `tech.tezla.Anvil` · schema v1**

---

## The thesis

**The character is in the time constants and the load, not the transfer
curve.**

A waveshaper, however carefully drawn, does the same thing to the hundredth
chord as to the first, and the same thing to a low E as to a lead line two
octaves up. A valve amplifier does neither, and every mechanism in this plugin
is one of the reasons why:

| mechanism | time constant | what it does |
|---|---|---|
| cathode bias shift | ~33 ms | the operating point moves under load |
| grid conduction | ~22 ms | the coupling capacitor charges and blocks |
| supply sag | 20–55 ms | the rail falls and the headroom with it |
| **transformer core flux** | — | saturation that depends on **pitch** |
| speaker impedance | — | the amplifier's own tone control |

The fourth is the one worth the trouble.

### Flux is the integral of voltage

A transformer core carries magnetic flux, and flux is the integral of the
voltage across the winding. For a sine of amplitude *A* at frequency *f*, the
peak of that integral is *A*/(2π*f*) — **inversely proportional to pitch**. Two
notes at identical voltage an octave apart put twice as much flux into the core
for the lower one. A low E saturates the transformer while a lead line sails
through it untouched, and the amplifier gets woollier as you play lower at the
same volume.

Nothing in the code tests the frequency. Integrate the voltage, let the
permeability fall as the flux rises, and the behaviour falls out. Measured on
this rig with the valves held bit-exactly linear, so nothing else could be
responsible:

| | 40 Hz | 80 Hz | 160 Hz | 320 Hz | 640 Hz | 1280 Hz |
|---|---|---|---|---|---|---|
| flux | 1.269 | 0.902 | 0.487 | 0.248 | 0.125 | 0.062 |
| THD | −20.1 | −32.0 | −47.8 | −66.0 | −83.8 | −102.2 dB |

The flux falls **6 dB per octave**, as an integral must. The distortion it
produces falls about **18 dB per octave** — the permeability term goes as the
square of the flux, and the corner it moves is itself further from the note each
time. Five octaves separate a filthy low E from a lead line that is beyond
clean, at exactly the same voltage.

The literature models the output transformer as *linear* — Cohen and Hélie's
DAFx-10 power amplifier says so explicitly — so `Core` can be taken to its
minimum and you get exactly that, which is what the addition is measured
against.

---

## The chain

```
in ──┬─────────────────────────────────────────── dry (BypassMixer) ──┐
     │                                                                │
     └── input trim                                                   │
           │   [ oversampled ×1/×2/×4/×8 ]                            │
           │     TriodeStage × 1..5     cascaded preamp valves        │
           │       (tone stack inserted where the voicing says)       │
           │     ToneStack              passive FMV, solved as a      │
           │                            circuit rather than as an EQ  │
           │     master                 an attenuator, as it is       │
           │     TriodeStage            the phase inverter            │
           │     PowerAmp               class AB, sag, feedback with  │
           │                            presence and resonance, and   │
           │                            the transformer               │
           │     SpeakerLoad            the impedance divider         │
           │     Cabinet                driver, box and microphone    │
           │   [ /oversampled ]                                       ▼
           └────────────────────────────── output trim ──────────► out
```

Everything linear that follows the nonlinearities is inside the oversampled
section with them, because the speaker load and the cabinet are the shape the
power amplifier is driving into.

### Where the tone stack goes, and why it is not a detail

A passive tone stack in front of the distortion and one behind it are different
instruments. In front, it decides what *gets* distorted — scooping the mids
before three cascaded valves is the entire modern high-gain sound. Behind, it
only decides what you hear of what was already made. Real amplifiers put it in
different places for exactly this reason, and the three voicings follow suit.

---

## The three voicings

| | valves | stack after | feedback | NFB | sag | core | stack |
|---|---|---|---|---|---|---|---|
| **Clean** | 1 | 1 | 0.85 | 5.3 dB | 10% / 25 ms | 32 Hz | American |
| **Vintage** | 2 | 1 | 0.70 | 4.6 dB | 32% / 55 ms | 58 Hz | British |
| **Modern** | 3 | 2 | 0.88 | 5.5 dB | 16% / 20 ms | 40 Hz | Modern |

The feedback column is the **loop gain**, and it sets the ceiling on what
Presence and Resonance can do — see below. These lanes shipped at 0.60, 0.15
and 0.32, which is 4.1, 1.2 and 2.4 dB of negative feedback, and a user
reported both controls as inaudible. They were right, and the numbers say why.

`Gain` means the same thing on each — every lane carries an input scale that
makes it so. THD at 220 Hz, −12 dBFS in, cabinet off, master at its default:

| lane | −6 dB | 0 dB | +6 dB | +12 dB | +24 dB |
|---|---|---|---|---|---|
| clean | −56.6 | −50.1 | −43.6 | −38.1 | −26.2 |
| vintage | −38.3 | −40.1 | −28.9 | −13.0 | −8.2 |
| modern | −19.8 | −11.1 | −9.1 | −7.0 | −2.6 |

**The clean lane is not a quieter version of the dirty one.** CLAUDE.md's second
priority asks for a setting that genuinely gets out of the way, and at the
bottom of its range this one is 56 dB down and still falling.

---

## The controls

### AMP

| control | range | what it does |
|---|---|---|
| **Voicing** | 3 | which amplifier this is — see above |
| **Gain** | −6 … +48 dB | how hard the preamp valves are driven |
| **Master** | −40 … 0 dB | how hard the output stage is driven |
| **Output** | ±24 dB | trim, after everything |
| **Bass / Middle / Treble** | 0–100% | the passive tone stack |
| **Mix** | 0–100% | dry against wet, dry delayed to match |

**Master is an attenuator**, because that is what a master volume is: a
potentiometer between the preamp and the phase inverter. It cannot amplify.
Turning it up does not add gain, it stops removing it — and what then distorts
is the inverter and the output valves. Measured on the clean lane at 36 dB of
gain, as the spacing between the second and third harmonics:

| master | −36 | −24 | −12 | −6 | 0 dB |
|---|---|---|---|---|---|
| h2 − h3 | 16.73 | 16.76 | 15.88 | 12.24 | 1.91 dB |

That is a valve running out of swing, not something getting louder.

**Mix at 0 is bit-exact.** The oversampler's round trip is a whole number of
base-rate samples by design, so the dry path is an integer delay — no passband
ripple, no filter, nothing.

### CAB

| control | range | what it does |
|---|---|---|
| **Cabinet** | None / Combo 1×12 / British 4×12 / Vintage 4×12 | the acoustic model |
| **Mic Position** | Cap … Edge | across the cone |
| **Mic Distance** | 2–50 cm | how far back |
| **Damping** | 0.2–20 | the amplifier's grip on the speaker |

**Damping is the part almost every amp simulation leaves out.** A loudspeaker is
8 Ω at one frequency and nothing like it anywhere else: 93 Ω at its 75 Hz
resonance, 6.6 Ω at 400 Hz, 40 Ω at 10 kHz. A solid-state amplifier holds its
voltage regardless. A valve amplifier's output impedance is a large fraction of
the load, so the two form a divider that the speaker's own impedance curve
shapes:

| damping | 41 Hz | 75 Hz | 400 Hz | 3 kHz | 8 kHz |
|---|---|---|---|---|---|
| 0.5 | +2.19 | +7.67 | −1.63 | +4.48 | +6.68 dB |
| 1.0 | +1.69 | +4.81 | −1.35 | +3.17 | +4.35 |
| 3.0 | +0.83 | +1.92 | −0.77 | +1.40 | +1.79 |
| 20 | +0.15 | +0.31 | −0.16 | +0.24 | +0.29 |

Nine decibels of tone shaping, and the only thing changing between the rows is
how stiffly the amplifier holds its voltage. This is why a valve amp into a
resistive load box sounds wrong in a way no cabinet impulse response fixes: the
impulse response is measured at the speaker terminals, and *this* is what
decides what arrives there.

**The two microphone controls do not overlap.** Position sweeps the top by ten
decibels and leaves 100 Hz where it was; Distance moves 100 Hz by nine decibels
and leaves everything above 4 kHz alone to three decimal places.

### CHARACTER

| control | range | what it does |
|---|---|---|
| **Core** | 20–400 Hz | where a full swing fills the transformer |
| **Presence** | 0–100% | shunts the highs out of the feedback |
| **Resonance** | 0–100% | shunts the lows out of the feedback |
| **Sag** | 0–200% | scales the voicing's rail droop |
| **Stages** | Stock / +1 / +2 | extra cascaded valves |
| **Oversampling** | Auto / Off / ×2 / ×4 / ×8 | see below |

**Core is the control this plugin exists for**, and for music that lives under
60 Hz it is the one that matters. Push it up and everything below it saturates
the transformer and blooms while the midrange stays exactly where it was. The
range goes far past any transformer ever wound, deliberately.

**Presence and Resonance are feedback controls, not tone controls.** The control
on the back of an amplifier is a capacitor from the feedback tap to ground. It
boosts nothing — it removes the loop's correction up top, so the output stage's
own gain *and its own distortion* show through. That is why it sounds nothing
like a treble control even when the curves look similar: turning it up makes the
amplifier **less linear** in the top, not louder there.

Measured, presence at 90% into a loop of 0.6 at 3× drive:

| | 100 Hz | 5 kHz |
|---|---|---|
| level | +0.05 dB | +2.99 dB |
| THD | — | **+16.59 dB** (−49.5 → −32.9) |

Three decibels of level, and sixteen and a half of distortion. A treble control
can do the first and cannot do the second, and it is the second you hear.
Resonance at 90% is the mirror: +2.66 dB at 60 Hz, and 5 kHz unchanged to two
decimal places.

### How far either can reach, and why it is 5.6 dB

**A shunt can only give back what the loop was taking away.** Both controls work
by removing the feedback at one end of the spectrum, so the negative feedback
the loop applies — `20·log10(1 + loopGain)` — *is* the control's entire
authority. Nothing about the shunt itself can exceed it.

That is what made both controls inaudible in the first release. The lanes
carried loop gains of 0.60, 0.15 and 0.32, so presence had 4.1, 1.2 and 2.4 dB
to work with and measured 3.7, 1.0 and 2.0 — doing exactly what it should, with
nothing to do it to. The loops are now set just under the stability bound and
the same measurement reads 4.9, 3.8 and 5.2 dB, with resonance at 4.4, 3.6 and
4.3. `tezla-measure anvil` prints the table.

**5.6 dB is a ceiling, not a choice.** The loop carries a one-sample delay —
`y[n] = A·x[n] − b·y[n−1]` — so its pole sits at `−b` and it is stable only
below a loop gain of 1. Past that the saturator is the only thing in the way,
which CLAUDE.md §7 is explicit is not a bound: measured, the stage begins
alternating at Nyquist by a loop gain of 2.4 and reaches 1e82 within 8000
samples at 3.0. `PowerAmp::kMaximumLoopGain` holds it at 0.9, and a test sweeps
past it and asks for the oscillation by name.

A real amplifier gets 10–20 dB of negative feedback because its loop is
continuous rather than delayed. Matching that means solving the loop implicitly
through two stateful ADAA shapers, which is a project of its own rather than a
patch, and it is the obvious next thing to do to this stage.

Setting the loops deeper cost a little of each lane's dirt, because that is what
negative feedback is for: vintage's THD at +12 dB of gain went from −10.7 to
−13.0 dB. Every lane's *level* is unchanged — the makeup trims were
recalibrated, and all three land within 0.05 dB of where they were.

Resonance is the same trick at the other end, and with a core that saturates on
flux it is a great deal — it is the control that makes a low note bloom.

---

## Oversampling: Anvil's Auto is not the suite's

**Auto here targets about 384 kHz internally, where the rest of the suite
targets 192.** That is a measured departure from CLAUDE.md §6's table, not a
preference.

The house figure was set for a plugin with one shaper in the path. This one has
up to five cascaded valve stages plus an output stage, and a cascade compounds:
each stage distorts the harmonics the last one made, so the energy reaching the
internal Nyquist is far greater than any single shaper produces.

Measured at maximum gain with five valves, **worst of a sweep from 82 Hz to
4.4 kHz**, absolute dBFS:

| lane | off | ×2 | ×4 | ×8 |
|---|---|---|---|---|
| clean | −43.0 | −52.0 | −66.2 | **−88.9** |
| vintage | −33.1 | −41.3 | −43.5 | **−63.4** |
| modern | −27.5 | −35.7 | −44.2 | **−62.4** |

CLAUDE.md §7 asks for nothing above −60 dBFS at maximum drive. Only ×8 delivers
that, so that is what Auto picks. The priority order in §1 puts fidelity above
CPU in as many words, and this is the case it was written for.

| host rate | Auto | effective | latency |
|---|---|---|---|
| 44.1 / 48 kHz | ×8 | 353–384 kHz | 71 samples |
| 88.2 / 96 kHz | ×4 | 353–384 kHz | 63 samples |
| 176.4 / 192 kHz | ×2 | 353–384 kHz | 47 samples |

CPU, one stereo instance at 48 kHz: ×1 3.9%, ×2 8.8%, ×4 17.3%, ×8 37.0% of one
core. The manual settings mean exactly what they say, so anyone who would rather
have the CPU back can take ×4 and know what it costs.

### The sweep is the point

A single 1 kHz probe reads ×4 at −68.7 dBFS and flatters it by twenty decibels.
The worst case is always the highest probe frequency, which is what one would
expect and what a single low probe cannot see.

**And the probe must not divide the host rate.** 1500 Hz at 48 kHz is bin-exact
*and* a divisor, so every alias of it lands on one of its own harmonics and is
scored as a harmonic. The same amplifier at the same setting:

```
 4400.39 Hz  ->   -46.5 dBFS
 1500.00 Hz  ->  -190.3 dBFS   <- 48000/1500 = 32
```

144 dB of difference, entirely from the instrument. `tezla-measure anvil`
prints both, and a test asserts the gap, so the aliasing figure cannot quietly
go blind.

---

## What the panel shows

The display is a **WorkingMeter**, not a gain-reduction meter, because none of
what this plugin does is a level:

- **SAG** — how far the rail has fallen under load. This is the compression
  people hear as bloom and attribute to the speaker.
- **FLUX** — how full the transformer's core is, marked at 1.0 where it gives
  way. Play a low note and a high one at the same level: the low one fills it
  twice as far.
- **BIAS** — how far the first valve's operating point has drifted, in knees.
  This is what makes the hundredth chord sound unlike the first.

---

## Presets

| preset | for |
|---|---|
| Bypass reference (bit-exact) | the thing to A/B everything else against |
| Clean: pedal platform | genuinely clean, 56 dB down |
| Vintage: cranked | everything up, no master, output stage doing the work |
| Modern: rhythm | scooped, tight, articulate at high gain |
| **Sub bass: transformer bloom** | core at 220 Hz, cabinet off, mix 45% |
| Reese: grind | modern lane for articulation, mix 70% |
| **Drum bus: glue and weight** | almost no gain, master near the top — sag and transformer, not distortion |
| Modern: five valves | the loudest, dirtiest thing it does |
| Vintage: edge, backed off | room rather than grille |

The two in bold are why this plugin exists on a dubstep rig rather than a
guitarist's.

---

## Not an impulse response

The cabinet is synthesised from the mechanisms, never captured. CLAUDE.md §2.1:
a captured IR of a commercial cabinet is that cabinet's measured property, and
shipping one means shipping somebody else's product.

It costs accuracy against any one named cabinet and buys three things an IR
cannot give: controls that move continuously and mean something physical, a
response that stays correct at every sample rate, and no legal question.

Five mechanisms — the enclosure's alignment, the rear radiation for an open
back, cone breakup, the driver's own top from cone mass and voice-coil
inductance, and the microphone. Three voicings, dust cap at 5 cm, dB relative to
each one's own level at 1750 Hz:

| | 40 | 100 | 200 | 500 | 1.2k | 1.75k | 2.5k | 5k | 8k |
|---|---|---|---|---|---|---|---|---|---|
| combo | −20.5 | −9.5 | −5.9 | −5.5 | −2.6 | 0.0 | +1.4 | −5.9 | −18.2 |
| modern | −16.7 | −2.2 | −0.2 | −6.0 | −4.7 | 0.0 | −3.5 | −11.7 | −23.7 |
| vintage | −14.0 | −0.6 | +1.1 | −2.7 | −0.0 | 0.0 | −1.9 | −13.6 | −25.9 |

The open back's cancellation **reaches a floor**, and that is the detail most
open-back models get wrong. A full dipole falls at 6 dB/octave for ever; a real
back is only partly open, so the rear radiation that escapes sets a floor and
the result is a shelf. Modelled as a highpass instead it reads 45 dB down at
25 Hz against the shelf's 21 — which is why open backs are usually simulated
with far too little bottom end.

---

## Verification

| check | result |
|---|---|
| Steinberg validator | **47/47** |
| DSP tests | 416 pass on x86-64 **and on ARM64 under emulation**, identically |
| Aliasing at maximum drive, swept | −62.4 dBFS worst lane, spec is −60 |
| Auto at 44.1 / 48 / 96 kHz | all under −60 dBFS |
| Block-size independence, controls sweeping | < 1e-9 between 64 and 512 |
| Bypass | bit-exact and latency-matched |
| Mix at 0 | bit-exact, delayed by the reported latency |
| Silence in | exactly zero out, all three lanes |
| Parameter sweep, every extreme | worst peak 1.166 |

**What is not verified:** nobody has loaded this into a DAW. It is developed in
a Linux container, so "the validator passes" means the VST3 interface is correct
and the DSP measures correctly — not that FL Studio scans it, not that Logic
loads the AU, and not that it sounds right. The last of those is the user's ears
and is the acceptance test that matters.

---

## Sources

Everything is recorded in [`../../docs/DSP-REFERENCES.md`](../../docs/DSP-REFERENCES.md)
with its licence and whether it was actually read. Nothing here was reverse
engineered from any product, and no impulse response, curve, preset or parameter
table was taken from one.
