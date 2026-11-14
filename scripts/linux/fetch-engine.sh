#!/usr/bin/env bash
set -euo pipefail
usage='usage: scripts/linux/fetch-engine.sh [--arch x64|arm64] [--nearest]
       packages/ardiacore/scripts/fetch-engine.sh --arch linux-x86_64 [--nearest]

Downloads, verifies and caches the prebuilt Linux ArdiaCore engine whose
engine key (engine/engine-key.sh: engine/ and the offline game it packs)
matches this checkout (scripts/linux/publish-engine.sh makes them), and prints
the engine directory (the one holding chrome and manifest.json) for
scripts/linux/package.py --out.

  --nearest   when this engine key has no published engine, use the one of
              the nearest earlier commit that changed a path the key covers,
              with a warning

Rows come from packages/ardiacore/prebuilt-engines-linux.tsv
(key<TAB>tag<TAB>sha256), then from the published table.
Environment:
  ARDIACORE_PREBUILT_REPO          GitHub repo (default mckamyk/ardia)
  ARDIACORE_PREBUILT_ASSETS        asset base URL (default
                                   https://github.com/$repo/releases/download)
  ARDIACORE_PREBUILT_TABLE_URL     published table URL, or none (default the
                                   linux branch copy of the table on GitHub)
  ARDIACORE_PREBUILT_LINUX_TABLE   local table path (default the checked-in one)
ARM64 uses prebuilt-engines-linux-arm64.tsv and a separate prebuilt-linux-arm64 cache.
The default architecture is the host; --arch selects an explicit target.'
nearest=0
arch=$(uname -m)
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) echo "$usage"; exit 0 ;;
    --nearest) nearest=1; shift ;;
    --arch) [ $# -ge 2 ] || { echo "$usage" >&2; exit 64; }; arch=$2; shift 2 ;;
    *) echo "$usage" >&2; exit 64 ;;
  esac
done
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
cpu=$(python3 -I -c 'import sys; sys.path.insert(0, sys.argv[1]); from linux_platform import normalize_cpu; print(normalize_cpu(sys.argv[2]))' "$here" "$arch") || exit 64
table_name=prebuilt-engines-linux.tsv
cache_arch=x86_64
if [ "$cpu" = arm64 ]; then
  table_name=prebuilt-engines-linux-arm64.tsv
  cache_arch=arm64
fi

