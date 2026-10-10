#!/usr/bin/env bash
# JSDoc type check of the studio's browser scripts with the TypeScript pinned in toolchain.env.
# Needs Node (npx); the package is cached in .tools/npm-cache. The scripts do not pass yet, so the
# check fails only when the error count rises above baseline.txt.
#
#   tools/jelli-art/typecheck/check.sh            check against the baseline
#   JELLI_TS_UPDATE=1 tools/jelli-art/typecheck/check.sh   write the current count as the baseline
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
# shellcheck source=../../../toolchain.env
source "$repo_dir/toolchain.env"
here="$repo_dir/tools/jelli-art/typecheck"
export npm_config_cache="$repo_dir/.tools/npm-cache"

status=0
output="$(cd "$repo_dir" && npx --yes --package="typescript@$JELLI_TYPESCRIPT_VERSION" -- tsc -p "$here/tsconfig.json" --pretty false)" || status=$?
count="$(grep -c 'error TS' <<<"$output" || true)"
if [[ "$status" -ne 0 && "$count" -eq 0 ]]; then
    printf '%s\n' "$output" >&2
    echo "tsc failed without reporting type errors" >&2
    exit 1
fi
baseline="$(tr -d '[:space:]' <"$here/baseline.txt")"
if [[ "${JELLI_TS_UPDATE:-}" == 1 ]]; then
    echo "$count" >"$here/baseline.txt"
    echo "TypeScript $JELLI_TYPESCRIPT_VERSION: $count errors; baseline updated"
    exit 0
fi
if ((count > baseline)); then
    printf '%s\n' "$output"
    echo "TypeScript $JELLI_TYPESCRIPT_VERSION: $count errors, baseline $baseline. Fix the new ones." >&2
    exit 1
fi
echo "TypeScript $JELLI_TYPESCRIPT_VERSION: $count errors (baseline $baseline)"
if ((count < baseline)); then
    echo "Fewer errors than the baseline: lower it with JELLI_TS_UPDATE=1 $0"
fi
