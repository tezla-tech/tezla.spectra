# Tezla Capstone

A true-peak brickwall limiter and clipper for the end of a chain — the drum
bus, the master bus, or anywhere a ceiling has to actually hold.

**Code `Tzcp` · vendor `Tzla` · bundle `tech.tezla.Capstone` · schema v1**

Nothing else in the suite holds a ceiling. Emberdrive's multiband limiter is
part of its own sound, and Halo has no output stage at all — its factory presets
span 11 dB of peak level on the same input. Capstone is the last plug.

---

## What makes it different

**The ceiling is a theorem, not a setting.** Most limiters get close and then
clamp. This one is built so the gain arriving at every sample is already below
what that sample needs, and the clamp at the end has nothing left to do —
measured at **6.1e-15**, which is −285 dBFS.

The proof is the ordering, and it is worth stating because it is the product:

```
level ── GainComputer ── EnvelopeFollower ── RunningMinimum ── BoxStack ── x
(true    (soft knee)     (release only)      (attack+hold)     (attack)    |
 peak)                                                                     v
x ─────────────────── delay(attack + detector) ───────────────────────── clamp
```

Write `g[n]` for the gain sample `n` requires. Every stage after the gain
computer may only make it smaller: the envelope holds it down, the running
minimum is taken over a window that contains `n`, and the smoothing kernel is
non-negative and sums to one. So every term of the weighted average is already
below `g[n]`, and so is the average. That holds for *any* such kernel, which is
why the shape could be chosen for how it sounds rather than for the proof.

Three things fall out of it:

- **Hold is free.** Widening the minimum window into the past keeps the
  guarantee — a minimum over a superset is smaller — and needs no future
  samples, so it costs no latency.
- **The gain can never go negative or above one**, being a convex combination
  of values in `[0, 1]`.
- **Attack *is* the look-ahead.** There is no second time constant downstream,
  because a second one is exactly what would break the alignment.

---

## The two stages

They live in the same slot and are different tools.

**Clip** cuts the waveform. That is the only thing that holds a ceiling with no
look-ahead at all: without future samples the gain cannot come down before the
peak, so the peak has to go. On a drum bus that is not a compromise, it is the
technique — shaving the transient tips means the limiter is not asked to duck
the whole mix 10 dB every time a kick lands, which is where limiters pump.

**Limit** brings the gain down ahead of the peak. Costs latency, buys
transparency. See the table below for exactly how much of each.

**Listen** solos what the two of them removed, so their interaction stays
inspectable rather than mysterious.

---

## Controls

| Control | Range | What it does |
|---|---|---|
| **Threshold** | 0 … −30 dB | Drives the signal into the ceiling. Makeup is automatic, so lowering it raises the level going in without raising the level coming out — the L1 workflow. |
| **Ceiling** | −24 … **+6** dB | The level the output is not allowed past. Above 0 dBFS is deliberate: nothing in a floating-point chain has to stop at full scale, and catching only the extremes is a real use. |
| **Clip** | on/off | The clipper. Off is bit-exact. |
| **Clip Threshold** | −12 … +6 dBFS | Absolute, not relative to Ceiling. Set it **above** Ceiling and the clipper takes the transient tips while the limiter handles the body; set it at or below Ceiling and the clipper does all the work — which is what 0 ms limiting means. |
| **Clip Shape** | hard … soft | 0 is a hard corner, 1 is a tanh. In between, the curve is the identity up to `1 − shape` and a tanh after it, matched in slope at the join. |
| **Clip Oversampling** | Auto / Off / ×2 / ×4 / ×8 | Auto follows the house policy: ×4 at 44.1/48 k, ×2 at 88.2/96 k, off at 176.4/192 k. |
| **Limit** | on/off | |
| **Lookahead** | on/off | Off pins the attack at zero, which is the only way the reported latency reaches exactly 0. |
| **Attack** | 0 … 20 ms | The look-ahead, the attack, and the reported latency — all the same number. |
| **Hold** | 0 … 100 ms | How long the gain stays down after a peak. Free: no extra latency. |
| **Release** | 1 … 2000 ms | |
| **Auto Release** | on/off | Program-dependent: a second, slower release runs alongside the first. |
| **Knee** | 0 … 24 dB | How far below the ceiling the curve starts bending. 0 is a hard corner. |
| **Stereo Link** | 0 … 100 % | 100 % keeps the centre image still. 0 lets each channel follow its own peaks — wider, looser, and it will move the image. |
| **True Peak** | Off / Standard / Strict | What the detector measures. See below. |
| **Output** | −12 … +12 dB | Applied *after* the ceiling. This is the control that goes past 0 dBFS. |
| **Listen** | on/off | Solo what was removed. |

