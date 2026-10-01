#!/usr/bin/env bash
#
# Local checks, for Linux and macOS: the same ones CI runs, runnable without
# paying for a runner.
#
# CI is dispatch-only on purpose (the repository is private, so every automatic
# run is charged), and in the ten days after automatic runs were switched off the
# documented suite count drifted from 36 to 61 while the job built to catch it
# waited for a person to press a button. A check that has to be dispatched
# reports on request. So the ones that must not wait are here, and the `docs` and
# `shaders` steps are the SAME code CI runs - CI calls this file for them - so
# the two cannot drift apart.
#
#   tools/check.sh                run the default set: docs shaders build
#   tools/check.sh docs           documented counts match the build
#   tools/check.sh shaders        every shader recompiles to its committed SPIR-V
#   tools/check.sh build          Release build, then every suite
#   tools/check.sh options        configure with tests and plugin OFF, build the core
#   tools/check.sh asan           ASan + UBSan + LSan build, every suite
#   tools/check.sh tsan           ThreadSanitizer build, the suites that use threads
#   tools/check.sh tidy           clang-tidy over the engine's own sources
#   tools/check.sh all            everything above
#
# Not here: tools/verify-replay.ps1's record / replay / corrupt-a-checkpoint round
# trip. It needs a window and a Vulkan device, which this script cannot assume, and
# a port that has never been run is a check that might not be checking. Run the
# PowerShell one before pushing anything that touches the tick loop, the state
# hash or the recording format (AGENTS.md).
#
# Build trees go under $CHECK_BUILD_ROOT (default build-check/, already ignored).
# JOBS bounds parallelism; the default is the core count, which CI deliberately
# does not use because forty translation units each pulling in vulkan.hpp is what
# the OOM killer took a runner for. Set JOBS=2 on a small machine.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD_ROOT="${CHECK_BUILD_ROOT:-build-check}"
JOBS="${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2) )}"

# Ninja when there is one, as CI's contributors use. Otherwise CMake's default.
GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi

# GitHub turns "::error file=...::message" into an annotation on the file. Anywhere
# else it is just noise, so print something a person can read instead.
fail_msg() {
    local file="$1" message="$2"
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        echo "::error file=${file}::${message}"
    else
        echo "ERROR (${file}): ${message}" >&2
    fi
}

step() { printf '\n==== %s ====\n' "$1"; }

# ---------------------------------------------------------------------------
# docs
# ---------------------------------------------------------------------------

# One place that knows what a number word is worth, used for both halves of a
# compound, so "sixty-one" needs no entry of its own.
number_word() {
    case "$1" in
        one) echo 1 ;; two) echo 2 ;; three) echo 3 ;; four) echo 4 ;;
        five) echo 5 ;; six) echo 6 ;; seven) echo 7 ;; eight) echo 8 ;;
        nine) echo 9 ;; ten) echo 10 ;; eleven) echo 11 ;;
        twelve) echo 12 ;; thirteen) echo 13 ;; fourteen) echo 14 ;;
        fifteen) echo 15 ;; sixteen) echo 16 ;; seventeen) echo 17 ;;
        eighteen) echo 18 ;; nineteen) echo 19 ;; twenty) echo 20 ;;
        thirty) echo 30 ;; forty) echo 40 ;; fifty) echo 50 ;;
        sixty) echo 60 ;; seventy) echo 70 ;; eighty) echo 80 ;;
        ninety) echo 90 ;;
        *) echo "" ;;
    esac
}

