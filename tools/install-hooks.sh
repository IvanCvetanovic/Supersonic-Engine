#!/usr/bin/env bash
#
# Points this clone's git at tools/git-hooks, which holds the pre-push check. One
# setting, per clone.
#
#   tools/install-hooks.sh            install, unless that would displace something
#   tools/install-hooks.sh --force    install anyway
#   tools/install-hooks.sh --remove   undo it
#
# core.hooksPath REPLACES .git/hooks rather than adding to it: once set, a pre-commit,
# commit-msg or any other hook in .git/hooks stops running. So this refuses when
# core.hooksPath already points somewhere else, and when .git/hooks holds a hook that
# is in use, unless told to go ahead; and --remove puts back the value it replaced.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ours="tools/git-hooks"
saved_key="supersonic.previousHooksPath"
current="$(git config --local --get core.hooksPath || true)"

if [ "${1:-}" = "--remove" ]; then
    if [ "$current" != "$ours" ]; then
        echo "Nothing to remove: core.hooksPath is not set to $ours here."
        exit 0
    fi
    previous="$(git config --local --get "$saved_key" || true)"
    if [ -n "$previous" ]; then
        git config --local core.hooksPath "$previous"
        echo "Removed. core.hooksPath is back to: $previous"
    else
        git config --local --unset core.hooksPath
        echo "Removed. core.hooksPath is unset; git uses .git/hooks again."
    fi
    git config --local --unset "$saved_key" 2>/dev/null || true
    exit 0
fi

force=0
[ "${1:-}" = "--force" ] && force=1

if [ "$current" = "$ours" ]; then
    echo "Already installed: git runs $ours/pre-push before every push."
    exit 0
fi

if [ -n "$current" ] && [ "$force" -eq 0 ]; then
    echo "core.hooksPath is already set to '$current' in this clone, and installing would" >&2
    echo "replace it (hooks there would stop running). Run with --force to do it anyway;" >&2
    echo "--remove then restores '$current'." >&2
    exit 1
fi

hooks_dir="$(git rev-parse --git-path hooks)"
active=""
if [ -z "$current" ] && [ -d "$hooks_dir" ]; then
    for hook in "$hooks_dir"/*; do
        [ -f "$hook" ] || continue
        case "$hook" in *.sample) continue ;; esac
        active="$active $(basename "$hook")"
    done
fi
if [ -n "$active" ] && [ "$force" -eq 0 ]; then
    echo "$hooks_dir already holds hooks that are in use:$active" >&2
    echo "Installing would stop them running (core.hooksPath replaces that directory)." >&2
    echo "Run with --force to do it anyway; --remove then puts them back in effect." >&2
    exit 1
fi

if [ -n "$current" ]; then
    git config --local "$saved_key" "$current"
fi
git config --local core.hooksPath "$ours"
chmod +x "$ours"/* 2>/dev/null || true

echo "Installed: git now runs $ours/pre-push before every push."
echo "  skip it once:        git push --no-verify"
echo "  add the full build:  SUPERSONIC_PREPUSH=full git push"
echo "  remove it:           tools/install-hooks.sh --remove"
