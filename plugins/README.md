# Plugins

One self-contained folder per plugin. A plugin folder should be deletable
without breaking anything else in the repository.

The top-level `CMakeLists.txt` discovers any folder here that contains a
`CMakeLists.txt`. Nothing else needs editing to add one.

---

## Registry

**Every plugin claims a unique 4-character plugin code here before it is
written.** The code plus the manufacturer code `Tzla` generates the VST3 unique
ID. Two plugins sharing a code produce the same ID, and the host will load
whichever it happened to scan first — a confusing failure that looks like a
corrupt install. The build cannot detect this; this table is the only guard.

| Code | Folder | Product name | Type | Status |
|------|--------|--------------|------|--------|
| `Tzem` | [`Emberdrive`](Emberdrive/) | Tezla Emberdrive | Effect — saturation, destruction, 3-band limiter, with a modulation layer | v0.4.0, builds and validates |
| `Tzha` | [`Halo`](Halo/) | Tezla Halo | Effect — harmonic exciter and bass enhancer, with a Chebyshev precision mode and a modulation layer | v0.3.0, builds and validates |
| `Tzcp` | [`Capstone`](Capstone/) | Tezla Capstone | Effect — true-peak brickwall limiter and clipper for the end of the chain | v0.1.0, builds and validates |
| `Tztr` | [`Transpectus`](Transpectus/) | Tezla Transpectus | Analyser — loudness, true peak, spectrum and stereo image. Does not touch the audio | v0.1.0, builds and validates |

Codes are case-sensitive and conventionally start with `Tz`.

### Reserved names

Held for the plugins already sketched out, so they do not get used for
something else in the meantime:

| Name | Code | Intended for |
|------|------|--------------|
| `Ferrite` | `Tzfe` | Plugin #2 — the tape machine proper: wow, flutter, hysteresis, head bump |
| `Anvil` | `Tzan` | Plugin #3 — amp and cabinet distortion, the Steinberg Warp lane |
| `Prism` | `Tzpr` | The multiband enhancer — four bands of harmonics plus per-band stereo width. Deliberately its own plugin rather than a mode of Halo: it costs considerably more CPU and signal complexity than belongs in a tool used on a single channel |

---

## Layout of a plugin folder

```
plugins/<Name>/
├── CMakeLists.txt     # one tezla_add_plugin() call
├── README.md          # what it is, every control, design notes, changelog
├── Dsp/               # framework-free C++20. No JUCE includes. Testable alone.
├── Source/            # thin JUCE layer: parameters, state, editor
└── Resources/         # graphics, presets
```

The `Dsp/` and `Source/` split is not optional — see
[`../CLAUDE.md`](../CLAUDE.md) §4. `Dsp/` must compile with plain g++/clang/MSVC
and nothing else, so that its behaviour can be measured offline before it ever
reaches a DAW.

---

## Adding a plugin

1. Claim a code in the table above.
2. Create the folder and a `CMakeLists.txt`:

```cmake
tezla_add_plugin(
    NAME            Example
    PRODUCT_NAME    "Tezla Example"
    PLUGIN_CODE     Tzex
    DESCRIPTION     "One line the DAW will show"
    VST3_CATEGORIES Fx Distortion
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/ExampleEngine.cpp)
```

3. Write the DSP in `Dsp/`, with tests in `../../tests/`.
4. Write the JUCE wrapper in `Source/`.
5. Build it: `scripts\build.bat Example -install` on Windows, or
   `./scripts/build.sh Example` on macOS and Linux. Nothing depends on
   PowerShell — see `CLAUDE.md` §5.
6. Update the table above and the status table in the root `README.md`.