check_docs() {
    step "docs: documented counts match the build"
    local fail=0

    # Anchored to a call with an argument: a bare count would also match the line
    # that DEFINES add_engine_test, which is how a check like this reports one more
    # suite than exists and is quietly disbelieved. Digits are allowed in the name
    # because test_light2d would otherwise be missed.
    #
    # ONE REGISTRAR since the games left for their own repositories. Missing a
    # second registrar was worse than not checking - it once counted 48 while ctest
    # ran 61 - so a new wrapper has to be counted here the day it is added.
    local actual
    actual=$(grep -cE '^add_engine_test\([a-z0-9_]+\)$' tests/CMakeLists.txt)
    echo "tests/CMakeLists.txt registers ${actual} suites."
    test "${actual}" -gt 0

    local tens units teens f claimed phrase word n
    tens='(twenty|thirty|forty|fifty|sixty|seventy|eighty|ninety)'
    units='(one|two|three|four|five|six|seven|eight|nine)'
    teens='(eleven|twelve|thirteen|fourteen|fifteen|sixteen|seventeen|eighteen|nineteen|ten)'

    for f in README.md AGENTS.md CONTRIBUTING.md ARCHITECTURE.md; do
        test -f "$f" || continue
        # Any "<number> suites" phrasing, digits or the words this project uses.
        # The tens and the unit are ONE group with an optional suffix rather than
        # every compound spelled out: the enumerated version stopped at forty-nine
        # and would have silently stopped gating the day the count passed fifty.
        claimed=$(grep -oiE "(${tens}(-${units})?|${teens}|${units}|[0-9]+)( green)? suites\b" "$f" || true)
        while IFS= read -r phrase; do
            test -n "$phrase" || continue
            word=$(echo "$phrase" | sed -E 's/( green)? suites$//' | tr 'A-Z' 'a-z')

            case "$word" in
                [0-9]*) n="$word" ;;
                *-*)    n=$(( $(number_word "${word%%-*}") + $(number_word "${word#*-}") )) ;;
                *)      n=$(number_word "$word") ;;
            esac

            # A word this check cannot price is a check that is not checking. Say
            # so rather than comparing an empty string and passing.
            if [ -z "$n" ]; then
                fail_msg "$f" "claims \"$phrase\", and this check cannot read that number"
                fail=1
                continue
            fi
            if [ "$n" != "${actual}" ]; then
                fail_msg "$f" "claims \"$phrase\"; the build registers ${actual}"
                fail=1
            fi
        done <<< "$claimed"
    done

    # The README badge is URL-encoded, so the prose check above cannot see it - and
    # the badge is the first number anyone reads. sed/cut, not a second grep -oE
    # '[0-9]+': the URL-encoded space in "tests-24%20suites" is itself a number.
    local badge
    badge=$(grep -oE 'tests-[0-9]+%20suites' README.md | cut -d- -f2 | cut -d'%' -f1 || true)
    if [ -n "$badge" ] && [ "$badge" != "${actual}" ]; then
        fail_msg README.md "the tests badge claims ${badge} suites; the build registers ${actual}"
        fail=1
    fi

    # The Testing table is the list AGENTS.md sends a reader to. Seven suites went
    # unlisted in it while the count above stayed right, so check the rows too.
    local registered suite
    registered=$(grep -oE '^add_engine_test\([a-z0-9_]+\)$' tests/CMakeLists.txt \
                 | sed -E 's/add_engine_test\((.*)\)/\1/')
    for suite in $registered; do
        if ! grep -q "^| \`${suite}\` |" README.md; then
            fail_msg README.md "${suite} is registered in tests/CMakeLists.txt but has no row in the Testing table"
            fail=1
        fi
    done

    # A suite file nothing builds is a suite that never runs, and reads like one
    # that passes.
    for f in tests/test_*.cpp; do
        suite=$(basename "$f" .cpp)
        if ! echo "$registered" | grep -qx "$suite"; then
            fail_msg "$f" "is not registered with add_engine_test, so nothing builds or runs it"
            fail=1
        fi
    done

    if [ "$fail" -eq 0 ]; then
        echo "Documented counts, the badge, the table and the registrations agree."
    fi
    return "$fail"
}

# ---------------------------------------------------------------------------
# shaders
# ---------------------------------------------------------------------------

