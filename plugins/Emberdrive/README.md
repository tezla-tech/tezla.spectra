# Emberdrive

Tube and tape saturation into a soft-knee limiter. Built for drum busses, sub
bass and reeses — analogue weight and grit that survives brutal input levels
and holds together below 60 Hz.

- **Format:** VST3, 64-bit, Windows x64
- **Plugin code:** `Tzem` · **Vendor:** Tezla Tech
- **Version:** 0.1.0

---

## Signal flow

```
in --+------------------------------------------------- dry --+
     |                                                        |
     +-- [ oversampled x1/x2/x4/x8 ] -----------------------+  |
           tone tilt + character voicing                    |  |
           drive                                            |  |
           saturation (ADAA, biased tanh)                   |  |
           DC blocker                                       |  v
           soft-knee limiter (stereo linked)                | mix --> trim --> out
           mix against the oversampled dry -----------------+
         [ /oversampled ]
```

Everything nonlinear — the saturation *and* the limiter — runs inside the
oversampled section. The dry/wet mix happens in there too, against the upsampled
input rather than the original, so both sides carry identical delay and partial
mix settings cannot comb.

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

---

## Presets

| Preset | For |
|---|---|
| Clean | Proof it can get out of the way. Drive 0, mix 100%, no limiting. |
| Drum bus | Glue and weight, slow attack so transients survive. |
| Sub bass | Harmonics that let 40 Hz survive a phone speaker, no DC wander. |
| Reese | Aggressive, valve-heavy, the reason this exists. |
| Mix glue | The gentlest useful setting: wide knee, slow, barely working. |

---

## Roadmap

**v2** — multiband: a Linkwitz-Riley crossover with per-band limiters and a
master limiter, on this same engine. Aimed at mix-bus and master duty rather
than per-track colour.

**v3** — cabinet and amp voicings (the `Ferrite` / `Anvil` lane in the plugin
registry), a hand-drawn panel, minimum-phase IIR polyphase oversampling as a
low-latency option, and a second saturation curve family.

---

## Notes

- The shaper is backed off by a factor of four before the tanh. Without that,
  Drive at 0 dB still runs a −20 dBFS signal at 0.1 into the curve, which is
  0.09% THD — respectable for analogue gear, but not the "genuinely
  transparent" that `CLAUDE.md` asks for as priority two.
- The Character control changes bias *and* the voicing filters around the
  nonlinearity. Frequency response around a saturator is most of what makes one
  sound like tape and another like a valve; the transfer curve alone is not.
- Saturation is per-channel, dynamics are stereo-linked. Independent per-channel
  dynamics would move the centre image.