repo=${ARDIACORE_PREBUILT_REPO:-mckamyk/ardia}
assets=${ARDIACORE_PREBUILT_ASSETS:-https://github.com/$repo/releases/download}
table=${ARDIACORE_PREBUILT_LINUX_TABLE:-$root/packages/ardiacore/$table_name}
table_url=${ARDIACORE_PREBUILT_TABLE_URL:-https://raw.githubusercontent.com/$repo/linux/packages/ardiacore/$table_name}
store=$root/apps/browser/build-ardiacore/prebuilt-linux-$cache_arch
archive_root=ardiacore-linux-$cpu
die() { echo "fetch-engine.sh: $*" >&2; exit 1; }

for tool in git curl sha256sum python3; do
  command -v "$tool" >/dev/null || die "$tool is required"
done
key_script=$root/engine/engine-key.sh
git -C "$root" rev-parse --git-dir >/dev/null 2>&1 \
  || die "$root isn't a git checkout: a prebuilt engine is matched to its engine key (engine/engine-key.sh)"
mapfile -t key_paths < <("$key_script" --paths)
[ "${#key_paths[@]}" -gt 0 ] || die "engine/engine-key.sh --paths listed nothing"
dirty_paths=()
for path in "${key_paths[@]}"; do
  [ -z "$(git -C "$root" status --porcelain -- "$path")" ] || dirty_paths+=("$path")
done
[ "${#dirty_paths[@]}" = 0 ] \
  || die "$(printf '%s, ' "${dirty_paths[@]}" | sed 's/, $//') (in the engine key, engine/engine-key.sh) has uncommitted changes, and no prebuilt engine has them: build the engine (docs/linux-port.md)"
key=$("$key_script") || die "couldn't compute this checkout's engine key (engine/engine-key.sh)"

remote_rows="" remote_loaded=0
row_in() { awk -F'\t' -v t="$1" '$1 == t { print; exit }'; }
load_remote() {
  [ "$remote_loaded" = 0 ] || return 0
  remote_loaded=1
  [ "$table_url" != none ] || return 0
  remote_rows=$(curl -fsSL --retry 2 "$table_url" 2>/dev/null) \
    || { echo "warning: couldn't read the published engine table at $table_url" >&2; remote_rows=""; }
}
cached_key() {
  local dir=$store/${1:0:12}
  [ -f "$dir/.complete" ] && [ -f "$dir/manifest.json" ] || return 1
  python3 -I -c 'import pathlib, sys; sys.path.insert(0, sys.argv[1]); import runtime_files; directory = pathlib.Path(sys.argv[2]); runtime_files.check_manifest(directory, sys.argv[3], sys.argv[4]); runtime_files.select(directory, sys.argv[4])' "$here" "$dir" "$1" "$cpu" >/dev/null 2>&1
}
row=""
resolve() {
  row=""
  [ -n "$1" ] || return 1
  [ ! -f "$table" ] || row=$(row_in "$1" <"$table")
  [ -z "$row" ] || return 0
  if cached_key "$1"; then row="$1"; return 0; fi
  load_remote
  row=$(row_in "$1" <<<"$remote_rows")
  [ -n "$row" ]
}

found="" distance=0
if resolve "$key"; then
  found=$row
else
  index=0
  while read -r commit; do
    candidate=$("$key_script" "$commit" 2>/dev/null || true)
    if [ "$candidate" != "$key" ] && resolve "$candidate"; then found=$row distance=$index; break; fi
    index=$((index + 1))
  done < <(git -C "$root" rev-list --max-count=200 HEAD -- "${key_paths[@]}")
  if [ -z "$found" ]; then
    die "no prebuilt Linux engine for engine key ${key:0:12} or any of the 200 commits before it that changed a path the key covers: build this commit's engine on the Linux builder (scripts/linux/build.sh --apply), publish it with scripts/linux/publish-engine.sh --upload and commit packages/ardiacore/$table_name"
  fi
  if [ "$nearest" = 0 ]; then
    die "no prebuilt Linux engine for engine key ${key:0:12}: build this commit's engine on the Linux builder (scripts/linux/build.sh --apply), publish it with scripts/linux/publish-engine.sh --upload and commit packages/ardiacore/$table_name, or pass --nearest to use ${found:0:12}, $distance engine commits older"
  fi
  echo "warning: no prebuilt Linux engine for engine key ${key:0:12}; using ${found:0:12}, $distance engine commits older, and the app may not match it" >&2
fi

IFS=$'\t' read -r selected tag sha <<<"$found"
[[ "$selected" =~ ^[0-9a-f]{40}$ ]] || die "bad engine table row: $found"
short=${selected:0:12}
dest=$store/$short
mkdir -p "$store"
exec 9>"$store/.lock"
if command -v flock >/dev/null; then
  flock -w 300 9 || die "another fetch-engine.sh still holds $store/.lock after 300s"
fi
if [ -z "${tag:-}" ] && [ -z "${sha:-}" ] && [ -f "$dest/.complete" ] && cached_key "$selected"; then
  echo "using the cached Linux engine $short" >&2
  touch "$dest/.complete"
  echo "$dest"
  exit 0
fi
[[ "${tag:-}" =~ ^[A-Za-z0-9._-]+$ ]] || die "bad release tag in the engine table row: $found"
[[ "${sha:-}" =~ ^[0-9a-f]{64}$ ]] || die "bad SHA-256 in the engine table row: $found"
if [ -f "$dest/.complete" ] && [ "$(cat "$dest/.complete")" = "$sha" ] && cached_key "$selected"; then
  echo "using the cached Linux engine $short" >&2
  touch "$dest/.complete"
  echo "$dest"
  exit 0
fi

for stale in "$store"/.tmp-*; do
  [ ! -e "$stale" ] || rm -rf "$stale"
done
tmp=$(mktemp -d "$store/.tmp-XXXXXX")
trap 'rm -rf "$tmp"' EXIT
file="ArdiaCore-$short-linux-$cpu.tar.xz"
url="$assets/$tag/$file"
echo "downloading the Linux engine $short ($url)" >&2
curl -fsSL --retry 3 -o "$tmp/$file" "$url" || die "couldn't download $url"
actual=$(sha256sum "$tmp/$file" | awk '{ print $1 }')
[ "$actual" = "$sha" ] || die "$file has SHA-256 $actual, not $sha from the engine table"
python3 -I "$here/runtime_files.py" unpack --archive "$tmp/$file" --into "$tmp/unpacked" --key "$selected" --arch "$cpu" >/dev/null \
  || die "$file isn't a valid Linux engine for $short"
rm -f "$tmp/$file"
rm -rf "$dest"
mv "$tmp/unpacked/$archive_root" "$dest"
printf '%s\n' "$sha" >"$dest/.complete"
rm -rf "$tmp"
trap - EXIT

kept=0
while read -r marker; do
  dir=${marker%/.complete}
  [ "$dir" != "$dest" ] || continue
  kept=$((kept + 1))
  [ "$kept" -lt 2 ] || rm -rf "$dir"
done < <(ls -1t "$store"/*/.complete 2>/dev/null)
for dir in "$store"/*/; do
  dir=${dir%/}
  [ -f "$dir/.complete" ] || rm -rf "$dir"
done
echo "$dest"
