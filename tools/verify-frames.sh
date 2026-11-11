#!/usr/bin/env bash
#
# Proves the renderer is repeatable, on any machine that can run the engine - a GPU,
# or Mesa's lavapipe in software under Xvfb.
#
# Two runs of one scene with identical arguments must write the same pixels and
# print the same [Counts]. That is the only image check that does not pin a driver:
# a golden PNG goes stale the day Mesa or a GPU vendor changes how it rounds a
# filtered sample, and then everyone learns to regenerate it without looking. Two
# runs on the SAME machine compare like with like, so a difference is the engine's.
# It catches what a renderer change can break that a unit test cannot see: an
# uninitialised value, a pass that reads last frame's contents, a draw order that
# depends on an address or a hash.
#
# TWO STEPS, AND THE SECOND IS THE ONE THAT MATTERS.
#
#   1. Render the scene twice. The pictures and the counts must match.
#   2. Render it again for more frames. The picture must DIFFER.
#
# Without step 2 this passes on a renderer that writes the same black image every
# time, and on a comparison that never compared anything. The scene has to move for
# it to be a control, and AllPasses does (a particle burst, a spinning model); one
# that did not would be reported rather than passed.
#
#   tools/verify-frames.sh
#   tools/verify-frames.sh --config Release --scene assets/scenes/MainScene.scene
#   tools/verify-frames.sh --exe build-check/release/SupersonicEngine --frames 60
#
# Exit status is the verdict. Not a pixel-for-pixel check against a committed image,
# and not a check across machines: see above.

set -euo pipefail

CONFIG="Debug"
SCENE="assets/scenes/AllPasses.scene"
FRAMES=40
WINDOW="1280x720"
EXE=""

usage() {
    cat <<'USAGE'
usage: tools/verify-frames.sh [--config <Debug|Release>] [--scene <path>] [--frames <n>]
                              [--window <WxH>] [--exe <engine binary>]
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --config) CONFIG="${2:?--config needs a value}"; shift 2 ;;
        --scene)  SCENE="${2:?--scene needs a path}"; shift 2 ;;
        --frames) FRAMES="${2:?--frames needs a count}"; shift 2 ;;
        --window) WINDOW="${2:?--window needs WxH}"; shift 2 ;;
        --exe)    EXE="${2:?--exe needs a path}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# From the project root: asset paths resolve against the working directory.
cd "$(dirname "${BASH_SOURCE[0]}")/.."

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

WORK="$(mktemp -d "${TMPDIR:-/tmp}/supersonic-verify-frames.XXXXXX")"
# shellcheck disable=SC2064  # expand now: the value is fixed for the run
trap "rm -rf '$WORK'" EXIT

# One run: picture to $2, output to $3. The exit code is read explicitly (a loader
# warning on stderr is not a failure of the engine's), and ${RUNNER[@]+...} because
# an empty array under `set -u` is an unbound variable in bash before 4.4.
render() {
    local frames="$1" picture="$2" log="$3" code=0
    set +e
    ${RUNNER[@]+"${RUNNER[@]}"} "$EXE" --hidden --frames "$frames" --fixed-step \
        --window "$WINDOW" --scene "$SCENE" --screenshot "$picture" > "$log" 2>&1
    code=$?
    set -e
    if [ "$code" -ne 0 ] || [ ! -s "$picture" ]; then
        cat "$log"
        echo "The engine failed (exit $code) rendering $SCENE for $frames frames." >&2
        exit 1
    fi
}

# The [Counts] block: what the frame drew, as a number of draws and binds. A change
# that moves a count without moving a pixel is still a change worth seeing.
#
# Exactly the block: the header line and the indented rows under it, nothing after. A
# fixed number of trailing lines (grep -A) took in whatever the engine logged next - for
# a scene with fewer counters, a line carrying the pipeline cache's size, which differs
# between a first run and a second - and failed a repeatable scene as unrepeatable.
counts() { awk '/^\[Counts\]/ {inblock=1; print; next} inblock && /^  / {print; next} {inblock=0}' "$1"; }

echo "1/2  Rendering $FRAMES frames of $SCENE twice ..."
render "$FRAMES" "$WORK/a.png" "$WORK/a.log"
render "$FRAMES" "$WORK/b.png" "$WORK/b.log"

if ! cmp -s "$WORK/a.png" "$WORK/b.png"; then
    echo "Two runs of the same scene wrote different pictures." >&2
    echo "  $(md5sum "$WORK/a.png" 2>/dev/null || md5 -q "$WORK/a.png")" >&2
    echo "  $(md5sum "$WORK/b.png" 2>/dev/null || md5 -q "$WORK/b.png")" >&2
    exit 1
fi
if [ -z "$(counts "$WORK/a.log")" ]; then
    echo "The run printed no [Counts] block, so there is nothing to compare there." >&2
    exit 1
fi
if [ "$(counts "$WORK/a.log")" != "$(counts "$WORK/b.log")" ]; then
    echo "Two runs of the same scene counted different draws:" >&2
    diff <(counts "$WORK/a.log") <(counts "$WORK/b.log") >&2 || true
    exit 1
fi
echo "     the same picture and the same counts, twice"

# --- the negative control --------------------------------------------------
echo "2/2  Rendering $((FRAMES + 20)) frames, which must look different ..."
render "$((FRAMES + 20))" "$WORK/c.png" "$WORK/c.log"
if cmp -s "$WORK/a.png" "$WORK/c.png"; then
    echo "Twenty more frames of $SCENE wrote the SAME picture." >&2
    echo "Either the comparison cannot see a difference or the scene does not move; a check" >&2
    echo "that cannot fail is not checking. Pick a scene with something animated in it." >&2
    exit 1
fi
echo "     a later frame is a different picture"

echo
echo "The renderer is repeatable, and the comparison can tell frames apart."
