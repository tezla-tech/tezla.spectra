# Tezla Transpectus

A metering and analysis plugin: loudness, true peak, spectrum and stereo image,
for the master bus and for any bus you are trying to make a decision about.

**Code `Tztr` · vendor `Tzla` · bundle `tech.tezla.Transpectus` · schema v1**

The rest of the suite changes the audio. This one does not touch it — its
`process()` takes a `const double* const*`, so that is enforced by the compiler
rather than by a code review — and reports zero latency, measured zero.

---

## Why it exists, and the one rule it follows

The suite had three processors and nothing to measure with. Emberdrive, Halo
and Capstone each carry a small meter, but there was no way to answer the
questions that actually decide a master: *how loud is this, how much will
Spotify turn it down, is there any transient left, and will the sub survive a
club system.*

The governing rule, and the reason this was worth building:

> **Every number it shows is either a published standard or a curve you
> measured yourself.**

No invented target curves, no genre folklore. Where a number comes from a
document, the document is named in [`../../docs/DSP-REFERENCES.md`](../../docs/DSP-REFERENCES.md).
Where it comes from a measurement, you took the measurement.

---

## The numbers

### Loudness — ITU-R BS.1770-5 / EBU R 128

Three readings, as the Recommendation defines them: **momentary** (400 ms, no
gating), **short term** (3 s, no gating) and **integrated** (gated — absolute at
−70 LUFS, then relative at −10 LU below the ungated mean).

**The trap this had to avoid, and it is [`CLAUDE.md`](../../CLAUDE.md) §6
exactly:** BS.1770 tabulates its K-weighting coefficients **for 48 kHz only**.
Used at any other rate they are silently wrong — not broken, just a meter that
reads a bit off and never says so. So the filter pair is designed from the
analogue prototype at whatever rate the host is running, and checked against
the printed table at 48 kHz, where it reproduces it to **8.9e-16**.

What that buys, from `tezla-measure loudness`:

| Host rate | Integrated | Short term | Momentary |
|---|---|---|---|
| 44 100 | −22.991 | −22.991 | −22.991 |
| 48 000 | −22.993 | −22.993 | −22.993 |
| 96 000 | −23.011 | −23.011 | −23.011 |
| 192 000 | −23.020 | −23.020 | −23.020 |

Worst deviation from −23.000: **0.0203 LU**. EBU Tech 3341 case 1 allows 0.1.

### Gating

| Signal | Integrated |
|---|---|
| 10 s at −23 dBFS, then 10 s of silence | **−23.059** (ungated it would read −26.0) |
| Half at −23, half 10 dB below | **−25.590** — both halves counted |
| Half at −23, half 15 dB below | **−23.057** — the quiet half gated out |

The boundary between those last two rows is **12.79 dB, not 10**, and getting
that wrong is how a loudness meter ends up subtly disagreeing with every other
one. The relative gate sits 10 LU below the ungated mean — but the quiet half
drags that mean down as it is computed, so the gate follows it. Solve it and
the quiet half is excluded exactly when `z₁/z₂ > 19`. A test pins both sides.

### True peak

Same detector as Capstone: BS.1770-5 Annex 2, whose coefficient table is typed
in verbatim (see §9 of `CLAUDE.md` on when copying beats deriving). Three
settings, and the cost of each is in the CPU table below.

| Setting | Oversampling | For |
|---|---|---|
| Off | ×1 | Sample peak only. Fastest, and honest about what it is. |
| Standard | ×4 | The ITU filter. What a delivery spec means by dBTP. |
| Strict | ×16 | For when you want to see the last tenth of a dB. |

### PLR and PSR

**PLR** = dBTP − LUFS-I, over the whole programme. **PSR** is the same against
short-term loudness, so it moves bar to bar — the one you watch while working.

This is what limiting costs, measured by running a drum pattern through
Capstone at increasing amounts:

| Limiting | dBTP | LUFS-I | PLR | vs clean |
|---|---|---|---|---|
| clean | −0.26 | −11.70 | 11.44 | — |
| 3 dB | −1.00 | −11.21 | 10.21 | **−1.23** |
| 6 dB | −1.00 | −10.29 | 9.29 | **−2.15** |
| 12 dB | −1.00 | −8.68 | 7.68 | **−3.76** |

Read the last two columns together: 12 dB of limiting bought 3.0 dB of loudness
and spent 3.76 dB of transient to do it. Below about 5 dB of PLR the transients
are gone, and every platform in the table below then turns the result down
anyway.

