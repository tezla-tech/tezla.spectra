# Emberdrive

Tube and tape saturation into a soft-knee limiter, with a full destruction
section — wavefolder, rectifier, bit crusher, rate reducer and feedback — and an
optional three-band split. Built for drum busses, sub bass
and reeses — analogue weight and grit that survives brutal input levels and
holds together below 60 Hz.

- **Format:** VST3, 64-bit, Windows x64
- **Plugin code:** `Tzem` · **Vendor:** Tezla Tech
- **Version:** 0.3.0

---

## Signal flow

```
in --+------------------------------------------------------- dry --+
     |                                                              |
     +-- [ oversampled x1/x2/x4/x8 ] -----------------------------+  |
           tone tilt + character voicing                          |  |
           three-band split (multiband mode only)                 |  |
           per band:  drive trim                                  |  |
                      fold      (ADAA sine folder)                |  |
                      saturate  (ADAA biased tanh)                |  |
                      DC block                                    |  |
                      soft-knee limiter                           |  v
           sum bands -> master limiter (multiband only)           | mix --> trim --> out
           mix against the oversampled dry ----------------------+
         [ /oversampled ]
```

Everything nonlinear runs inside the oversampled section — **except Crush and
Downsample, deliberately.** Everywhere else in this plugin aliasing is a defect
to suppress; in a bit crusher it is the instrument, and an antialiased quantiser
just sounds like a slightly noisy version of the input. Those two run at the
host's own rate with the artefacts left alone.

Because they are wet-only, the dry/wet mix happens after them at base rate, with
the dry path delayed by exactly the oversampler's latency — which is a whole
number of base-rate samples by design, so the two sides stay sample-aligned and
partial mix settings cannot comb.

---

## Controls

| Control | Range | Default | What it does |
|---|---|---|---|
| **Drive** | 0 … +30 dB | 0 dB | How hard the signal is pushed into the saturation. 0 dB is genuinely clean; +18 dB is obviously coloured; +30 dB is destruction. |
| **Character** | Tape ⟷ Valve | 35% valve | Tape is symmetric — odd harmonics, compressive knee, plus a low head bump and gentle HF loss. Valve is asymmetric — even harmonics, flatter and brighter. |
| **Tone** | Dark ⟷ Bright | Flat | A ±5 dB tilt *before* the saturation, so it changes what gets distorted rather than filtering the result. |
| **Ceiling** | −24 … 0 dBFS | −0.3 dBFS | The level the output is held to. |
| **Knee** | 0 … 24 dB | 6 dB | How far below the Ceiling the curve starts bending. 0 is brickwall limiting; 24 dB is gentle, always-working glue. This is what decides limiter vs compressor. |
| **Speed** | 0.05 … 100 ms | 5 ms | Attack, as a 1/e time constant. Fast flattens transients; slow lets the front of a kick through. |
| **Release** | 20 … 2000 ms | 200 ms | Release, as a 1/e time constant. |
| **Auto release** | on/off | off | Program-dependent: a second release six times slower runs alongside, and whichever holds more reduction wins. Short peaks recover fast, sustained material slowly. |
| **Mix** | 0 … 100 % | 100 % | Dry/wet. The dry path is delay-matched exactly. |
| **Output** | −24 … +24 dB | 0 dB | Final trim, after everything. |
| **Auto trim** | on/off | on | Compensates the level for whatever Drive is doing, so Drive is a tone control rather than a volume control. |
| **Oversampling** | Auto / Off / ×2 / ×4 / ×8 | Auto | See below. |
| **Bypass** | on/off | off | Latency-matched and crossfaded over 10 ms, so A/B is honest. |

### MANGLE

| Control | Range | Default | What it does |
|---|---|---|---|
| **Fold** | 0 … 100 % | 0 % | A wavefolder. A clipper flattens a peak and stops; a folder turns it back on itself, so the harmonics keep changing as you push instead of converging on a square wave. At 0 it is *exactly* a straight wire. |
| **Range** | ×1 / ×10 / ×100 | ×1 | The multiplier on Fold. ×1 is a musical folder, ×10 is aggressive, ×100 folds a full-scale signal about 32 times per half cycle. See the measurements below — ×100 is a bass tool. |
| **Rectify** | 0 … 100 % | 0 % | Blends toward full-wave rectification. Flipping the negative half cycles up doubles the fundamental, giving an octave-up ghost that tracks the note with no pitch tracking involved. Sits first, so Fold and the saturation work on the octave. |
| **Feedback** | 0 … 95 % | 0 % | Routes the output back into the drive stage through a short delay, with the whole nonlinear chain inside the loop. Sustains and screams. **Cannot run away** — see below. |
| **FB Time** | 0.1 … 50 ms | 8 ms | The loop period. The signal repeats at this rate, so the resonance is 1/time — the readout shows both. Short is a metallic ring, long is a stuttering repeat. |
| **Crush** | Off … 1 bit | Off | Bit-depth reduction, 16 bits down to 1. Not antialiased, on purpose. |
| **Downsample** | Off … ×64 | Off | Sample-and-hold rate reduction. At ×8 on a 48 kHz session the signal behaves as if running at 6 kHz. Fractional ratios work, so it sweeps rather than stepping. |

