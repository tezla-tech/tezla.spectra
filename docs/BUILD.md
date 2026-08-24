# Build guide

Everything here is free. No IDE is required, no SDK has to be downloaded by
hand, and a clean clone builds with one command.

---

## 1. Install the tools (Windows 11)

| Tool | Why | Where |
|---|---|---|
| **Visual Studio 2022 Build Tools** | The C++ compiler (MSVC). The command-line-only package; the full Visual Studio Community edition works too if you prefer an IDE. | <https://visualstudio.microsoft.com/downloads/> → *Tools for Visual Studio* → *Build Tools for Visual Studio 2022* |
| **CMake** ≥ 3.22 | The build system. Tick **"Add CMake to the system PATH"** in the installer. | <https://cmake.org/download/> |
| **Git** | Fetches the repository and JUCE. | <https://git-scm.com/download/win> |
| **Ninja** *(optional)* | Noticeably faster builds. CMake ships with a copy, or install standalone. | <https://github.com/ninja-build/ninja/releases> |

When installing the Build Tools, select the **"Desktop development with C++"**
workload. That single checkbox brings in the compiler, the Windows SDK and the
CMake integration. Nothing else is needed.

### Verify

Open a new PowerShell window and check:

```powershell
cmake --version      # 3.22 or newer
git --version
```

You do **not** need to open a "Developer Command Prompt" — CMake locates MSVC
itself.

---

## 2. Build

```powershell
git clone https://github.com/wingit33/tezla.tech.git
cd tezla.tech

.\scripts\build.ps1
```

The first configure downloads JUCE (a few hundred MB, once — it is cached in
`build/`). Later builds skip that.

### Options

```powershell
.\scripts\build.ps1 -List                     # what plugins exist
.\scripts\build.ps1                           # all plugins, Release
.\scripts\build.ps1 -Plugins Foo              # one plugin
.\scripts\build.ps1 -Plugins Foo,Bar          # a list
.\scripts\build.ps1 -Config Debug             # debug build
.\scripts\build.ps1 -Install                  # copy to the system VST3 folder
.\scripts\build.ps1 -Test                     # run the DSP tests afterwards
.\scripts\build.ps1 -Clean                    # wipe build/ first
.\scripts\build.ps1 -Plugins NONE -Test       # DSP + tests only; skips JUCE entirely
```

From `cmd.exe` instead of PowerShell, use `scripts\build.bat` with the same
arguments.

**`-Plugins NONE` is the fast loop.** It builds the framework-free DSP, its
tests and the measurement tools, needs no JUCE and no download, and finishes in
seconds. Use it while working on DSP; only build a plugin target when you
actually want to load something into FL Studio.

---

## 3. Install into FL Studio

`-Install` copies the built bundles to `C:\Program Files\Common Files\VST3\`,
which is the folder FL Studio scans by default. Writing there needs an
**Administrator** PowerShell — right-click PowerShell → *Run as administrator*.

Without `-Install`, the bundles are left in
`build\<Plugin>_artefacts\<Config>\VST3\` and you can copy them yourself or
point FL Studio at that folder.

Then in FL Studio: **Options → Manage plugins → Find more plugins**. Tick
*Verify plugin signatures* off if a fresh build is not picked up, and use
**Rescan previously verified plugins** after replacing an existing build.

> FL Studio caches plugin metadata. After rebuilding a plugin whose parameters
> changed, rescan it, or FL will keep showing the old parameter list.

---

## 4. Building on Linux (optional, for CI and for checking the wrapper)

The plugins target Windows, but the plugin target does build on Linux, and doing
so is a cheap way to catch wrapper mistakes before they reach the DAW — the
Steinberg validator runs there too. On Ubuntu 24.04:

```bash
sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libxext-dev libxcomposite-dev libasound2-dev libfreetype6-dev \
    libfontconfig1-dev libgl1-mesa-dev

./scripts/build.sh --plugins Emberdrive
```

This is *not* a substitute for building on Windows — MSVC finds things g++ does
not — but it removes most of the round trips.

---

## 5. Build without a DAW

The DSP is deliberately independent of JUCE, so it builds and runs anywhere:

```powershell
.\scripts\build.ps1 -Plugins NONE -Test
```

```bash
./scripts/build.sh --test          # Linux/macOS, for CI or DSP work
```

Measurement tools:

```
build\bin\tezla-measure selftest
build\bin\tezla-measure clip-aliasing --fs 48000 --drive 4
build\bin\tezla-measure filter-response --freq 1000 --q 0.707 --out response.csv
```

`selftest` verifies the analysis chain itself before you trust any number it
produces about a plugin.

---

## 6. Troubleshooting

**`cmake` is not recognised**
The CMake installer's "Add to PATH" box was not ticked. Re-run the installer, or
add `C:\Program Files\CMake\bin` to PATH. Open a *new* terminal afterwards.

**`No CMAKE_CXX_COMPILER could be found`**
The Build Tools are missing the C++ workload. Re-run the Visual Studio Installer,
*Modify*, and tick **Desktop development with C++**.

**The JUCE download fails or is very slow**
Configure once with a local checkout instead:
```powershell
git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE C:\dev\JUCE
.\scripts\build.ps1 -Plugins Foo   # after adding -DTEZLA_JUCE_PATH=C:\dev\JUCE
```
or pass it directly to CMake:
```powershell
cmake -S . -B build -DTEZLA_JUCE_PATH=C:\dev\JUCE -DTEZLA_PLUGINS=Foo
```

**Access denied when installing**
`-Install` writes to `C:\Program Files`. Use an Administrator shell.

**FL Studio does not see the plugin**
Confirm the `.vst3` is in `C:\Program Files\Common Files\VST3\`, then rescan.
Check it is a 64-bit build — this repository builds x64 only, and a 32-bit host
will not load it.

**The plugin loads but sounds wrong at 192 kHz**
That is a bug in the plugin, not the host. Report the session sample rate; see
the sample-rate policy in [`../CLAUDE.md`](../CLAUDE.md).

**`A C compiler is required to build targets that depend on JUCE`**
The top-level `project()` must list `C` as well as `CXX` — JUCE vendors zlib,
libpng and friends as C. Already handled; noted here because the error message
points at JUCE rather than at the cause.

---

## 7. Adding a new plugin

1. `plugins/<Name>/` with `CMakeLists.txt`, `Source/`, `Dsp/`, `README.md`.
2. Claim a unique 4-character plugin code in [`../plugins/README.md`](../plugins/README.md).
3. Use `tezla_add_plugin()` from `cmake/TezlaPlugin.cmake` — it applies the
   shared vendor identity, formats and warning settings.
4. Put the DSP in `Dsp/` with no JUCE includes, and add tests in `tests/`.

The top-level `CMakeLists.txt` discovers any folder under `plugins/` that has a
`CMakeLists.txt`. Nothing else needs editing.