*(A pure sine is useless for this and the tool says so: a stereo 1 kHz tone
reads PLR 0.00 at every level, because its 3.01 dB of crest factor is cancelled
exactly by the 3.01 dB of summing two correlated channels.)*

### What each platform will do

Verified 2026-08-27, stored as **data with that date** because these change and
a hardcoded number that silently goes stale is worse than no number.

| Target | LUFS-I | −8 LUFS master | −20 LUFS master |
|---|---|---|---|
| Spotify | −14 | −6.0 dB | **+6.0 dB** |
| Apple Music | −16 | −8.0 dB | **+4.0 dB** |
| YouTube | −14 | −6.0 dB | **+0.0 dB** |
| Tidal | −14 | −6.0 dB | **+0.0 dB** |
| Amazon Music | −14 | −6.0 dB | **+0.0 dB** |
| SoundCloud | −14 | −6.0 dB | **+6.0 dB** |
| Deezer | −15 | −7.0 dB | **+5.0 dB** |
| EBU R 128 | −23 | −15.0 dB | −3.0 dB |

Signed as the gain the platform applies. **The zeroes are not rounding.**
YouTube, Tidal and Amazon Music only ever turn a loud master *down* — they
never boost a quiet one. So "you are 6 dB under the target" is advice there and
a correction on Spotify, and the panel says which.

All of them expect a **−1 dBTP** ceiling; the industry converged on this zone
via AES TD1008.

---

## Correlation, and why one number is not enough

`r = Σ(L·R) / sqrt(ΣL² · ΣR²)`. +1 the channels agree, 0 uncorrelated, −1
polarity inverted.

The panel shows two: the full band, and the sub. Here is why, measured on a mix
whose sub is inverted and whose everything-else is not:

| | full | low | mid | high |
|---|---|---|---|---|
| sub inverted | **+0.8571** | **−1.0000** | +0.9998 | +1.0000 |

The full-band reading is dominated by whatever carries the energy, and it reads
healthy while the sub cancels completely. That failure survives headphones —
each ear gets its own channel and nothing sums — and then removes the bass on
any rig that folds to mono, which is most club systems and every phone speaker.
So the sub gets its own bar, and the **Mono Check** control says where "sub"
ends.

The bands come from the same `ThreeBandSplitter` Emberdrive processes with, so
"the low band" means the same thing across the suite.

---

## The goniometer

The correlation number answers *do the channels agree*. The goniometer answers
*how do they disagree*, and those are different questions with different fixes.

Rotated 45° so mono is vertical — `x = (R−L)/√2`, `y = −(L+R)/√2` — because the
eye is much better at spotting a tilt off vertical than off a diagonal.

Six cases, all rendered by `tezla-ui-preview goniometer` against signals it
generates:

| Signal | r | Shape |
|---|---|---|
| mono | +1.000 | a vertical line |
| wide | +0.423 | a full ellipse |
| hard panned | −0.006 | a diamond reaching both corners |
| polarity inverted | −1.000 | a horizontal line |
| 6 dB left | +1.000 | the trace leans left |
| sub inverted | −0.246 | a horizontal band under a vertical one |

Two of those rows are the whole argument. **Wide** and **hard panned** read
+0.42 and −0.01 — close enough that a bar between them is a guess — and they
need opposite fixes: one is a stereo image, the other is two mono tracks that
never met. And **6 dB left** reads exactly +1.000, because correlation
normalises level away by construction. No correlation meter of any kind can see
a lopsided mix. The picture shows it immediately.

It holds **50 ms at every sample rate**, sized in seconds rather than samples,
and strides to keep its point count — so the picture spans the same slice of
time at 192 kHz as at 44.1.

---

## The spectrum, and the two honest references

**Pink slope.** A −3 dB/octave line. Not a target — a ruler. Pink noise has
equal power per octave, which is roughly how hearing divides the spectrum, so a
mix that runs parallel to it is balanced in a sense that can be stated rather
than felt.

**A captured reference.** Point the plugin at a track you already like, press
CAPTURE REFERENCE, and it accumulates for **30 seconds** and stores the shape.

Four decisions in that, each of which is what makes it useful rather than
decorative:

- **Thirty seconds, minimum five.** A two-second capture is a snapshot of one
  chord and says nothing about tonal balance. The floor is enforced, not
  documented.
