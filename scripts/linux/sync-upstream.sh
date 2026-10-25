#!/usr/bin/env bash
set -euo pipefail

usage="usage: scripts/linux/sync-upstream.sh [--push]"
if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  echo "$usage"
  exit 0
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"

STATUS_JSON="$("$ROOT/scripts/linux/check-upstream.sh" --json)"
echo "Upstream status: $STATUS_JSON"

if echo "$STATUS_JSON" | grep -q '"up_to_date":true'; then
  echo "Already synced with upstream/main."
else
  echo "Rebasing onto upstream/main..."
  git rebase upstream/main
fi

pnpm install --frozen-lockfile
pnpm typecheck
pnpm --filter @ardia/demo build

echo "UI bundle built cleanly at apps/demo/dist."

if [ "${1:-}" = "--push" ]; then
  git push --force-with-lease origin main
  echo "Pushed updated main to origin."
fi
