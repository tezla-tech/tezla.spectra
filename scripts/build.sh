#!/usr/bin/env bash
# Linux/macOS build, mainly for CI and for working on the framework-free DSP.
# The plugins themselves target Windows; this path is how the DSP gets tested
# and measured without a DAW.
#
#   ./scripts/build.sh                 # DSP core + tests + tools
#   ./scripts/build.sh --plugins ALL   # also build plugin targets (needs JUCE deps)
#   ./scripts/build.sh --test

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build"
plugins="NONE"
config="Release"
run_tests=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --plugins) plugins="$2"; shift 2 ;;
        --config)  config="$2";  shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --test)    run_tests=1; shift ;;
        --clean)   rm -rf "${build_dir}"; shift ;;
        -h|--help)
            sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi

cmake -S "${repo_root}" -B "${build_dir}" "${generator[@]}" \
      -DCMAKE_BUILD_TYPE="${config}" \
      -DTEZLA_PLUGINS="${plugins}"

cmake --build "${build_dir}" --parallel

if [[ ${run_tests} -eq 1 ]]; then
    "${build_dir}/bin/tezla-tests"
fi