- **Power is averaged, not decibels.** Averaging logarithms weights a quiet
  moment as heavily as a loud one and pulls the curve towards whatever the
  track does least. A test pins a signal where the two disagree completely:
  averaging power gives an 8 dB rise, averaging dB gives exactly flat.
- **Normalised to zero mean.** A quiet reference and a loud one with the same
  balance give the same curve. Measured: 54 dB apart in level, identical in
  shape to 1.9e-06 dB — which is the precision of the `float` the display hands
  over, not a difference in the curves.
- **Smoothed to 1/6 octave**, or bin noise makes it unreadable.

**Difference** then draws live-minus-reference, which states the EQ move
directly instead of leaving it to be eyeballed.

### The permanent peak hold

Three traces, three different questions, three colours:

| Trace | Colour | Answers |
|---|---|---|
| live | bright green, filled | what is happening now |
| the analyser's own hold | dim green | what happened in the last second or so |
| **the permanent hold** | **violet** | **what is the worst this mix has done** |

The violet one never decays. That is the whole point — the middle trace falls
away while you are still reaching for the mouse, and the question a mix decision
actually asks is *what is the loudest that resonance ever got*, not *what was it
half a second ago*.

**RESET PEAKS** throws it away and starts collecting again, which is how the
feature gets used: make an EQ move, clear, play the section back, and see what
the new worst case turns out to be.

It **survives closing the window**, because it is a measurement and a
measurement that vanishes when you close a window is not one — the same reason
the true-peak hold and the integrated loudness behave that way. It is stored on
the processor, not in the editor. **RESET MEASUREMENT** clears it along with
everything else held.

Measured, by running the tool's test signal loud, dropping it 26 dB, and looking
at what each trace did:

| | |
|---|---|
| after the drop | violet sits **26 dB above** the live curve; the analyser's own hold has fallen most of the way |
| after RESET PEAKS | violet gone, re-collecting from the current level |
| after closing and reopening the editor | violet **still there**; the analyser's own hold correctly gone, because that one lives in the editor |

### The crosshair

Point at the spectrum and it reads out where you are pointing:

```
998 Hz   B5 +18
signal  -32.1 dB
cursor  -42.0 dB
```

Three things, because they answer three different questions. **Frequency with
the nearest note in cents**, because half of what a spectrum gets used for on
this rig is finding out what note a bass or a resonance is sitting on.
**Signal**, the live curve's level at that frequency, with a dot on the curve so
the number is anchored to something visible. **Cursor**, the dB of the
horizontal hairline itself, for measuring the distance between two things.

Verified against the grid rather than by eye: the panel is 864 px wide, which
puts 1 kHz at x = 488.24 and −42 dB at y = 122.0. Pointing at exactly (488, 122)
reads **998 Hz** and **−42.0 dB** — the 2 Hz is the quarter-pixel that got
rounded away, and it is the whole error.

The readout flips to whichever side of the pointer has room, so it never hangs
off the panel or covers the part of the curve being pointed at.

### Where a reference lives

Both places, because they answer different needs:

- **In the plugin state**, so a capture travels with the project and survives a
  reopen.
- **In a `.tzref` file** via SAVE / LOAD, so a reference is reusable across
  every project — which is what a reference is *for*. Plain text on purpose: a
  header line and 96 numbers. A curve you cannot open and read is a curve you
  have to trust.

The loader refuses anything it cannot fully parse rather than half-loading it,
because a half-loaded reference still looks like a measurement. It also strips
carriage returns, since these files are meant to move between the Windows rig
and the Mac.

### The boundary, and it is not negotiable

This measures **audio you play through it**, which `CLAUDE.md` §2.1 explicitly
permits — measuring our own plugin against a target the user plays back is the
primary tuning loop here.

**No captured curve from any commercial record will ever ship with this
plugin.** The tool makes your references. It does not come with somebody
else's.

---

## The panel

Seven readouts, a spectrum, a goniometer and two correlation bars is a lot to
fit, so two things give way.

**Each readout is one line**, its caption beside the number rather than above
it. That is what lets a row be 44 pixels instead of 76, and the room it frees
goes to the spectrum.

**Either large panel can take the whole body**, with MAX in its top-right
corner. Maximise means *this and nothing else* — the readouts and the other
panel are hidden, because a maximise that leaves half the window in place is a
resize. On the spectrum it is also a precision control: more pixels across the
axis is a finer crosshair reading.

