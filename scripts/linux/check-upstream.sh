#!/usr/bin/env bash
set -euo pipefail

usage="usage: scripts/linux/check-upstream.sh [--json]"
if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  echo "$usage"
  exit 0
fi

UPSTREAM_URL="https://github.com/mantrakp04/ardia.git"
UPSTREAM_REMOTE="upstream"

if ! git remote get-url "$UPSTREAM_REMOTE" >/dev/null 2>&1; then
  git remote add "$UPSTREAM_REMOTE" "$UPSTREAM_URL"
fi

git fetch --quiet "$UPSTREAM_REMOTE" main

LOCAL_BASE="$(git merge-base HEAD "$UPSTREAM_REMOTE/main")"
UPSTREAM_HEAD="$(git rev-parse "$UPSTREAM_REMOTE/main")"
ENGINE_TREE="$(git rev-parse HEAD:engine)"
ENGINE_SHORT="${ENGINE_TREE:0:12}"

if [ "$LOCAL_BASE" = "$UPSTREAM_HEAD" ]; then
  if [ "${1:-}" = "--json" ]; then
    printf '{"up_to_date":true,"commits_behind":0,"rebuild_level":"NONE","engine_tree":"%s"}\n' "$ENGINE_SHORT"
  else
    echo "Up to date with mantrakp04/ardia@main ($UPSTREAM_HEAD, engine tree $ENGINE_SHORT)."
  fi
  exit 0
fi

COMMITS_BEHIND="$(git rev-list --count "$LOCAL_BASE..$UPSTREAM_HEAD")"
CHANGED_FILES="$(git diff --name-only "$LOCAL_BASE..$UPSTREAM_HEAD")"

REBUILD_LEVEL="UI_ONLY"
if echo "$CHANGED_FILES" | grep -qE '^engine/patches/build/(env\.sh|helium-pins\.ini)$'; then
  REBUILD_LEVEL="ENGINE_FULL"
elif echo "$CHANGED_FILES" | grep -qE '^engine/'; then
  REBUILD_LEVEL="ENGINE_INCREMENTAL"
fi

ABI_CHANGED="false"
if echo "$CHANGED_FILES" | grep -qE '^engine/chromium/src/chrome/browser/ardia/(public/ac_engine\.h|BUILD\.gn)$'; then
  ABI_CHANGED="true"
fi

if [ "${1:-}" = "--json" ]; then
  printf '{"up_to_date":false,"commits_behind":%d,"rebuild_level":"%s","abi_or_gn_changed":%s,"upstream_head":"%s","engine_tree":"%s"}\n' \
    "$COMMITS_BEHIND" "$REBUILD_LEVEL" "$ABI_CHANGED" "$UPSTREAM_HEAD" "$ENGINE_SHORT"
else
  echo "Upstream mantrakp04/ardia is ahead by $COMMITS_BEHIND commit(s) ($LOCAL_BASE -> $UPSTREAM_HEAD)"
  echo "Current engine tree SHA:   $ENGINE_SHORT"
  echo "Recommended rebuild level: $REBUILD_LEVEL"
  echo "ABI or BUILD.gn touched:   $ABI_CHANGED"
  echo ""
  echo "Recent upstream commits:"
  git log --oneline -n 15 "$LOCAL_BASE..$UPSTREAM_HEAD"
fi