check_shaders() {
    step "shaders: every shader compiles from source and matches its committed SPIR-V"

    # The names come from SHADER_JOBS in CMakeLists.txt, the list the build itself
    # uses, so this cannot fall behind the build the way compile_shaders.bat did.
    local declared declared_sources on_disk job src out
    declared=$(grep -oE '"[a-z_]+\.(vert|frag)=[a-z_]+\.spv"' CMakeLists.txt | tr -d '"')
    test -n "$declared" \
        || { echo "no shader jobs found in CMakeLists.txt - has SHADER_JOBS moved?"; return 1; }

    declared_sources=$(grep -oE '"[a-z_]+\.(vert|frag)=' CMakeLists.txt | tr -d '"=' | sort)
    on_disk=$(cd assets/shaders && ls ./*.vert ./*.frag | sed 's|^\./||' | sort)
    if [ "$declared_sources" != "$on_disk" ]; then
        echo "assets/shaders and CMakeLists.txt disagree about which shaders exist:"
        echo "  < only in CMakeLists   > only on disk"
        diff <(echo "$declared_sources") <(echo "$on_disk") || true
        return 1
    fi
    echo "Every shader source is compiled by the build."

    # glslc's output is not guaranteed bit-stable across shaderc releases, and the
    # committed blobs are compared byte for byte. CI pins the SDK to the version
    # they reproduce under; a different glslc here can report a difference that is
    # not a real one, so say what to conclude from that.
    if ! command -v glslc >/dev/null 2>&1; then
        echo "glslc not found (install the Vulkan SDK) - skipping the recompile; the"
        echo "list above was still checked."
        return 0
    fi

    local tmp
    tmp=$(mktemp -d)
    # shellcheck disable=SC2064  # expand $tmp now: it is gone by the time the trap fires
    trap "rm -rf '$tmp'" RETURN

    while IFS= read -r job; do
        src="${job%%=*}"
        out="${job##*=}"
        echo "Compiling $src"
        glslc "assets/shaders/$src" -o "$tmp/$out"

        if ! cmp -s "$tmp/$out" "assets/shaders/$out"; then
            fail_msg "assets/shaders/$out" "committed SPIR-V does not match $src"
            echo "  Rebuild the shaders and commit the result:"
            echo "    cmake --build build --target Shaders"
            echo "  If your glslc is not the version CI pins, a mismatch with an empty"
            echo "  source diff may be shaderc's output changing, not a stale blob."
            return 1
        fi
    done <<< "$declared"

    echo "All shaders compile cleanly and match their committed SPIR-V."
}

# ---------------------------------------------------------------------------
# builds
# ---------------------------------------------------------------------------

# configure_and_build <dir> <target-or-empty> <cmake args...>
configure_and_build() {
    local dir="$1" target="$2"
    shift 2
    cmake -S . -B "$dir" "${GENERATOR_ARGS[@]}" "$@"
    if [ -n "$target" ]; then
        cmake --build "$dir" --parallel "$JOBS" --target "$target"
    else
        cmake --build "$dir" --parallel "$JOBS"
    fi
}

check_build() {
    step "build: Release, then every suite"
    configure_and_build "$BUILD_ROOT/release" "" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    ctest --test-dir "$BUILD_ROOT/release" --output-on-failure -j "$JOBS"
}

check_options() {
    step "options: tests and plugin OFF"
    # The configuration the documentation advertises and nothing else builds: a
    # game builds the engine this way, so a target that quietly depended on the
    # suites or the plugin being built fails here first.
    configure_and_build "$BUILD_ROOT/options-off" "" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSUPERSONIC_BUILD_TESTS=OFF -DSUPERSONIC_BUILD_SCRIPT_PLUGIN=OFF
}

check_asan() {
    step "asan: ASan + UBSan + LSan, every suite"
    # RelWithDebInfo: the sanitizers need frame pointers and symbols to produce a
    # readable report, and -O0 across this tree is what exhausts a small machine.
    configure_and_build "$BUILD_ROOT/asan" "" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    # Leaks and undefined behaviour both fail the run. The suites only: the engine
    # itself needs a device, and a driver's own allocations are somebody else's.
    ASAN_OPTIONS="abort_on_error=1:detect_leaks=1" \
    UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
        ctest --test-dir "$BUILD_ROOT/asan" -C RelWithDebInfo --output-on-failure -j "$JOBS"
}

check_tsan() {
    step "tsan: the suites that use threads"
    configure_and_build "$BUILD_ROOT/tsan" "" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
    # Only the suites that start a thread. TSan over the rest is slower and finds
    # nothing a single thread can do.
    TSAN_OPTIONS="halt_on_error=1" \
        ctest --test-dir "$BUILD_ROOT/tsan" -C RelWithDebInfo --output-on-failure \
              -R '^test_(jobs|mixer|audio|scripts|layerstack|assetwatcher)$'
}

check_tidy() {
    step "tidy: clang-tidy over the engine's own sources"
    command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not found"; return 1; }

    # Configure only: the compile database is all clang-tidy needs, and a build
    # first would just be the Release build again.
    cmake -S . -B "$BUILD_ROOT/tidy" "${GENERATOR_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    local db="$BUILD_ROOT/tidy/compile_commands.json"
    test -f "$db" || { echo "no compile_commands.json - the configure failed"; return 1; }

    # Correctness and performance families only. easily-swappable-parameters and
    # narrowing-conversions are left out: the first flags every (float, float)
    # signature in a maths library and the second every float-to-int in a renderer,
    # and a check nobody can act on gets disabled wholesale the first time it is
    # noisy. Vendored code is not this project's to lint.
    local checks='-*,bugprone-*,performance-*,clang-analyzer-*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions'
    local files
    files=$(grep -oE '"file": "[^"]+"' "$db" | cut -d'"' -f4 \
            | grep -E "/(src|tests|plugins)/" | grep -vE '/third_party/|Implementation\.cpp$' | sort -u)
    echo "$files" | xargs -P "$JOBS" -n 4 clang-tidy -p "$BUILD_ROOT/tidy" --quiet --checks="$checks" \
        --warnings-as-errors='*'
}

# ---------------------------------------------------------------------------

usage() {
    # The header comment, from the title to the end of the usage list.
    awk 'NR>1 && /^set -euo pipefail/ {exit} NR>1 {sub(/^# ?/, ""); print}' "$0"
}

run() {
    local name="$1"
    case "$name" in
        docs)    check_docs ;;
        shaders) check_shaders ;;
        build)   check_build ;;
        options) check_options ;;
        asan)    check_asan ;;
        tsan)    check_tsan ;;
        tidy)    check_tidy ;;
        all)     local s; for s in docs shaders build options asan tsan tidy; do run "$s"; done ;;
        -h|--help|help) usage ;;
        *)       echo "unknown step: $name (try: docs shaders build options asan tsan tidy all)" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 0 ]; then
    set -- docs shaders build
fi

# set -e does the stopping: every step is called plainly, never inside an `||` or
# an `if`, because either would switch -e off for the whole body of the step and
# let a failed cmake be followed by a build of whatever it left behind. The trap
# only says which step it was. Later steps are slower than earlier ones, and a
# failed docs check says nothing a long build would improve on.
current=""
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo >&2; echo "check.sh: ${current:-startup} failed (exit $rc)." >&2; fi' EXIT

for requested in "$@"; do
    current="$requested"
    run "$requested"
done