**Or either can be lifted into its own window**, with POP. That is the answer
when you want the big picture *and* the numbers: the panel opens beside the
editor — constrained to stay on screen, so it never lands in the void past the
last monitor — and everything else stays where it was. Closing that window, or
the DOCK button standing where the panel used to be, puts it back.

The two are deliberately different tools rather than two names for the same
one. Maximise is for looking at one thing closely; detach is for looking at two
things at once, which is what a second monitor is for.

---

## Controls

| Control | Range | What it does, and what it costs |
|---|---|---|
| **Target** | 8 platforms | Which row the VS TARGET readout is computed against. Changes nothing else. |
| **True Peak** | Off / Standard / Strict | ×1 / ×4 / ×16 oversampling in the detector. The only control here with a real CPU cost — see below. |
| **Mono Check** | 60–300 Hz, default 120 | Where the sub correlation bar stops looking. 120 Hz because that is about where a club system stops being able to place a sound and starts merely moving air. |
| **Bypass** | — | Pauses measurement. The audio is untouched either way; this plugin never changes it. |

The spectrum's own switches — **Pink slope**, **Difference**, **Peak hold** —
are view state rather than parameters: they change what is drawn, never what is
measured, so they are not saved with the project.

There are no presets. Nothing here shapes a sound, so there is nothing to
preset — the Target selector is the only thing anyone would save, and it is one
click.

---

## Measured

### CPU — one core, 60 s of stereo audio in 128-sample blocks

| Setting | Time | Realtime factor |
|---|---|---|
| true peak Off | 0.204 s | **294×** |
| true peak Standard | 0.427 s | **140×** |
| true peak Strict | 2.114 s | **28×** |

An analyser gets left open on every channel you are watching, so this is the
number that decides whether you can. Standard is the default. Strict's ×16
oversampling is a deliberate choice with a price you can see.

### Everything else

| Check | Result |
|---|---|
| Audio passthrough | **bit-identical** to the source signal, max error 0.000e+00 |
| Measuring vs bypassed output | **bit-identical** |
| 512-sample vs 64-sample blocks | **bit-identical** |
| Reported latency | **0 samples**, measured 0 |
| K-weighting vs the printed 48 kHz table | agrees to **8.9e-16** |
| Allocations while processing | **0**, across every target and true-peak mode |
| Allocations in the scope read the editor does each frame | **0** |
| Silence in | every reading at its floor, no NaN |
| Editor: create, maximise each panel, resize 760x520 to 1520x1040, destroy | clean (`tezla-render editor`) |
| Every control reachable by a click at its own centre, at 760 and at 980 wide | yes (`hit:` — catches a control buried under an opaque sibling) |
| Pop a panel out, dock it back, then use its buttons | works (this was a bug: see the root README) |
| Editor: close the standalone with a panel detached | exits 0 |
| Crosshair at the 1 kHz gridline | reads **998 Hz**, one quarter-pixel of rounding |
| Steinberg validator | **47 tests passed, 0 failed** |
| `tezla-tests` | **316 passed, 0 failed** on x86-64 and on ARM64 under qemu |

### Reproducing all of it

```
scripts\build.bat NONE -test          :: or ./scripts/build.sh NONE --test
tezla-measure selftest                :: check the instrument first
tezla-measure loudness                :: everything in the tables above
tezla-ui-preview goniometer out.png   :: the six stereo shapes

:: the editor with no host and no window manager: click controls by id,
:: point at one, photograph the result
TranspectusRender editor spectrum-max shot:big.png
TranspectusRender editor spectrum@488,122 shot:crosshair.png

:: with real signal in it, and driving the editor's own timer
TranspectusRender editor audio:2 tick:10 audio:3@0.05 tick:40 shot:hold.png
TranspectusRender editor size:760x520 hit:reset-peaks shot:narrow.png
```

---

## What is not verified

Per `CLAUDE.md` §5: this is developed in a Linux container. The DSP tests pass
on four architectures and the plugin validates, but **nobody has loaded this
into FL Studio or Logic from here.** The acceptance test is you, on a master
bus, with a track you know.

---

## Roadmap

- Per-band correlation is measured for all three bands; only full and sub are
  displayed. A three-bar view is a small change if the mid one turns out to be
  worth watching.
- Loudness range (LRA, EBU Tech 3342) — the machinery is there, it is a
  percentile over the same gated block list.
- Spectrum peak-hold and ghost-trace exposure. The ballistics already exist in
  `SpectrumAnalyser`; this is a UI control, not new DSP.
