# Tezla Malleus

A percussion synthesiser that makes **objects** and hits them — and then does
the thing no physical modeller does: lets the tuning reach inside the object
and retune its overtones.

**Code `Tzml` · vendor `Tzla` · bundle `tech.tezla.Malleus` · schema v1 ·
instrument**

Build plan and phase history: [`PLAN.md`](PLAN.md).

The malleus is the hammer — the middle-ear bone that strikes the **incus**,
which is Latin for *anvil*. This suite already ships Anvil. This is the bone
that hits it, and the bone you hear with.

---

## The thesis

Every other synth on this rig makes a waveform and filters it. Malleus has no
waveform at all: a voice is a **bank of resonant modes**, and its spectrum
comes from geometry and material. Striking it excites those modes; the pitch
you hear is one of them.

That much is ordinary modal synthesis. The flagship is what the tuning engine
does to it:

**Overtone Lock quantises the object's own partials onto the loaded scale.**

A bell's hum, tierce and quint are not in any key — they are where the
founder's profile put them. Turn Overtone Lock up with Bohlen–Pierce loaded
and every one of them lands on a tritave degree; measured worst case over the
first 32 partials of a stretched bell:

| scale | lock 0 | lock 1 |
|---|---|---|
| 12-TET | 49.30 cents off | 0.0000 |
| Bohlen–Pierce | 68.97 cents off | 0.0000 |
| 5-TET | 120.00 cents off | 0.0000 |

Bohlen–Pierce has no octave at all, and the lattice still works, because the
lock walks the scale's own repeat interval rather than assuming 2/1. That is
the point of the whole instrument: **the object agrees with the scale instead
of fighting it.**

## The object

Mode ratio tables are **derived from the physics, not copied** (CLAUDE.md
§2.1), and pinned by test against the figures every acoustics text prints:

| material | where the ratios come from | first partials |
|---|---|---|
| String | the harmonic series | 1, 2, 3, 4 |
| Bar | roots of cos x · cosh x = 1, root-found here | 1, 2.756, 5.404, 8.933 |
| Membrane | zeros of the Bessel functions J_m, computed here | 1, 1.593, 2.136, 2.295 |
| Plate | (m²/2 + n²), aspect √2, closed form | 1, 2, 3, 3.667 |
| Bell | the canonical minor-third profile (empirical; see DSP-REFERENCES) | 0.5, 1, 1.2, 1.5 |

**Material** morphs continuously across them in log-frequency — the integer
positions are the pure tables to the bit, so a project saved on a pure
material reopens sounding identical. **Stretch** applies a power law on top,
taking the object past anything physical. **Position** weights each mode by
sin(kπp), so striking the middle of a bar puts every even mode on a node and
they get *exactly* nothing.

A partial that would land above 0.45 × the sample rate is **dropped, not
folded**. Combined with a strike injected in closed form, that is why this
instrument oversamples nowhere: measured on a 64-partial inharmonic bar at
maximum hardness, the geometric midpoints between modes sit **92 to 155 dB**
below the modes themselves.

## How it is hit

