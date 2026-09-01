# Plugin conventions

House rules every plugin follows, so they feel like one family and so a project
saved today still opens in two years.

---

## Identity

| Field | Value |
|---|---|
| Vendor | `Tezla Tech` |
| Website | `https://tezla.tech` |
| Manufacturer code | `Tzla` |
| Bundle ID | `tech.tezla.<PluginName>` |
| Plugin code | 4 characters, unique per plugin — **claim it in [`../plugins/README.md`](../plugins/README.md)** |

The plugin code and manufacturer code together generate the VST3 unique ID. Two
plugins sharing a code will fight over the same ID and the host will load
whichever it saw first. `tezla_add_plugin()` rejects a code that is not exactly
four characters, but it cannot detect a collision — the registry is the only
guard, so keep it current.

**None of the four values above may change once a plugin has shipped**, and in
particular a project rename must not touch them. The project is *tezla.spectra*;
the company is *Tezla Tech*; the domain is *tezla.tech*. Only the first is a
project name. Changing the manufacturer or plugin code changes the plugin's
identity, and every saved project loses its instances — the host looks for an ID
that no longer exists and reports the plugin as missing. See
[`../CLAUDE.md`](../CLAUDE.md) §8.

---

## Parameters

- **String IDs are permanent.** `"drive"` stays `"drive"` forever. Renaming one
  silently resets that parameter in every saved project.
- **Never reorder or delete.** VST3 hosts index automation by ID, but FL Studio's
  own automation clips and the plugin's linked-controller state are order
  sensitive. Add new parameters at the end. Retire an old one by hiding it, not
  by removing it.
- **Version the layout.** Store a schema version in the plugin state. When the
  layout changes, migrate old state on load rather than discarding it.
- **Ranges are musical, not mathematical.** dB where the user thinks in dB, ms
  where they think in ms, Hz on a log skew. Put the useful range in the middle
  of the travel — a drive control that does everything interesting in its first
  15% is a broken control.
- **Default = neutral.** Loading the plugin should change the sound as little as
  possible, so the first thing a user hears is their own material.
- **Smooth everything continuous.** 10–50 ms. Discrete switches crossfade.
- Attach units and a sensible `toString`: `"+6.0 dB"`, `"12.5 ms"`, `"4.2 kHz"`.

---

## Tooltips

Every control has one, and it says **what the control does and what it costs**.

Tooltips are how this workshop documents itself — there is no manual, and there
does not need to be one if the tooltips are honest.

Good:

> **Oversampling — Auto.** Runs the saturation stage near 192 kHz internally so
> distortion lands on real harmonics instead of folding back as aliasing. Your
> session is at 96 kHz, so Auto is running ×2 (+1.1 ms latency, compensated). At
> a 192 kHz session Auto switches off — the headroom is already there.

Bad:

> Sets the oversampling factor.

Where a control's behaviour depends on the host — sample rate, block size,
channel count — the tooltip reads the **live** value and says what is happening
*right now*. Do not make the user work it out.

---

## Presets

Small and opinionated beats large and generic. Aim at the actual work:

- **Clean** — genuinely transparent. Proof the plugin can get out of the way.
- **Drum bus** — glue and weight without killing transients.
- **Sub bass** — harmonics that let a 40 Hz sine survive a phone speaker,
  without a wandering DC offset.
- **Reese / mid bass** — aggressive, the reason this repository exists.
- **Mix glue** — the gentlest useful setting.

Name presets for what they are for, not for gear they resemble.

**A preset is a complete parameter set or it is a trap.** Switching from one to
another must not leave the first one's settings behind on any control the second
does not mention. Two ways to hold that guarantee, and which one is clearer
depends on the count:

- Under about thirty parameters, write every one out longhand. Anvil does, and
  reading the table tells you the whole patch.
- Past that, reset every parameter to its default first and then apply the
  departures. Sonitus does, with a hundred and fifty; longhand there would be a
  hundred and fifty lines per preset in which the four that matter are
  invisible. This only works because §Parameters requires every default to be
  neutral, so "the defaults" is itself a valid preset.

**An instrument's presets do not carry its tuning.** A scale is loaded by the
player and outlives the patch they are auditioning, and a preset that silently
reset it to 12-TET would be a bug rather than a feature.

