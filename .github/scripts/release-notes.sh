#!/usr/bin/env bash
#
# Generate Markdown release notes from the commits since the previous release
# (the most recent tag that is an ancestor of the target ref). Used by the
# "release" job in .github/workflows/ci.yml, which feeds the output to
# softprops/action-gh-release as the release body.
#
# Usage:  release-notes.sh [<ref>]      (<ref> defaults to HEAD)
# Output: Markdown on stdout.
#
# Requires the full git history (actions/checkout with fetch-depth: 0).
set -euo pipefail

CURRENT="${1:-HEAD}"

# Human-readable name for the current point: the exact tag if there is one,
# otherwise the short SHA.
CUR_NAME="$(git describe --tags --exact-match "$CURRENT" 2>/dev/null \
            || git rev-parse --short "$CURRENT")"

# Previous release = nearest tag reachable from the commit *before* CURRENT, so
# an annotated tag on CURRENT itself is not picked as its own predecessor.
PREV_TAG="$(git describe --tags --abbrev=0 "${CURRENT}^" 2>/dev/null || true)"

if [ -n "$PREV_TAG" ]; then
  RANGE="${PREV_TAG}..${CURRENT}"
  echo "## Changes since ${PREV_TAG}"
else
  # No earlier tag: this is the first release, summarise the whole history.
  RANGE="$CURRENT"
  echo "## Initial release"
fi
echo

# One bullet per non-merge commit: subject + abbreviated hash. Group by the
# "area:" prefix Dia uses in commit subjects when present, for a tidier list.
git log --no-merges --pretty=format:'%s|%h' "$RANGE" \
| awk -F'|' '
    {
      subj = $1; hash = $2
      area = "Other"
      if (match(subj, /^[A-Za-z0-9_.-]+:/)) {
        area = substr(subj, 1, RLENGTH - 1)
        subj = substr(subj, RLENGTH + 1)
        sub(/^ +/, "", subj)
      }
      if (!(area in seen)) { order[n++] = area; seen[area] = 1 }
      items[area] = items[area] sprintf("- %s (%s)\n", subj, hash)
    }
    END {
      for (i = 0; i < n; i++) {
        a = order[i]
        printf "\n### %s\n%s", a, items[a]
      }
    }'

echo
if [ -n "$PREV_TAG" ]; then
  echo "**Full changelog:** \`${PREV_TAG}...${CUR_NAME}\`"
fi
