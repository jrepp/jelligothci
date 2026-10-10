#!/usr/bin/env bash
# JSDoc type check of the studio's browser scripts with the TypeScript pinned in toolchain.env.
# Needs Node (npx); the package is cached in .tools/npm-cache. The scripts do not pass yet:
# baseline.txt lists today's errors as "file: TSxxxx message" (no line numbers), and the check
# fails on any error not listed there.
#
#   tools/jelli-art/typecheck/check.sh                     check against the baseline
#   JELLI_TS_UPDATE=1 tools/jelli-art/typecheck/check.sh   write the current errors as the baseline
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
# shellcheck source-path=SCRIPTDIR
# shellcheck source=../../../toolchain.env
source "$repo_dir/toolchain.env"
here="$repo_dir/tools/jelli-art/typecheck"
export npm_config_cache="$repo_dir/.tools/npm-cache"

status=0
output="$(cd "$repo_dir" && npx --yes --package="typescript@$JELLI_TYPESCRIPT_VERSION" -- tsc -p "$here/tsconfig.json" --pretty false)" || status=$?
# "tools/jelli-art/x.js(12,3): error TS2339: msg" -> "tools/jelli-art/x.js: TS2339: msg", sorted.
current="$(grep 'error TS' <<<"$output" | sed -E 's/\([0-9]+,[0-9]+\): error (TS[0-9]+)/: \1/' | LC_ALL=C sort || true)"
if [[ "$status" -ne 0 && -z "$current" ]]; then
    printf '%s\n' "$output" >&2
    echo "tsc failed without reporting type errors" >&2
    exit 1
fi
if [[ "${JELLI_TS_UPDATE:-}" == 1 ]]; then
    printf '%s\n' "$current" | sed '/^$/d' >"$here/baseline.txt"
    echo "TypeScript $JELLI_TYPESCRIPT_VERSION: $(grep -c . "$here/baseline.txt") errors written to the baseline"
    exit 0
fi
new="$(LC_ALL=C comm -23 <(printf '%s\n' "$current" | sed '/^$/d') "$here/baseline.txt")"
fixed="$(LC_ALL=C comm -13 <(printf '%s\n' "$current" | sed '/^$/d') "$here/baseline.txt")"
if [[ -n "$new" ]]; then
    printf '%s\n' "$output" | grep -F -f <(sed -E 's/^([^:]+): (TS[0-9]+): (.*)$/\3/' <<<"$new") || true
    printf 'Errors not in the baseline:\n%s\n' "$new" >&2
    exit 1
fi
echo "TypeScript $JELLI_TYPESCRIPT_VERSION: no errors beyond the baseline ($(grep -c . "$here/baseline.txt") listed)"
if [[ -n "$fixed" ]]; then
    echo "$(grep -c . <<<"$fixed") baseline errors are gone; lower the baseline with JELLI_TS_UPDATE=1 $0"
fi
