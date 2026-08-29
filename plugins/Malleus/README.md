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

## Presets

*Init* · *Physical 808* · *Tabla Drop* · *Neuro Stab* · *Slendro Gongs* ·
*BP Bell Choir* · *Bowed Bowl Drone* · *Sitar Cloud* · *Glass Marimba* ·
*Dropped Mallet* · *Idle reference*

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
pressure × speed plane, the between-modes floor, and CPU.

## What has not been verified

This plugin has never been loaded into a DAW. It builds, it passes Steinberg's
validator 47/47, its DSP is measured — and nobody has yet played it on the rig
it was written for. That is the acceptance test, and it has not happened.