Chain order on this page: **Rectify → Fold → saturation**, with **Feedback**
wrapping all of it, and **Downsample → Crush** last at the host's rate. Crush and
Downsample are wet-only.

### BANDS

| Control | Range | Default | What it does |
|---|---|---|---|
| **Multiband** | on/off | off | Splits into three bands, saturates each separately, limits the sum. |
| **Low / Mid** | 40 … 800 Hz | 120 Hz | Lower crossover. |
| **Mid / High** | 800 … 12000 Hz | 2500 Hz | Upper crossover. |
| **Low / Mid / High Drive** | ±24 dB | 0 dB | Per-band drive trim, relative to the main Drive. Auto trim compensates each band separately, so this changes how dirty a band is, not how loud. |
| **Low / Mid / High Band** | On / Mute / Solo | On | Solo on any band silences the others; Mute always wins. |

### EXPERT

Off by default. While it is off, Character drives all of these and nothing here
has any effect.

| Control | Range | Default | What it does |
|---|---|---|---|
| **Bias** | −2 … +2 | 0 | Raw asymmetry of the curve, replacing what Character was setting. 0 is symmetric (odd harmonics only). |
| **Headroom** | ×1 … ×16 | ×4 | How far the signal is backed off before the curve. ×4 is what makes Drive at 0 transparent; ×1 makes the same Drive distort far harder. |
| **DC Block** | 1 … 40 Hz | 10 Hz | Corner of the high-pass that removes the DC asymmetry produces. |
| **Antialiasing** | on/off | on | ADAA on the fold and saturation. Off is here so you can hear what it does — and because the aliasing is sometimes the sound. |
| **Bump Freq / Gain** | 40–300 Hz / ±6 dB | 90 Hz / 1.5 dB | The tape head bump. |
| **Gap Freq / Gain** | 2–16 kHz / −12…+6 dB | 8 kHz / −2.5 dB | The tape high-frequency loss. |
| **Stereo Link** | 0 … 100 % | 100 % | How much the channels share gain reduction. 100 % keeps the centre image still. |
| **Detector** | Peak … RMS | Peak | Peak catches transients (limiting); RMS follows body over ~10 ms (glue). |

Every control has a tooltip that says what it does *and* what it costs. There is
no separate manual; the tooltips are the manual.

---

## Oversampling

Auto targets roughly the same ~192 kHz internal rate at every session rate, so
the plugin sounds the same whatever the session is running at:

| Session rate | Auto factor | Internal rate | Latency |
|---|---|---|---|
| 44.1 / 48 kHz | ×4 | ~176–192 kHz | 63 samples (1.31 ms at 48 k) |
| 88.2 / 96 kHz | ×2 | ~176–192 kHz | 47 samples (0.49 ms at 96 k) |
| 176.4 / 192 kHz | ×1 (off) | 176–192 kHz | 0 samples |

Manual `Off / ×2 / ×4 / ×8` is there if you want to spend or save CPU
deliberately. Forcing a factor higher than Auto picks costs CPU for very little;
forcing it Off saves CPU and gives up about 25 dB of alias rejection at high
drive.

Latency is reported to the host, so FL Studio's PDC compensates it.

---

## Measured

From `tezla-measure emberdrive`, 1 kHz tone at −12 dBFS, Character 0.35.

**The harmonic profile is identical to 0.01 dB at every session rate** — this is
the requirement that matters most on this rig, and it is checked by a test
rather than asserted:

| Drive | Level | THD | 2nd | 3rd | Audible aliasing |
|---|---|---|---|---|---|
| 0 dB | −12.04 dBFS | −41.4 dB | −41.4 dB | −72.0 dB | −228 dB |
| +6 dB | −12.05 | −35.4 | −35.4 | −60.0 | −236 dB |
| +12 dB | −12.06 | −29.5 | −29.6 | −48.0 | −244 dB |
| +18 dB | −12.09 | −23.9 | −24.2 | −36.2 | −246 dB |
| +24 dB | −12.19 | −19.0 | −20.3 | −25.1 | −247 dB |
| +30 dB | −12.37 | −14.4 | −19.9 | −16.2 | −246 dB |

