#!/usr/bin/env bash
#
# Proves the determinism claim, locally, in under a minute - on Linux and macOS.
# tools/verify-replay.ps1 is the same check for Windows; the two are kept in
# step by hand, and the comments there say why each step exists. In brief:
#
# THREE STEPS, AND THE THIRD IS THE ONE THAT MATTERS.
#
#   1. Record a session.
#   2. Replay it. It must reproduce.
#   3. Corrupt one checkpoint and replay again. It must FAIL, at the tick that
#      was corrupted.
#
# Without step 3 this is a test that passes without checking anything. A replay
# harness that never diverges and a replay harness that cannot detect divergence
# produce identical output on step 2, and this project has shipped that shape of
# mistake before - see docs/planning/2026-08-28-determinism-audit.md, where the
# state hash agreed with everything because it was reading the wrong field.
#
# WHAT THIS DOES NOT PROVE. The demo scene reads no input inside a tick, so a
# green run here shows the simulation reproduces and NOT that recorded input
# drives it. That claim is carried by test_replay's last four cases, which run
# under ctest. Do not read this script as covering them.
#
# It needs a Vulkan device and somewhere to open a window. A GPU and a desktop
# are the ordinary case. A machine with neither is enough as well: Mesa's
# lavapipe is a Vulkan device in software (mesa-vulkan-drivers), and when there
# is no DISPLAY and xvfb-run is installed the engine is started under it - which
# is how a Linux box with no screen, this project's CI runner included, runs it.
#
#   tools/verify-replay.sh
#   tools/verify-replay.sh --config Release --scene assets/scenes/MainScene.scene
#   tools/verify-replay.sh --exe build-check/release/SupersonicEngine --frames 300
#
# Exit status is the verdict: 0 when the replay reproduces AND the corrupted
# one is caught at the right tick, non-zero otherwise, with the engine's output
# printed so the failure can be read.

set -euo pipefail

CONFIG="Debug"
SCENE="assets/scenes/MainScene.scene"
FRAMES=180
EXE=""

usage() {
    cat <<'USAGE'
usage: tools/verify-replay.sh [--config <Debug|Release>] [--scene <path>]
                              [--frames <n>] [--exe <engine binary>]
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --config) CONFIG="${2:?--config needs a value}"; shift 2 ;;
        --scene)  SCENE="${2:?--scene needs a path}"; shift 2 ;;
        --frames) FRAMES="${2:?--frames needs a count}"; shift 2 ;;
        --exe)    EXE="${2:?--exe needs a path}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# From the project root: asset paths resolve against the working directory, so
# running this from tools/ would fail to find shaders and look like a bug in the
# engine rather than in the invocation.
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Multi-config generators put the binary under build/<Config>/; single-config
# ones (Ninja, Makefiles) put it straight in build/. --exe overrides both, for
# a build tree somewhere else - tools/check.sh's, or a game's.
if [ -z "$EXE" ]; then
    for candidate in "build/$CONFIG/SupersonicEngine" "build/SupersonicEngine" \
                     "build/$CONFIG/SupersonicEngine.exe" "build/SupersonicEngine.exe"; do
        if [ -f "$candidate" ]; then EXE="$candidate"; break; fi
    done
fi
if [ -z "$EXE" ] || [ ! -f "$EXE" ]; then
    echo "No engine binary. Build first: cmake --build build --config $CONFIG" >&2
    exit 1
fi

