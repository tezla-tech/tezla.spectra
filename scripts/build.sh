#!/usr/bin/env bash
# ============================================================================
#  tezla.spectra build script for macOS and Linux.
#
#  On macOS this is the primary way to build: VST3 and Audio Unit, universal
#  arm64 + x86_64 by default. On Linux it is mainly how the framework-free DSP
#  gets built and measured without a DAW.
#
#    ./scripts/build.sh                     DSP core + tests only, no JUCE
#    ./scripts/build.sh Emberdrive          one plugin
#    ./scripts/build.sh Emberdrive,Foo      a list
#    ./scripts/build.sh ALL --install       everything, installed for the user
#    ./scripts/build.sh --list              show available plugins
#
#  Options:
#    --config <cfg>   Debug | Release | RelWithDebInfo   (default Release)
#    --install        copy the built plugins to the user plug-in folders
#    --test           run the DSP unit tests after building
#    --clean          delete the build folder first
#    --build-dir <d>  use a different build folder
#    --juce <path>    use a JUCE source tree you already have
#    --juce-system    use a JUCE installed with `cmake --install`
#    --native         macOS only: build for this Mac's architecture alone,
#                     which halves build time while you are iterating
#    --help
#
#  Everything here has a documented manual equivalent in docs/BUILD-MACOS.md.
# ============================================================================

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build"
plugins="NONE"
config="Release"
run_tests=0
do_install=0
extra_args=()

case "$(uname -s)" in
    Darwin) platform="macos" ;;
    Linux)  platform="linux" ;;
    *)      platform="other" ;;
esac

usage() { sed -n '3,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

list_plugins() {
    echo "Available plugins:"
    local found=0
    for dir in "${repo_root}"/plugins/*/; do
        if [[ -f "${dir}CMakeLists.txt" ]]; then
            echo "   $(basename "${dir}")"
            found=1
        fi
    done
    [[ ${found} -eq 0 ]] && echo "   (none yet)"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)      config="$2"; shift 2 ;;
        --build-dir)   build_dir="$2"; shift 2 ;;
        --juce)        extra_args+=("-DTEZLA_JUCE_PATH=$2"); shift 2 ;;
        --juce-system) extra_args+=("-DTEZLA_JUCE_SOURCE=System"); shift ;;
        --native)      extra_args+=("-DTEZLA_UNIVERSAL_BINARY=OFF"); shift ;;
        --test)        run_tests=1; shift ;;
        --install)     do_install=1; shift ;;
        --clean)       rm -rf "${build_dir}"; shift ;;
        --list)        list_plugins; exit 0 ;;
        -h|--help)     usage; exit 0 ;;
        --plugins)     plugins="$2"; shift 2 ;;          # kept for compatibility
        -*)            echo "unknown option: $1" >&2; exit 1 ;;
        *)             plugins="$1"; shift ;;            # a bare name is the plugin list
    esac
done

# ---------------------------------------------------------------- tools -----
for tool in cmake git; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "ERROR: ${tool} was not found." >&2
        if [[ "${platform}" == "macos" ]]; then
            echo "       Install it with:  brew install ${tool}" >&2
            echo "       See docs/BUILD-MACOS.md." >&2
        fi
        exit 1
    fi
done

if [[ "${platform}" == "macos" ]] && ! xcode-select -p >/dev/null 2>&1; then
    echo "ERROR: the Xcode Command Line Tools are not installed." >&2
    echo "       Run:  xcode-select --install" >&2
    exit 1
fi

generator=()
command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)

# ------------------------------------------------------------ configure -----
echo "Configuring (${config}, plugins: ${plugins})..."
cmake -S "${repo_root}" -B "${build_dir}" "${generator[@]}" \
      -DCMAKE_BUILD_TYPE="${config}" \
      -DTEZLA_PLUGINS="${plugins}" \
      "${extra_args[@]}"

echo "Building..."
cmake --build "${build_dir}" --parallel

# ----------------------------------------------------------------- test -----
if [[ ${run_tests} -eq 1 ]]; then
    echo "Running DSP tests..."
    test_exe="$(find "${build_dir}" -name tezla-tests -type f -perm -u+x | head -1)"
    if [[ -z "${test_exe}" ]]; then
        echo "ERROR: tezla-tests was not built." >&2
        exit 1
    fi
    "${test_exe}"
fi

# -------------------------------------------------------------- report ------
echo
bundles=()
while IFS= read -r line; do bundles+=("${line}"); done < <(
    find "${build_dir}" -maxdepth 8 \( -name "*.vst3" -o -name "*.component" \) 2>/dev/null | sort)

if [[ ${#bundles[@]} -eq 0 ]]; then
    echo "Done (no plugin targets were selected)."
    exit 0
fi

echo "Built:"
for bundle in "${bundles[@]}"; do echo "   ${bundle}"; done

# ------------------------------------------------------------- install ------
if [[ ${do_install} -eq 0 ]]; then
    echo
    echo "Re-run with --install to copy these into your plug-in folders."
    exit 0
fi

if [[ "${platform}" != "macos" ]]; then
    echo
    echo "--install only knows where plug-ins go on macOS. On Linux, copy the"
    echo "bundles wherever your host scans."
    exit 0
fi

# The user folders, not the system ones: no sudo, and nothing to undo later.
vst3_dir="${HOME}/Library/Audio/Plug-Ins/VST3"
au_dir="${HOME}/Library/Audio/Plug-Ins/Components"
mkdir -p "${vst3_dir}" "${au_dir}"

echo
for bundle in "${bundles[@]}"; do
    name="$(basename "${bundle}")"
    case "${name}" in
        *.vst3)      destination="${vst3_dir}/${name}" ;;
        *.component) destination="${au_dir}/${name}" ;;
        *)           continue ;;
    esac

    rm -rf "${destination}"
    cp -R "${bundle}" "${destination}"
    echo "Installed ${destination}"

    # A bundle built locally is not quarantined, but one that has ever been
    # through a zip, a download or AirDrop is -- and Gatekeeper then refuses to
    # load it with an error that blames the plugin rather than the quarantine.
    xattr -dr com.apple.quarantine "${destination}" 2>/dev/null || true
done

echo
echo "Logic and GarageBand cache Audio Unit scans. If a rebuilt AU does not"
echo "appear, run:  killall -9 AudioComponentRegistrar"
