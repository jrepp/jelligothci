#!/bin/sh
# Prepare the studio checkout, then run Jelli Art.
#
# JELLI_REPO            checkout path inside the volume (default /work/jelligotchi)
# JELLI_GIT_URL         clone URL when the checkout is missing (public HTTPS by default)
# JELLI_BASE_BRANCH     branch new studio branches start from (default main)
# JELLI_STUDIO_BRANCH   commit every save to this branch; unset = no git writes
# JELLI_GIT_PUSH        1 = push the studio branch (needs JELLI_PUSH_URL and a deploy key)
# JELLI_PUSH_URL        SSH push URL, e.g. git@github.com:jrepp/jelligothci.git
# JELLI_DEPLOY_KEY      private key file (default /run/secrets/jelli-art-deploy-key)
set -eu

repo="${JELLI_REPO:-/work/jelligotchi}"
base="${JELLI_BASE_BRANCH:-main}"
branch="${JELLI_STUDIO_BRANCH:-}"
key="${JELLI_DEPLOY_KEY:-/run/secrets/jelli-art-deploy-key}"
export JELLI_REPO="$repo"
export GIT_AUTHOR_NAME="${GIT_AUTHOR_NAME:-Jelli Art}" GIT_AUTHOR_EMAIL="${GIT_AUTHOR_EMAIL:-jelli-art@home.jrepp.com}"
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME" GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"
# The checkout may belong to another uid (CI mounts, host volumes); trust it explicitly.
# A throwaway global config keeps this working with a read-only home directory.
export GIT_CONFIG_GLOBAL="${GIT_CONFIG_GLOBAL:-/tmp/jelli-art.gitconfig}"
git config --global --get-all safe.directory 2>/dev/null | grep -qxF "$repo" || git config --global --add safe.directory "$repo"
if [ -r "$key" ]; then
    export GIT_SSH_COMMAND="ssh -i $key -o IdentitiesOnly=yes -o UserKnownHostsFile=/app/tools/jelli-art/github_known_hosts -o StrictHostKeyChecking=yes"
fi

if [ ! -d "$repo/.git" ]; then
    git clone --quiet --branch "$base" "${JELLI_GIT_URL:-https://github.com/jrepp/jelligothci.git}" "$repo"
fi
cd "$repo"
[ -n "${JELLI_PUSH_URL:-}" ] && git remote set-url --push origin "$JELLI_PUSH_URL"

set -- --host 0.0.0.0 --port "${JELLI_PORT:-8765}" --no-open
if [ -n "$branch" ]; then
    git fetch --quiet origin
    if git show-ref --verify --quiet "refs/heads/$branch"; then
        git checkout --quiet "$branch"
    elif git show-ref --verify --quiet "refs/remotes/origin/$branch"; then
        git checkout --quiet -b "$branch" "origin/$branch"
    else
        git checkout --quiet -b "$branch" "origin/$base"
    fi
    # Once the studio's art has landed on the base branch (merged or squashed), restart from it.
    if git diff --quiet "origin/$base" HEAD -- assets/slice && git diff --quiet HEAD -- assets/slice; then
        git reset --quiet --hard "origin/$base"
    fi
    set -- "$@" --git-branch "$branch"
    [ "${JELLI_GIT_PUSH:-0}" = "1" ] && set -- "$@" --git-push
fi
exec python /app/tools/jelli-art/jelli_art.py "$@"