---

## Measured

All figures from `tezla-measure capstone`, at 48 kHz unless stated. Re-run it
after any change to the engine.

### True peak at the output — ceiling −1.0 dBFS

Content dense near Nyquist, which is where a sample meter is worst.

| Detector | Ratio | Sample peak | True peak |
|---|---|---|---|
| Off | ×1 | −0.000 dB | **+1.506 dB** |
| Standard | ×4 | −1.242 dB | +0.264 dB |
| Strict | ×16 | −1.506 dB | **+0.000 dB** |

Read the two columns together. Off holds the samples exactly on the ceiling and
reconstructs 1.5 dB above it. Strict deliberately holds the samples 1.5 dB
*below* the ceiling so that what the converter actually produces lands on it. A
true-peak limiter is quieter on the meter and correct in the air.

On a sine at exactly a quarter of the sample rate, offset 45°, the sample-peak
setting is **+3.011 dB** over — against the Recommendation's formula of 3.0103.
It is not approximating there; it is missing the peak entirely.

**These numbers are identical at 44.1, 48, 96 and 192 kHz.** The ratio is fixed
rather than scaled by host rate, and that is deliberate: see
`TruePeakDetector.hpp`. Scaling it — which the Recommendation allows and which
the house oversampling policy does — was built and measured, and dropped
Standard to a ratio of 1 at 192 kHz, reading 1.506 dB under the true peak.
Identical to Off, the setting it exists to improve on. The reduction assumes the
metered content is band-limited to about 20 kHz, and a limiter sitting after a
clipper cannot assume that.

### Clipper aliasing — hard corner, driven 12 dB in

| Oversampling | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Off | −50.6 dB | −57.2 dB | −77.1 dB | −96.4 dB |
| ×2 | −72.9 dB | −77.0 dB | −96.6 dB | −114.3 dB |
| ×4 | −92.6 dB | −96.5 dB | −115.2 dB | −149.5 dB |
| ×8 | −112.0 dB | −115.0 dB | −131.3 dB | −149.9 dB |

Audible-band inharmonic energy relative to the fundamental. The house target is
−60 dB; Auto clears it by 36 dB at 48 kHz.

Two things get it there. The stage is oversampled, and the shaper is
band-limited by ADAA — neither alone is enough on a hard corner, which
`test_Measurement.cpp` pins at −47 dB for a naive clipper.

ADAA is applied to the clipper's **excess**, `f(x) − x`, rather than to the
whole curve. ADAA averages consecutive samples, so over a straight line it is a
lowpass: run the whole curve through it and a signal well under the threshold
comes out 0.47 dB down at 20 kHz at ×4 from 48 kHz, for nothing. The excess is
exactly zero below the knee, so a quiet signal passes untouched to the bit, and
the two forms band-limit identically to within 0.2 dB.

### What look-ahead buys — 60 Hz tone, limited 12 dB

| Attack | Latency (48 k) | THD |
|---|---|---|
| 0 ms | 0 samples | −31.0 dB |
| 0.05 ms | 1 | −31.0 dB |
| 0.2 ms | 9 | −31.2 dB |
| 1 ms | 47 | −32.5 dB |
| 5 ms | 239 | −59.6 dB |
| 20 ms | 959 | −166.1 dB |

