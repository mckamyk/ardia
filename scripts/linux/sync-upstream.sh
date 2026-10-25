#!/usr/bin/env bash
# usage: scripts/linux/sync-upstream.sh [--push]
set -euo pipefail

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

echo "Installing workspace dependencies and verifying react-native-web + WebGL UI..."
pnpm install --frozen-lockfile
pnpm typecheck
pnpm --filter @ardia/demo build

echo "UI bundle built cleanly at apps/demo/dist."

if [ "${1:-}" = "--push" ]; then
  git push --force-with-lease origin main
  echo "Pushed updated main to origin."
fi
