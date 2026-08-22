#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build-release-check}"
out="${2:-$root/output/grape-source.tar.gz}"
sdk="${GEODE_SDK:?Set GEODE_SDK to the Geode SDK source checkout used to build}"

[[ -d "$build/_deps" ]] || { echo "Missing configured build: $build" >&2; exit 1; }
git -C "$sdk" rev-parse --is-inside-work-tree >/dev/null

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/grape" "$stage/third_party"

paths=(
  .forgejo .github .gitignore .clang-format .clangd
  CMakeLists.txt LICENSE LICENSES NOTICE README.md about.md build.sh
  include lib logo.png mod.json pc resources scripts specs src tests vendor
)
for path in "${paths[@]}"; do
  [[ -e "$root/$path" ]] && cp -a "$root/$path" "$stage/grape/"
done
find "$stage" -type f -name '*:Zone.Identifier*' -delete

manifest="$stage/SOURCE_MANIFEST.txt"
{
  echo "Grape root: $(git -C "$root" rev-parse HEAD)"
  if [[ -n "$(git -C "$root" status --short)" ]]; then
    echo "Grape working tree: modified (bundle contains the working files)"
  else
    echo "Grape working tree: clean"
  fi
  echo "Geode SDK: $(git -C "$sdk" rev-parse HEAD)"
} > "$manifest"

git -C "$sdk" archive HEAD | tar -x -C "$stage/third_party"
mv "$stage/third_party" "$stage/geode-sdk"
mkdir "$stage/third_party"

for source in "$build"/_deps/*-src; do
  [[ -d "$source" ]] || continue
  name="$(basename "$source")"
  mkdir "$stage/third_party/$name"
  tar --exclude=.git -C "$source" -cf - . |
    tar -xf - -C "$stage/third_party/$name"
  if [[ -e "$source/.git" ]]; then
    printf '%s: %s (%s)\n' "$name" \
      "$(git -C "$source" rev-parse HEAD)" \
      "$(git -C "$source" remote get-url origin 2>/dev/null || echo local)" \
      >> "$manifest"
  else
    printf '%s: archive source; checksum is recorded in CMake configuration\n' \
      "$name" >> "$manifest"
  fi
done

mkdir -p "$(dirname "$out")"
tar -czf "$out" -C "$stage" .
sha256sum "$out" > "$out.sha256"
printf 'Created %s\n' "$out"
