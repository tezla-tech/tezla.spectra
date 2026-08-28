# Svarayantra reference material

Fetched 2026-08-28 for the SoundFont player. These are **third-party works,
(c) their authors, under their own licences** — not part of this project's
licensed work (see the root `NOTICE.md`).

| File | Origin | Licence | Role |
|---|---|---|---|
| `tsf.h` | TinySoundFont, github.com/schellingb/TinySoundFont | MIT | Reference implementation. Confirms the hydra record layouts byte for byte, the unit conversions (timecents = 2^(tc/1200) s; cents→Hz anchored at 8.176 Hz; centibel sustain), and the −12000 tc "effectively instant" convention |
| `fluidsynth-fluid_gen.c/.h` | FluidSynth, github.com/FluidSynth/fluidsynth | LGPL-2.1-or-later | Carries the SF2 spec §8.1.3 generator table verbatim — number, range and default per generator. The defaults Svarayantra uses are checked against this table |
| `fluidsynth-fluid_sffile.c/.h` | FluidSynth | LGPL-2.1-or-later | The battle-tested parser, for chunk-order and validation behaviour |
| `fluidsynth-fluid_mod.c`, `fluidsynth-fluid_voice.c` | FluidSynth | LGPL-2.1-or-later | The spec's default modulators (§8.4) and the voice pitch/gain arithmetic |
| `fluidsynth-fluid_adsr_env.h`, `fluidsynth-types.h` | FluidSynth | LGPL-2.1-or-later | Envelope segment behaviour |

**The SoundFont 2.04 specification PDF itself could not be fetched** — the
proxy refuses `synthfont.com` and the mirrors tried. Everything implemented
rests on the two implementations above agreeing with each other, and is
recorded as such in `docs/DSP-REFERENCES.md`. If a wild soundfont renders
wrongly, the spec is the arbiter: the user can supply `sfspec24.pdf`
(e.g. from https://www.synthfont.com/sfspec24.pdf).

**Usage discipline (CLAUDE.md §9):** consulted for defined behaviour — record
layouts, generator defaults, unit formulas — which is exactly the knowledge
measurement cannot verify. Code is written fresh against those facts;
anything taken more directly is attributed at the point of use as well.