- **Level holds within 0.33 dB across the entire drive range** with Auto trim on.
- **Audible-band aliasing is −220 dB or better** with Auto oversampling, and
  −136 dB at a 192 kHz session where Auto turns oversampling off. The standard
  in `CLAUDE.md` is −60 dB.
- At Character 0 the second harmonic measures **−282 dB** — symmetric to the
  limits of double precision, so the tape end really is odd-harmonic only.

Aliasing is measured in the audible band (20 Hz – 18 kHz) and in steady state.
Both qualifications matter: the full-band figure for the same signal reads
−79 dB because the entire residual is one harmonic sitting in the decimator's
transition band at 22 kHz, and including the oversampler's start-up ramp makes
an FFT report −30 dB for a chain that is really at −130 dB.

### The wavefolder, and where it stops being clean

From `tezla-measure fold`. Fold at maximum, drive 0, Auto oversampling, audible
band, dB relative to the fundamental:

| Input | Range ×1 | Range ×10 | Range ×100 |
|---|---|---|---|
| 40 Hz | −235 | −227 | **−181** |
| 80 Hz | −236 | −224 | **−188** |
| 160 Hz | −236 | −222 | **−175** |
| 330 Hz | −235 | −224 | −137 |
| 660 Hz | −234 | −226 | −103 |
| 1320 Hz | −236 | −214 | −64 |
| 2640 Hz | −237 | −162 | −46 |

**×1 and ×10 are clean everywhere. ×100 is a bass tool.** A folder's harmonics
run to roughly the fold gain times the fundamental, so at ×100 a 40 Hz sub keeps
everything comfortably below Nyquist and a 2.6 kHz lead does not. On a sub or a
reese it is clean; on a lead it will alias — which may be exactly what you want.

At Range ×100 with drive, THD reaches **+14 dB** — the harmonics are five times
louder than the note that made them, and the output no longer resembles the
input in any useful sense. That is the point of the control.

### The rest of the mangle section

| Stage | Measured |
|---|---|
| **Rectify** at 30 % | Octave (2nd harmonic) at −14.8 dB relative to the fundamental |
| **Rectify** at 60 % | Octave within 4 dB of the fundamental |
| **Rectify** at 100 % | The original fundamental is cancelled to the numerical floor — the octave *is* the signal. The raw ratio reads +210 dB, which is an artefact of dividing by a fundamental that no longer exists, not a gain. |
| **Crush** at 8.4 bits | Takes a chain sitting at −236 dB of aliasing up to **−26 dB** |
| **Downsample** at ×12 | Same chain up to **−10 dB** |

Those last two numbers are the feature working, not failing. A bit crusher
without folded-back images sounds like a slightly noisy version of the input.
There is a test asserting the aliasing *rises*, so a future change that quietly
starts antialiasing them shows up as a failure.

### Feedback, and why it cannot run away

A feedback loop wrapped around a nonlinearity with 30 dB of drive in it is
exactly the arrangement that blows up. Three things stop it, and none are
defeatable:

1. A **soft clip inside the loop** bounds whatever returns to ±1 regardless of
   how loud the loop has become.
2. The **feedback amount is capped below unity** (95 %) on top of that.
3. The **saturator's own compression** is what makes the loop settle into
   oscillation rather than screaming — its incremental gain falls below 1 as
   the level rises.

Swept across every combination of feedback (including a deliberately
out-of-range 200 %), delay from 0.1 to 50 ms, drive at 0 and +30 dB, with fold
and rectify running: **always finite, always bounded.** Silence in stays silent
— the loop cannot self-start from nothing.

It repeats at the delay you set. Measured by autocorrelation rather than by
hunting for a spectral peak, because spectrally the loop imposes a comb of
1/delay on whatever is circulating, and at delays like 4 ms those sidebands land
exactly on the sustained tone's own harmonics and become invisible:

| FB Time | Strongest repeat | Expected | Correlation |
|---|---|---|---|
| 2 ms | 97 samples | 96 | 0.99 |
| 4 ms | 193 samples | 192 | 0.99 |
| 8 ms | 385 samples | 384 | 0.99 |

The one-sample offset is the ADAA half-sample delay plus rounding.

Tail energy 0.5 s after a 50 ms burst has finished: **−100 dB without feedback,
−2.9 dB with it at 90 %.**

### Multiband

- The three bands sum flat to within 0.15 dB at every crossover point and at all
  four session rates (`tests/test_Crossover.cpp`).
- A clean sub under destroyed mids, measured in one pass: **−50 dB THD at 55 Hz
  and −20 dB at 700 Hz simultaneously**, with Low Drive at −24 dB and Mid Drive
  at +24 dB.
