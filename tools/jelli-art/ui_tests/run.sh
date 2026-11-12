#!/usr/bin/env bash
# Run the Jelli Art UI tests.
#
#   tools/jelli-art/ui_tests/run.sh [pytest args]        in the pinned Playwright image (podman or docker)
#   JELLI_UI_LOCAL=1 tools/jelli-art/ui_tests/run.sh     on this machine, with Chromium in .tools/
#   JELLI_UI_UPDATE=1 tools/jelli-art/ui_tests/run.sh    rewrite the aria and axe baselines
#
# The image is the one CI uses (.github/workflows/jelli-art.yml), pinned by digest. Screenshots are
# taken everywhere but compared only when JELLI_UI_SCREENSHOTS=1, which CI sets: Chromium cannot run
# under amd64 emulation, so the linux/amd64 CI run is the reference. Results land in build/ui-tests/.
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
image="mcr.microsoft.com/playwright/python:v1.63.0-noble@sha256:72bd171a9ffc2b4b59532aaa6210e21014d07093120dc25528870c0b840da1f0"
requirements="tools/jelli-art/ui_tests/requirements.txt"

# Each CI shard receives complete, non-overlapping files. Normal local runs keep
# the entire suite and accept all existing pytest arguments as before.
test_paths=(tools/jelli-art/ui_tests)
if [[ -n "${JELLI_UI_SHARD:-}" ]]; then
    selection="$(python3 "$repo_dir/tools/jelli-art/ui_tests/shards.py" "$JELLI_UI_SHARD")"
    [[ -n "$selection" ]] || { echo 'UI shard selected no tests' >&2; exit 2; }
    test_paths=()
    while IFS= read -r path; do test_paths+=("$path"); done <<< "$selection"
fi

if [[ "${JELLI_UI_CONTAINER:-}" == 1 ]]; then
    # Inside the image (no ensurepip): the hash-pinned requirements in a throwaway target directory.
    site="${JELLI_UI_SITE:-/tmp/jelli-ui-site}"
    [[ ( -d "$site/pytest" || -d "$site/_pytest" ) && -d "$site/cmake" ]] ||
        python3 -m pip install -q --disable-pip-version-check --break-system-packages --require-hashes \
            --target "$site" -r "$repo_dir/$requirements"
    export PYTHONPATH="$site${PYTHONPATH:+:$PYTHONPATH}"
    export PATH="$site/bin:$PATH"  # hash-pinned CMake for the real creation/clone generators
    cd "$repo_dir"
    exec python3 -m pytest "${test_paths[@]}" "$@"
fi

if [[ "${JELLI_UI_LOCAL:-}" == 1 ]]; then
    if [[ "${JELLI_UI_UPDATE:-}" == 1 ]]; then
        echo "Baselines come from the pinned image: run JELLI_UI_UPDATE=1 without JELLI_UI_LOCAL" >&2
        exit 2
    fi
    # Browsers go in the repository's .tools/, not a global cache.
    export PLAYWRIGHT_BROWSERS_PATH="$repo_dir/.tools/ms-playwright"
    cd "$repo_dir"
    ./scripts/uv run --python 3.15 --no-project --with-requirements "$requirements" python -m playwright install chromium >&2
    exec ./scripts/uv run --python 3.15 --no-project --with-requirements "$requirements" pytest "${test_paths[@]}" "$@"
fi

engine="$(command -v podman || command -v docker || true)"
[[ -n "$engine" ]] || { echo "Install podman or docker, or set JELLI_UI_LOCAL=1" >&2; exit 2; }
mounts=(-v "$repo_dir:$repo_dir")
# A linked worktree's .git file points into the main checkout's git directory; mount it too.
common="$(git -C "$repo_dir" rev-parse --path-format=absolute --git-common-dir)"
[[ "$common" == "$repo_dir/.git" ]] || mounts+=(-v "$common:$common")
case "$(uname -m)" in arm64 | aarch64) platform=linux/arm64 ;; *) platform=linux/amd64 ;; esac
exec "$engine" run --rm --ipc=host --platform "$platform" \
    -e JELLI_UI_CONTAINER=1 -e JELLI_UI_UPDATE="${JELLI_UI_UPDATE:-}" -e JELLI_UI_SCREENSHOTS="${JELLI_UI_SCREENSHOTS:-}" -e JELLI_UI_SHARD="${JELLI_UI_SHARD:-}" \
    "${mounts[@]}" -w "$repo_dir" "$image" \
    bash tools/jelli-art/ui_tests/run.sh "$@"
