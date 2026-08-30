<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# The vocal chain

Where the tezla.spectra plugins go around a voice, in what order, and — the
part most chain guides skip — **where each one is the wrong tool**.

This is written for the rig it was built for: Windows 11, FL Studio, dubstep
and DnB. Rap vocals first, sung vocals second.

---

## The short version

```
  take
   │
   ├─ Syrinx            gate · de-ess · level · peak · tone      ← the strip
   │
   ├─ Emberdrive        grit, multiband, on the body only        ← optional
   ├─ Halo              air above 8 kHz                          ← optional
   ├─ Ferrite           glue and wobble                          ← optional
   │
   ├─ Capstone          the ceiling                              ← last, always
   │
   └─ Transpectus       checking, not processing                 ← anywhere
```

**Syrinx first, Capstone last.** Everything between them is taste, and most
takes need one of the three rather than all of them.

The one hard rule: **Capstone goes last**, on the track and on the master. It
is the only plugin here that guarantees a ceiling, and anything after it can
undo that.

---

## 1. Syrinx — the strip

Everything a voice needs before it has any character: the gate, the de-esser,
the levelling and the tone. Its internal order is fixed and
[argued in its README](../plugins/Syrinx/README.md); you do not have to think
about it.

**Start here, always.** Load *Rap Lead* or *Sung Verse* and get the level
steady before reaching for anything else. A take that is still swinging 18 dB
makes every plugin after it behave differently from bar to bar.

### Starting settings — rap

Preset ***Rap Lead***, then:

| control | where | why |
|---|---|---|
| IN Trim | so the leveller shows 3–6 dB | every threshold is relative to what arrives |
| HPF | 80–110 Hz | higher for a double, lower for a solo take |
| GATE | on, threshold under the quietest kept word, Range 12–18 dB | not infinity — the room should stay |
| DE-ESS Range | 6–10 dB | use LISTEN: esses and little else |
| LEVELLER | 2.5:1, 30 ms / 250 ms, AUTO REL on | riding phrases |
| PEAK | 6:1, 2 ms / 80 ms | catching what got past |
| EQ | flat to start | earn every dB |

For an **ad-lib**, use *Rap Ad-lib*: tighter gate, harder ratios, brighter. For
the **double**, use *Rap Double* — it is rolled off on top deliberately, so it
thickens the lead rather than competing with its consonants.

### Starting settings — singing

Preset ***Sung Verse*** or ***Sung Chorus***, and change two habits:

- **Gate off, or much gentler.** A sung phrase ends quietly on purpose. A
  rap-tuned gate eats those endings, and it is the single most common way a
  strip ruins a vocal.
- **Slower leveller.** 40 ms attack and 400 ms release, ratio 2:1. The point is
  to be inaudible.

### Where Syrinx is the wrong tool

- **A take with a real noise problem.** The gate hides broadband noise between
  words; it does nothing during them. Re-record, or use a dedicated denoiser.
- **A vocal that needs *character*.** Syrinx is deliberately clean. Nothing in
  it will make a voice sound like anything — that is the next three plugins.
- **Pitch.** Not here, not ever, by design.

---

## 2. Emberdrive — grit, on the body only

Saturation and multiband drive. On a voice, the mistake is to drive the whole
signal: the sub gets woolly and the sibilance gets *edges*, which no de-esser
downstream can fix because it is now harmonics rather than an /s/.

**Use the multiband split and drive the middle band only** — roughly 120 Hz to
3 kHz. That is where a voice's body and presence live. Leave the low band
alone so the sub stays defined, and leave the top alone so the consonants stay
clean.

| for | setting |
|---|---|
| rap, aggressive | mid band, moderate drive, low mix — 20–35% |
| rap, subtle weight | mid band, low drive, auto-trim on |
| singing | usually skip it; Ferrite is gentler |

**Auto-output-trim on**, always. Loudness sells distortion, and you are trying
to judge tone.

### Where it is the wrong tool

- **Fixing a thin voice.** Distortion adds harmonics, not weight. An EQ shelf
  or a better take is the answer.
- **The whole vocal bus with the sub included.** Split it or leave it.
- **After Capstone.** Anything nonlinear after the limiter puts the peaks back.

---

## 3. Halo — air

