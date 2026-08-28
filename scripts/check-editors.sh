#!/usr/bin/env bash
# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#
# Drives every plugin's editor offscreen and checks the things a unit test
# cannot reach.
#
# `tests/` is framework-free by design (CLAUDE.md section 4), which leaves the
# JUCE layer -- layout arithmetic, click wiring, component lifetimes -- with no
# coverage at all. `tezla-render <Plugin> editor` fills that gap, and this runs
# it across the suite so a wiring mistake in one plugin cannot hide behind five
# that work.
#
# The TIPS button is why this exists. It was wired in Sonitus and in none of the
# other five, and from outside there was no way to tell: a button whose callback
# is null looks exactly like one whose callback works.
#
# Needs a build configured with -DTEZLA_BUILD_RENDER=ON, and an X server or
# xvfb-run.
#
#   scripts/check-editors.sh [build-dir]

set -uo pipefail

BUILD="${1:-build-plugin}"
PLUGINS="Emberdrive Halo Capstone Anvil Transpectus Sonitus"

if command -v xvfb-run >/dev/null 2>&1; then
    RUN="xvfb-run -a"
else
    RUN=""
fi

failures=0

note_failure()
{
    echo "    FAIL: $1"
    failures=$((failures + 1))
}

for plugin in $PLUGINS; do
    binary="$BUILD/plugins/$plugin/${plugin}Render_artefacts/Release/${plugin}Render"

    if [ ! -x "$binary" ]; then
        note_failure "$plugin: no render binary at $binary"
        continue
    fi

    echo "  $plugin"

    # ---- the editor stands up, resizes through its range, and destroys ------
    out=$($RUN "$binary" editor 2>&1)

    if ! echo "$out" | grep -q "editor destroyed cleanly"; then
        note_failure "$plugin: the editor did not survive being created and resized"
        echo "$out" | sed 's/^/      /'
        continue
    fi

    # ---- the TIPS button actually reaches the flag it claims to set ---------
    #
    # Read, click, read, click, read. Both edges, because a callback that sets
    # the flag to a constant passes a one-way check.
    out=$($RUN "$binary" editor \
              state:tooltipsEnabled tips state:tooltipsEnabled tips state:tooltipsEnabled 2>&1)

    values=$(echo "$out" | sed -n 's/.*tooltipsEnabled = \(.*\)/\1/p' | tr -d ' \n')

    if [ "$values" != "101" ]; then
        note_failure "$plugin: TIPS read '$values', wanted '101' -- the toggle is not wired"
        echo "$out" | sed 's/^/      /'
    fi
done

if [ "$failures" -ne 0 ]; then
    echo
    echo "$failures editor check(s) failed"
    exit 1
fi

echo
echo "all editor checks passed"