A low tone is the hard case: the gain has to move slowly enough not to distort a
cycle that lasts 800 samples. Zero look-ahead cannot bring the gain down before
the peak, so it cuts the waveform instead — which is what the Clip stage does
deliberately, band-limited, and with a shape control. Same figures at every host
rate to within 0.7 dB.

### CPU — one core, 60 s of stereo audio in 128-sample blocks

| Setting | 48 kHz | 192 kHz |
|---|---|---|
| limit only, sample peak | 155× realtime | 41× |
| limit only, Standard | 91× | 24× |
| limit only, Strict | 25× | 6× |
| clip Auto + limit Standard | 31× | 20× |
| clip ×8 + limit Strict (worst) | 12× | 3× |

Strict is about four times Standard, and that is the whole cost of the accuracy
in the first table. Standard is the default. On a bus or master — where this
plugin lives — one instance of Strict at 192 kHz is comfortable; twenty is not,
and nothing about this plugin suggests twenty.

### Everything else

| Check | Result |
|---|---|
| Both stages off | **bit-exact** input |
| Clip on, signal below its threshold, oversampling off | **bit-exact** |
| Ceiling, swept over 972 combinations | never exceeded; clamp excess < 1e-12 |
| Output at block sizes 1, 64, 100, 512, 4096 | **identical** |
| Reported latency | equals the measured impulse delay |
| Look-ahead off, True Peak off, Clip off | latency exactly 0 |
| Hold, any value | adds no latency |
| Silence in, everything on | silence out |
| Tests | 269 pass on x86-64 and ARM64 under qemu |
| Steinberg validator | **47/47** |
| Reported latency, checked through the JUCE layer | 53 samples at 1 ms + Standard; 314 at 5 ms + Strict + clip ×4 |
| Plugin inert (preset "Clean") | **bit-exact** through the real processor |

---

## Notes

- **The ceiling test needed fixing before it meant anything.** The 972-case
  sweep passed with the limiter's minimum window deliberately halved against the
  smoother's support, because the clamp at the end of `LimiterCore` holds the
  ceiling whatever reaches it. Every peak reading landed exactly on the ceiling
  while the clamp was removing 1.02 of full scale — the limiter had become a
  clipper and no peak measurement could say so. The sweep now asserts on
  `getClampExcess()` instead, and goes red on that break.
- **The ITU coefficients are transcribed, not derived** — the one thing in
  Capstone that is copied. A meter that agrees with every other dBTP meter has
  to use the standard's filter, and no measurement we could run would say a
  filter of our own was the wrong one to have chosen. Every coefficient is an
  exact multiple of 1/8192 and the prototype is symmetric, which is what makes a
  transcription error checkable; `test_TruePeakDetector.cpp` checks both.
- **No modulation layer.** A limiter does not want LFOs. The shared MOD strip
  can be added later at the cost of an afternoon if that turns out to be wrong.

---

## Presets

Nine, each a complete parameter set — loading one never leaves a stale control
behind.

| Preset | For |
|---|---|
| Clean (bit-exact) | The reference. Both stages off, no drive, no trim — the thing to A/B everything else against. |
| Master: transparent | A ceiling and nothing else. Long look-ahead, slow release, wide knee, Strict. |
| Master: −1 dBTP delivery | The usual streaming ceiling, with Strict making the number true rather than approximately true. |
| Drum bus: clip and catch | The clipper shaves 2 dB above the ceiling so the limiter never ducks the whole kit. |
| Drum bus: 0 ms hard | No look-ahead at all: the clipper is the whole limiter. Zero latency, no pumping. |
| Sub bass: hold it down | Long release so the gain cannot move inside a 40 Hz cycle. Linked, so the sub stays centred. |
| Reese: weight | Soft clip above the ceiling for grit the limiter alone will not add. |
| Safety: catch the extremes | Ceiling at +3 dB, hard knee. Passes the mix untouched and only catches the accident. |
| Loud: drum and bass master | 12 dB of drive into a clipper, then a fast limiter. The starting point for a dubstep master. |