Generated high-harmonic content above about 8 kHz. It makes a vocal sound
*present* in a way a shelf cannot, because it adds detail rather than
amplifying what is there — which matters on a take that has nothing up there to
amplify.

**After the de-esser**, which is automatic if it is after Syrinx. That ordering
is not optional: adding air before a de-esser means the de-esser fights air you
just generated, and you get a dull vocal and a de-esser working far too hard.

| for | setting |
|---|---|
| rap lead | low amount, focus high — a lift, not a sheen |
| sung chorus | more amount, wider focus |
| a dull or distant take | this is the tool |

### Where it is the wrong tool

- **A take that is already sibilant.** Fix that in Syrinx first, then decide
  whether it still needs air. Usually it does not.
- **As a substitute for the EQ high shelf.** If there is real content up there,
  a shelf is cheaper and cleaner. Halo is for when there is not.

---

## 4. Ferrite — glue and movement

Tape. On a single vocal, use it for the **glue** — the gentle compression and
head-bump weight — rather than for the wobble.

On a **stack of doubles and ad-libs bussed together**, this is where it earns
its place: tape saturation across the group makes layers sit as one thing in a
way that a compressor on the bus does not.

Wow and flutter on a lead vocal are a deliberate effect, not a default. A
little flutter on an ad-lib can be good. Wow on a lead sounds like a broken
tape machine, because that is what it is modelling.

### Where it is the wrong tool

- **On every vocal track separately.** It is glue; glue works on a group.
- **When you want the vocal to sound modern and clean.** That is Syrinx alone.

---

## 5. Capstone — last, always

The ceiling. On the vocal bus, on the master, and **after everything else**.

The rule is not stylistic. Capstone is the only plugin in the suite that
guarantees a ceiling, and every plugin after it can exceed one. Put an EQ boost
after your limiter and the limiter's guarantee is worth nothing.

Its own gain reduction should be small on a vocal — a few dB of catching, not
levelling. If Capstone is doing 6 dB of work on a voice, Syrinx is not doing
its job and the fix is upstream.

### Where it is the wrong tool

- **As a compressor.** It has no ratio and is not trying to shape anything. Use
  Syrinx's two.
- **To make a vocal loud.** It makes it *not exceed* a ceiling. Loudness comes
  from levelling first.

---

## 6. Transpectus — checking

Not processing. Put it wherever you need to see something: after Syrinx to
check the levelling worked, on the master to check the loudness and the
correlation.

The two things worth checking on a vocal:

- **Correlation**, on a doubled or widened vocal. A vocal that reads negative
  is a vocal that disappears in mono, and a lot of playback is mono.
- **PLR/PSR**, to see how much dynamic range is left. A rap vocal that has been
  levelled well sits in a narrow band; if it is still wide, the strip is not
  finished.

---

## Two whole chains, concretely

### Rap lead, aggressive mix

```
Syrinx     "Rap Lead", trim so the leveller shows 4-6 dB
Emberdrive mid band only, moderate drive, mix 25%, auto-trim on
Halo       low amount, high focus
Capstone   ceiling -1.0 dBTP, a few dB of catching
```

Doubles and ad-libs go to their own bus with *Rap Double* / *Rap Ad-lib*, then
**Ferrite** across that bus for glue, then into the same Capstone.

### Sung vocal, spacious

```
Syrinx     "Sung Verse", gate off, leveller slow
Halo       moderate amount, wide focus
Ferrite    low drive, no wobble, for weight
Capstone   ceiling -1.0 dBTP
```

---

## The things that are not here

- **Pitch correction.** Ruled out permanently, at the user's direction. Use
  what you already use.
- **Reverb and delay.** Not in this suite yet. They go *after* Syrinx and
  *before* Capstone, on sends rather than inserts.
- **De-noise.** Not in this suite. A gate is not a denoiser.

---

## An honest note on all of this

Every number above is a starting point that came from how these plugins
measure, not from having mixed a record with them. The measurements are real
and quoted in each plugin's README. The *musical* claims — that this order
sounds better, that 25% mix is the right amount of grit — are starting points
for your ears to overrule.

Nothing in this suite has been loaded into FL Studio from the machine it was
built on. That test belongs to the rig, and it is the one that decides.