- The master limiter's attack is fixed at 0.2 ms rather than following Speed.
  Three bands summing overshoot more than any one of them was limited to — 2 dB
  against a single band's 1 dB — and catching that is its only job.
- Linkwitz-Riley sums flat at the cost of phase rotation. **Multiband mode is not
  phase-transparent against bypass.** That is true of every crossover-based
  processor; most of them do not mention it.

### What the antialiasing is worth

It depends almost entirely on Headroom, which is why that control is on the
expert page:

| Configuration | ADAA on | ADAA off | Difference |
|---|---|---|---|
| Headroom ×4, oversampling off | −69 dB | −59 dB | 10 dB |
| Headroom ×1, oversampling off | −36 dB | −24 dB | 12 dB |
| Headroom ×4, Auto (×4) | −174 dB | −172 dB | 2 dB |
| Headroom ×1, Auto (×4) | −111 dB | −86 dB | **25 dB** |
| Fold ×10, Auto (×4) | +12 dB | +41 dB | **30 dB** |

At the default headroom with oversampling on, ADAA is nearly free of effect —
the curve simply is not generating enough high harmonics to matter. Push
headroom down or the folder up and it becomes the difference between a usable
sound and a broken one.

---

## Presets

| Preset | For |
|---|---|
| Clean | Proof it can get out of the way. Drive 0, mix 100%, no limiting. |
| Drum bus | Glue and weight, slow attack so transients survive. |
| Sub bass | Harmonics that let 40 Hz survive a phone speaker, no DC wander. |
| Reese | Aggressive, valve-heavy, the reason this exists. |
| Mix glue | The gentlest useful setting: wide knee, slow, barely working. |
| Clean sub, dirty top | Multiband doing its job: low band at −18 dB trim, mid at +6. |
| Folded reese | The folder at a musical setting — metallic and hollow, still tracking the note. |
| Annihilate | Fold ×10 into heavy drive into multiband. On purpose. |
| Bitcrush | The XP-era bitcrusher, near enough — ×7 rate reduction into 8-bit, in parallel so the bottom survives. |
| Octave ghost | Rectify at 75 % blended under the original, for mid-bass and leads. |
| Screamer | Short delay, 72 % feedback — the plugin sustains and rings on after the note has gone. |

Presets never switch the expert panel on: it is for deliberate hands-on work,
and a preset silently overriding Character would be a surprise.

---

## Roadmap

**Done in v0.2.0** — multiband, the wavefolder with its ×1/×10/×100 range, and
the expert panel.

**Done in v0.3.0** — Rectify, Crush, Downsample and Feedback. The MANGLE page is
complete.

**Later** — cabinet and amp voicings (the `Ferrite` / `Anvil` lane in the plugin
registry), a hand-drawn panel, minimum-phase IIR polyphase oversampling as a
low-latency option, per-band Character, and a second saturation curve family.

---

## Notes

- The shaper is backed off by a factor of four before the tanh. Without that,
  Drive at 0 dB still runs a −20 dBFS signal at 0.1 into the curve, which is
  0.09% THD — respectable for analogue gear, but not the "genuinely
  transparent" that `CLAUDE.md` asks for as priority two.
- The Character control changes bias *and* the voicing filters around the
  nonlinearity. Frequency response around a saturator is most of what makes one
  sound like tape and another like a valve; the transfer curve alone is not.
- Saturation is per-channel, dynamics are stereo-linked by default. Independent
  per-channel dynamics move the centre image, which is why the link is a control
  rather than a fixed choice.
- The folder is `N(g)·sin(g·x)/g`. That form is chosen so g = 0 is exactly the
  identity — the stage is permanently in the path, so a folder that coloured the
  sound at its zero setting could never be turned off. `N(g)` holds the level as
  g rises, so Range changes the sound and not the volume.
- Its antiderivative is written as `2·sin²(g·x/2)`, not the algebraically
  identical `1 − cos(g·x)`. They agree on paper and not in floating point: for
  small arguments the second form is a difference either side of 1 and loses
  nearly every significant digit, which ADAA then divides by an equally small
  number.
- The low band passes through an allpass matching the second crossover. Without
  it the three bands no longer sum flat, and the symptom is not an obvious bug
  but "multiband mode sounds a bit odd".
- The rectifier's antiderivative is `x·|x|/2`, written without a branch so it
  stays continuous across the origin. ADAA straddles that point constantly on
  any signal that crosses zero.
- The feedback tap is taken after the DC blocker but *before* the auto-trim, so
  changing the trim does not change the loop gain.
- Every stage on the MANGLE page has an exact bypass at its neutral setting —
  bit-exact, not "close enough". They sit permanently in the path, so anything
  less would mean existing projects changed the day the plugin updated. There is
  a test asserting bit equality for all four at once.
