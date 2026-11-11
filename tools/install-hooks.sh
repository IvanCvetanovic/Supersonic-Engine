#!/usr/bin/env bash
#
# Points this clone's git at tools/git-hooks, which holds the pre-push check. One
# setting, per clone, reversible with `git config --unset core.hooksPath`.
#
#   tools/install-hooks.sh

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

git config core.hooksPath tools/git-hooks
chmod +x tools/git-hooks/* 2>/dev/null || true

echo "Installed: git now runs tools/git-hooks/pre-push before every push."
echo "  skip it once:        git push --no-verify"
echo "  add the full build:  SUPERSONIC_PREPUSH=full git push"
echo "  remove it:           git config --unset core.hooksPath"
