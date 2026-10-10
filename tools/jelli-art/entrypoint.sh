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
#
# A start must survive a crash or an offline host: a stale index.lock or an
# interrupted rebase from a killed git is cleared, a failed fetch starts from the
# local checkout, and studio files left uncommitted are never reset away
# (jelli_art.py commits them on start).
set -eu

repo="${JELLI_REPO:-/work/jelligotchi}"
base="${JELLI_BASE_BRANCH:-main}"
branch="${JELLI_STUDIO_BRANCH:-}"
key="${JELLI_DEPLOY_KEY:-/run/secrets/jelli-art-deploy-key}"
app="${JELLI_APP:-/app/tools/jelli-art}"
export JELLI_REPO="$repo"
export GIT_AUTHOR_NAME="${GIT_AUTHOR_NAME:-Jelli Art}" GIT_AUTHOR_EMAIL="${GIT_AUTHOR_EMAIL:-jelli-art@home.jrepp.com}"
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME" GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"
# The checkout may belong to another uid (CI mounts, host volumes); trust it explicitly.
# A throwaway global config keeps this working with a read-only home directory.
export GIT_CONFIG_GLOBAL="${GIT_CONFIG_GLOBAL:-/tmp/jelli-art.gitconfig}"
git config --global --get-all safe.directory 2>/dev/null | grep -qxF "$repo" || git config --global --add safe.directory "$repo"
if [ -r "$key" ]; then
    export GIT_SSH_COMMAND="ssh -i $key -o IdentitiesOnly=yes -o UserKnownHostsFile=$app/github_known_hosts -o StrictHostKeyChecking=yes -o ConnectTimeout=15"
fi
log() { echo "jelli-art: $*" >&2; }

if [ ! -d "$repo/.git" ]; then
    git clone --quiet --branch "$base" "${JELLI_GIT_URL:-https://github.com/jrepp/jelligothci.git}" "$repo"
fi
cd "$repo"
if [ -n "${JELLI_PUSH_URL:-}" ]; then
    git remote set-url --push origin "$JELLI_PUSH_URL"
fi

set -- --host "${JELLI_HOST:-0.0.0.0}" --port "${JELLI_PORT:-8765}" --no-open
if [ -n "$branch" ]; then
    # Nothing else runs git in this container yet, so a lock file was left by a killed git.
    if [ -f .git/index.lock ]; then
        log "removing a stale .git/index.lock"
        rm -f .git/index.lock
    fi
    if [ -d .git/rebase-merge ] || [ -d .git/rebase-apply ]; then
        log "aborting an interrupted rebase"
        git rebase --abort || true
    fi
    if ! git fetch --quiet origin; then
        log "fetch failed (offline?); starting from the local checkout, pushes will retry"
    fi
    if git show-ref --verify --quiet "refs/heads/$branch"; then
        git checkout --quiet "$branch"
    elif git show-ref --verify --quiet "refs/remotes/origin/$branch"; then
        git checkout --quiet -b "$branch" "origin/$branch"
    elif git show-ref --verify --quiet "refs/remotes/origin/$base"; then
        git checkout --quiet -b "$branch" "origin/$base"
    else
        git checkout --quiet -b "$branch"
    fi
    # Once the studio's work (art and content/) has landed on the base branch,
    # merged or squashed, restart from it. Uncommitted or new studio files block the reset.
    if git show-ref --verify --quiet "refs/remotes/origin/$base" \
        && git diff --quiet "origin/$base" HEAD -- assets/slice content \
        && git diff --quiet HEAD -- assets/slice content \
        && [ -z "$(git ls-files --others --exclude-standard -- assets/slice content)" ]; then
        git reset --quiet --hard "origin/$base"
    fi
    set -- "$@" --git-branch "$branch" --git-base "$base"
    if [ "${JELLI_GIT_PUSH:-0}" = "1" ]; then
        set -- "$@" --git-push
    fi
fi
exec python "$app/jelli_art.py" "$@"