| exciter | what it is |
|---|---|
| **Mallet** | the Hann contact pulse's own spectrum, \|sinc(u)/(1−u²)\|, injected per mode. Hardness runs the contact from 8 ms of felt to 0.15 ms of brass — measured centroid 155 Hz to 1438 Hz |
| **Pluck** | a displacement start: the classic sin(kπp)/k² series, audibly darker than any mallet (centroid 111 Hz against the softest mallet's 155 Hz) |
| **Roll** | a bouncing-ball clock — each interval is the last times a ratio, floored at a buzz. A dropped mallet, seeded so two hits differ and a take replays exactly |
| **Bow** | regularised stick-slip friction, which sustains on *any* object here: bowed bells, and impossibly, bowed membranes |

**Drop** glides the whole object's tension per hit, through a retune that
preserves the ring — the membrane physics behind an 808's drop and a tabla's
gliss, rather than a pitch envelope on a sine.

## What it rings through

A **vactrol low-pass gate**, modelled from the component: an LED flashing on a
CdS cell whose dark decay is not exponential (measured half-life 27.8 ms while
bright against 87.4 ms once dark — the signature), with cutoff and gain
coupled, so a closing gate darkens as it quietens. That coupling is why a
pinged LPG reads as a struck object rather than a chopped one: at a held 0.35
a 6 kHz tone passes at 0.057 of the open level where a plain VCA would pass
0.350.

The gate is an amplitude envelope as well as a filter, so **the audible note
is shorter than the object's ring** — measured at about 0.47× the Decay
setting. That shortening is the west-coast ping, and the Decay tooltip says so
rather than implying otherwise.

Alongside it, a **sympathetic bank**: up to twelve strings tuned to degrees of
the same scale, not played but excited by the object. Fed pitchless noise, the
weakest degree rings **857×** louder than the loudest gap between degrees.
Drone feeds them back into themselves — a feedback loop around a
nonlinearity, so it carries the full §7 kit (soft clip inside the loop, a cap
below unity, a swept test with teeth) and cannot start from silence.

## Phase 2 — the object answers back, and you can hear it from somewhere

Three things separated a *mode bank* from an *instrument*: everything in it
was linear, undamped by the player, and heard from one point.

**Bloom** couples the modes, so a hard strike **builds after contact** — the
shimmer a tam-tam grows a second in is energy climbing out of the low modes,
not reverb and not a filter. The physics is the geometric (von Kármán)
nonlinearity, whose quadratic term couples mode triads and whose rate goes as
amplitude squared. It is a feedback loop around a nonlinearity and carries the
full §7 kit: the coupling passes through `q/(1+|q|)`, under 1 for every finite
input, and the whole bank is renormalised to the energy the linear bank would
have had — so the cascade *redistributes* energy and cannot create any. Swept
across 1260 combinations, worst sample 4.39 and the ring's energy still
falling.

Late high-band share of a struck plate against the velocity it was hit at:

| velocity | voice peak | Bloom 1 | Bloom 0 |
|---|---|---|---|
| 0.10 | 0.0002 | 0.0007 | 0.0005 |
| 0.25 | 0.0026 | 0.0145 | 0.0036 |
| 0.55 | 0.0138 | 0.1909 | 0.0124 |
| 0.85 | 0.0330 | 0.9773 | 0.0160 |
| 1.00 | 0.0458 | 0.9874 | 0.0168 |

The linear column barely moves; the bloom column climbs sixty-fold. **This is
a hit-it-hard control**, which is what a large-displacement nonlinearity is,
and its useful window is about 10 dB wide — past it the injection swamps the
state rather than perturbing it. That window is *placed* rather than widened,
because widening it means redesigning the bound.

It was also **inert until the knob went on the panel**. Calibrated against a
unit test that strikes a bare bank at twenty times a voice's amplitude, the
control moved the fourth decimal place of the plugin's output and nothing
else. The constant that fixes it is now calibrated on a voice, against
velocity, and `tezla-measure malleus` prints the sweep that decided it.

**Damp** is a hand on the object, and it is *played* rather than set — pushed
while a note rings, it changes the ring. The loss is proportional to
**frequency**, because soft tissue is a constant-loss-factor absorber, so the
object goes **dull before it goes quiet**. A flat decay multiplier would be a
volume pedal. Measured T60 on modes with a 4 s natural decay:

| Hz | damp 0 | 0.25 | 0.50 | 1.00 |
|---|---|---|---|---|
| 125 | 4.008 | 1.792 | 1.152 | 0.680 |
| 250 | 4.004 | 1.148 | 0.672 | 0.368 |
| 500 | 4.002 | 0.670 | 0.366 | 0.194 |
| 1000 | 4.001 | 0.365 | 0.192 | 0.099 |
| 2000 | 4.000 | 0.191 | 0.099 | 0.050 |

Each doubling of pitch roughly halves the time, which is the whole claim.

**Two exciters and a blend.** A real strike is a contact *and* a scrape: a
mallet with a fingernail on it, a bow started with a pluck. The blend is a
lerp on the excitation amounts, so either end is bit for bit the single
exciter, and setting both slots the same is that exciter at every position.
Measured on a mallet blended into a pluck at 220 Hz, the strike's spectral
centroid walks **390 Hz to 227 Hz** across the control.

**Velocity picks the hardness**, because on a real drum a soft hit is felt and
a hard hit is stick — the same mallet compresses differently. At full amount
velocity *is* the hardness: the strike's centroid runs **216 Hz at velocity
0.1 to 542 Hz at 1.0**, agreeing exactly with the same note played with the
knob set to the velocity.

**Two listening positions.** The strike is combed by sin(kπp); so is the ear,
by the same law. Two taps at two points on the object is stereo from the
geometry rather than a widener — and the mono sum genuinely cancels, which is
measured rather than avoided:

| L / R | correlation | mono keeps |
|---|---|---|
| 0.05 / 0.95 | −0.359 | 0.566 |
| 0.20 / 0.80 | −0.280 | 0.600 |
| 0.29 / 0.71 | −0.059 | 0.686 |
| 0.45 / 0.55 | +0.857 | 0.964 |

**Width and mono compatibility trade off directly** and nothing escapes it. At
*matched* width an asymmetric pair survives better: 0.10/0.75 and 0.20/0.80
are equally wide and keep 0.641 against 0.600, which is why the tooltip says
offset rather than mirror. Listening amount at 0 is the mono instrument that
shipped, bit for bit.

## Presets

*Init* · *Physical 808* · *Tabla Drop* · *Neuro Stab* · *Slendro Gongs* ·
*BP Bell Choir* · *Bowed Bowl Drone* · *Sitar Cloud* · *Glass Marimba* ·
*Dropped Mallet* · *Tam-tam Bloom* · *Choked Cymbal* · *Fingernail Marimba* ·
*Plucked Bow* · *Idle reference*

The last four are phase 2's, one per feature: *Tam-tam Bloom* wants to be hit
hard, *Choked Cymbal* wants Damp under your left hand or on a pedal,
*Fingernail Marimba* is a mallet with a pluck blended into it, and *Plucked
Bow* is how a bowed string is actually begun. All four are genuinely stereo —
channel correlations +0.76, +0.25, +0.68, +0.67 against +1.0000 for every
phase-1 preset.

Measuring them found **Bowed Bowl Drone at 1.945 of full scale**, nearly +6 dB
over: a bow sustains and the taraf's drone adds to it for as long as the key
is held. Trimmed to −12 dB, it reads 0.489. Every preset now peaks between
0.43 and 0.83 on a single note.

Two of them are named for scales you should load first: Slendro Gongs and BP
Bell Choir are built around Overtone Lock, and a preset does not change your
tuning — a scale outlives the patch you are auditioning.

*Idle reference* is named for what it does rather than what would read better
in a list: with nothing played the output is bit-exact zero (measured through
the real plugin, 192000 samples, both idle and with twelve strings droning at
full coupling), but a note through it is 60 dB down. Quiet, not silent.

## Cost

Measured at 48 kHz, as a percentage of one core:

| | |
|---|---|
| one struck voice, 64 partials | 1.5% |
| 16 struck voices, 64 partials | 12.1% |
| 16 bowed voices, 64 partials | 17.1% |
| 16 bowed + 12 sympathetic strings | 17.3% |

A voice whose key is up and whose vactrol has gone dark is **retired**: it
contributes bit-exact zero and costs one branch. The test asserts the count
reaches zero and that the death time tracks the Decay control — activity, not
silence, because a silent zombie passes every silence test while pinning the
CPU (the lesson Sonitus taught this workshop the hard way).

## Measuring it

```
tezla-measure malleus [--fs 48000] [--out modes.csv]
```

Mode tables (all 64, to CSV), Overtone Lock accuracy per scale, decay
accuracy, strike centroid against hardness, the bow's onset map across the
pressure × speed plane, the between-modes floor, phase 2's three tables
(Bloom against velocity, the damping law as T60, the listening pair's width
against its mono fold), and CPU.

## What has not been verified

This plugin has never been loaded into a DAW. It builds, it passes Steinberg's
validator 47/47, its DSP is measured — and nobody has yet played it on the rig
it was written for. That is the acceptance test, and it has not happened.