# A window needs a display. On Linux with none, xvfb-run supplies one; the
# engine opens its window hidden anyway, so the virtual screen is never drawn
# to by anything else.
RUNNER=()
if [ "$(uname -s)" = "Linux" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        RUNNER=(xvfb-run -a)
        echo "No DISPLAY: running the engine under xvfb-run."
    else
        echo "No DISPLAY and no xvfb-run: the engine has nowhere to open a window." >&2
        echo "Install xvfb (and mesa-vulkan-drivers for a software Vulkan device)." >&2
        exit 1
    fi
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/supersonic-verify-replay.XXXXXX")"
# shellcheck disable=SC2064  # expand now: the value is fixed for the run
trap "rm -rf '$WORK'" EXIT
RECORDED="$WORK/session.replay"
CORRUPT="$WORK/corrupted.replay"
OUT="$WORK/engine.log"

# Output captured rather than streamed: the verdict line is what this script
# decides on, and the profiler report around it is noise here. THE EXIT CODE IS
# THE VERDICT, read explicitly, so a loader warning on stderr about somebody
# else's layer is not mistaken for a failure of the engine's.
CODE=0
invoke_engine() {
    set +e
    "${RUNNER[@]}" "$EXE" --hidden "$@" > "$OUT" 2>&1
    CODE=$?
    set -e
}

echo "1/3  Recording $FRAMES frames of $SCENE ..."
invoke_engine --frames "$FRAMES" --fixed-step --scene "$SCENE" --record "$RECORDED"
if [ "$CODE" -ne 0 ] || [ ! -f "$RECORDED" ]; then
    cat "$OUT"
    echo "Recording failed (exit $CODE)." >&2
    exit 1
fi
TICKS="$(grep -oE '^ticks [0-9]+$' "$RECORDED" | head -n 1 | cut -d' ' -f2)"
POINTS="$(grep -cE '^checkpoint ' "$RECORDED" || true)"
BYTES="$(wc -c < "$RECORDED" | tr -d ' ')"
echo "     ${TICKS:-?} tick(s), ${POINTS} checkpoint(s), ${BYTES} bytes"

echo "2/3  Replaying it ..."
invoke_engine --frames "$FRAMES" --fixed-step --scene "$SCENE" --replay "$RECORDED"
if [ "$CODE" -ne 0 ]; then
    cat "$OUT"
    echo "The replay did not reproduce, and it should have." >&2
    exit 1
fi
VERDICT="$(grep -m 1 'Replay reproduced' "$OUT" || true)"
if [ -z "$VERDICT" ]; then
    cat "$OUT"
    echo "The replay exited 0 but never said it reproduced." >&2
    exit 1
fi
echo "     $VERDICT"

# --- the negative control -------------------------------------------------
#
# One checkpoint, one line. The LAST checkpoint rather than the first, so the
# run has to get all the way there before disagreeing - corrupting tick zero
# would also pass a harness that only ever checks its first checkpoint.
echo "3/3  Corrupting one checkpoint, which must be caught ..."
if [ "${POINTS:-0}" -lt 2 ]; then
    echo "The recording has fewer than two checkpoints; there is nothing to corrupt meaningfully." >&2
    exit 1
fi
AT="$(grep -nE '^checkpoint [0-9]+ ' "$RECORDED" | tail -n 1 | cut -d: -f1)"
TICK="$(sed -n "${AT}p" "$RECORDED" | cut -d' ' -f2)"
awk -v at="$AT" -v tick="$TICK" \
    'NR == at { print "checkpoint " tick " deadbeefdeadbeef"; next } { print }' \
    "$RECORDED" > "$CORRUPT"

invoke_engine --frames "$FRAMES" --fixed-step --scene "$SCENE" --replay "$CORRUPT"
if [ "$CODE" -eq 0 ]; then
    cat "$OUT"
    echo "A corrupted checkpoint replayed CLEANLY. The oracle is not checking anything." >&2
    exit 1
fi
REPORTED="$(grep -m 1 'diverged at tick' "$OUT" || true)"
if [ -z "$REPORTED" ]; then
    cat "$OUT"
    echo "It failed, but never said where. A divergence with no tick number is not a bug report." >&2
    exit 1
fi
if ! echo "$REPORTED" | grep -qE "tick ${TICK}([^0-9]|$)"; then
    cat "$OUT"
    echo "Reported the wrong tick. Corrupted $TICK, got: $REPORTED" >&2
    exit 1
fi
echo "     $REPORTED"

echo
echo "Replay reproduces, and a one-byte change is caught at the right tick."