---

## Metering and gain

- **Auto output trim** on any drive control, defaulting to on. Loudness sells
  distortion; we are trying to judge tone.
- **VU ballistics (300 ms integration)** where a plugin claims analogue
  behaviour — it is part of how these units are actually used. A peak meter next
  to it, not instead of it.
- **Honest overload indication.** Show internal clipping, not just output
  clipping.

---

## UI

- Resizable, readable at high DPI, sane at 100% zoom on a 4K display.
- Function before flourish. Clear metering beats skeuomorphic decoration.
- Double-click resets a control to **the parameter's own default**, not to the
  middle of its range — on a skewed range those are somewhere else entirely.
  Shift-drag is fine adjustment.
- **The wheel scrolls the panel. It never moves a control.** These panels
  scroll, and on a scrolling panel a wheel that also edits is a trap with no
  feedback: the pointer passes over Detune on the way down, the page does not
  move, and a patch has silently changed by three cents. Every `Slider` and
  `ComboBox` is built with the wheel off — `ui::noWheel` at the point of
  construction, `ui::sweepNoWheel` over the finished tree as a net for controls
  a page builds later. See `shared/tezla-ui/include/tezla/ui/ScrollWheel.hpp`.
- No modal dialogs, no splash screens, no network access, no telemetry.

### The house panel design

The numbers live once, in `shared/tezla-ui/include/tezla/ui/PanelDesign.hpp`,
and were chosen by building eight variants of a real panel and photographing
them rather than by drawing mockups. What they say:

- **A hue per group**, 18 degrees apart, rotated off the plugin's own accent in
  hue only — saturation and lightness are held, because those are what each
  plugin's contrast measurement was made against. The hue is carried by the
  heading, a spine down the plate's left edge, the control names (mixed 40% of
  the way from the dim grey, so a group reads as *warm* or *cool* before it
  reads as coloured), the knob tracks, and the dropdowns.
- **A size hierarchy.** A group's lead control is drawn 1.32× and a set-once
  trim 0.74×, so the eye has something to land on. The cell keeps its footprint
  either way; only the control inside it moves.
- **Knobs sit in a countersunk well**, lit from above, with a machined skirt.
  Geometry separates a knob from its plate, not hue — a body drawn a few points
  lighter than the plate behind it reads as a smudge.
- **Never a tick box.** An on/off control is `ui::LampButton`: a moulded cap in
  a recessed bezel that travels when pressed and lights red, with a halo. It is
  **red on every group** on purpose — a power switch is red on every box in a
  rack precisely so it can be read without first being identified.
- **A dropdown wears its group's colour** — a tab down its leading edge, a wash
  across the fill, a full-strength outline — because it holds a word where its
  neighbours hold a number, which makes it the control most easily mistaken for
  a caption. Opt-in per control through a `tezlaTint` component property, so a
  panel that is not knobs-on-plates is unaffected.
- **A group fills its row.** Column counts at the call sites are a request, not
  an instruction: the layout raises them until the row is full, down to
  `design::kCellWidthMin`, rather than centring a short group in a sea of metal.

---

## Latency and bypass

- **Report latency to the host**, and re-report whenever it changes
  (oversampling factor, lookahead). FL Studio's PDC depends on it.
- **Bypass is latency-matched and click-free.** A bypass that changes the
  timing makes A/B comparison a lie.
- Crossfade in and out of bypass over a few milliseconds.

---

## State

- `prepareToPlay` clears all filter and envelope state. No pops on transport
  restart, no state leaking between instances.
- Saved state is the parameter values plus the schema version — never raw DSP
  state.
- The plugin must survive `prepareToPlay` being called with a different sample
  rate, block size or channel count at any time.

---

## Versioning

`MAJOR.MINOR.PATCH` per plugin, independent of the repository version.

- **PATCH** — fixes that do not change the sound.
- **MINOR** — new features; existing presets still sound the same.
- **MAJOR** — the sound changed. Say so in the plugin's `README.md`, and keep
  the old behaviour reachable if projects depend on it.

A change that alters existing projects' sound without a major bump is a bug.
