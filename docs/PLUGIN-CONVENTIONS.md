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
- Keyboard: double-click to reset a control to default, shift-drag for fine
  adjustment, mouse wheel works everywhere.
- No modal dialogs, no splash screens, no network access, no telemetry.

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
