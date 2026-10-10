"""Commit studio saves to a working branch and push it in the background.

Used by the hosted studio: every save becomes one commit on the configured
branch (for example art/studio), and a background push keeps the branch on
GitHub so changes arrive as a reviewable pull request. Local runs leave git alone.

Failure handling, all visible through status() (GET /api/git):

- A commit only ever includes the files that save wrote (`git commit -- paths`),
  even when other changes are staged in the checkout.
- A commit that fails (index.lock held, wrong branch, hook failure) is queued
  and retried before the next commit and by the background loop. The save
  itself is already on disk; nothing is lost.
- recover() commits studio files left uncommitted by a crash between a write
  and its commit, so they are not discarded by the next start's reset.
- A push that fails (network down, auth) is retried with backoff. A rejected
  push is resolved when it is safe: if the remote branch's art already landed on
  the base branch (merged or squashed), the branch is replaced with
  --force-with-lease; if the remote has new commits, this branch is rebased onto
  them when that applies cleanly. Otherwise the error says a human is needed.
"""
import subprocess
import threading
import time

STUDIO_PATHS = ("assets/slice", "content")
RETRY_MIN_S, RETRY_MAX_S = 15, 600


def classify(stderr):
    text = stderr.lower()
    if "non-fast-forward" in text or "fetch first" in text or "[rejected]" in text or "stale info" in text:
        return "rejected"
    if "permission denied" in text or "authentication" in text or "publickey" in text:
        return "auth"
    if "could not resolve" in text or "unable to access" in text or "connection" in text or "timed out" in text:
        return "network"
    return "error"


class GitSync:
    def __init__(self, repo, branch, push=False, remote="origin", base=None, write_lock=None):
        self.repo, self.branch, self.push_enabled, self.remote = repo, branch, push, remote
        self.base = base  # the branch studio work lands on (main); enables the landed-art push repair
        self.write_lock = write_lock  # the studio's save lock, held while a rebase rewrites files
        self.lock = threading.Lock()
        self.wake = threading.Event()
        self.pending = []  # [(paths, subject, artist, error)] commits that failed and will be retried
        self.last_commit_error = ""
        self.last_push = {"ok": None, "at": None, "error": "", "kind": ""}
        self.failures = 0
        self.next_retry_at = None
        self.push_requested = False
        threading.Thread(target=self._loop, daemon=True, name="git-sync").start()

    def git(self, *args, check=True, timeout=120):
        try:
            result = subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True, text=True,
                                    check=False, timeout=timeout)
        except FileNotFoundError as error:
            raise RuntimeError("git is not installed") from error
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"git {args[0]} timed out") from error
        if check and result.returncode:
            raise RuntimeError(f"git {args[0]} failed: {result.stderr.strip() or result.stdout.strip()}")
        return result.stdout.strip() if check else result

    # ---------- commits ----------

    def _commit_now(self, paths, subject, artist):
        current = self.git("rev-parse", "--abbrev-ref", "HEAD")
        if current != self.branch:
            raise RuntimeError(f"Studio checkout is on {current}, expected {self.branch}")
        names = [str(p) for p in paths]
        self.git("add", "--", *names)
        if not self.git("diff", "--cached", "--name-only", "--", *names):
            return None
        body = f"\n\nPainted-by: {artist.strip()[:80]}" if artist.strip() else ""
        self.git("commit", "--quiet", "--no-verify", "-m", subject + body, "--", *names)
        return self.git("rev-parse", "--short", "HEAD")

    def _flush_pending(self):
        """Retry queued commits in order; stops at the first that still fails. Caller holds self.lock."""
        while self.pending:
            paths, subject, artist, _ = self.pending[0]
            try:
                self._commit_now(paths, subject, artist)
            except RuntimeError as error:
                self.pending[0] = (paths, subject, artist, str(error))
                self.last_commit_error = str(error)
                return False
            self.pending.pop(0)
        self.last_commit_error = ""
        return True

    def commit(self, paths, subject, artist=""):
        """Stage exactly these paths and commit them; returns the new short SHA or None.

        Raises RuntimeError when the commit fails; the commit is then queued for retry.
        """
        with self.lock:
            if not self._flush_pending():
                self.pending.append((list(paths), subject, artist, self.last_commit_error))
                raise RuntimeError(f"{self.last_commit_error} (queued; {len(self.pending)} commit(s) will be retried)")
            try:
                sha = self._commit_now(paths, subject, artist)
            except RuntimeError as error:
                self.last_commit_error = str(error)
                self.pending.append((list(paths), subject, artist, str(error)))
                raise RuntimeError(f"{error} (queued; will be retried)") from error
        if sha and self.push_enabled:
            self.request_push()
        return sha

    def recover(self, paths=STUDIO_PATHS, subject="chore(art): recover studio edits left uncommitted"):
        """Commit studio files a crash left uncommitted; returns the SHA or None.

        Only what a save could have written: modified tracked files, and new
        .png/.json files (hand-painted.json on its first save). Ignored files and
        anything else stay out of the commit.
        """
        with self.lock:
            changed = self.git("status", "--porcelain", "-z", "--untracked-files=all", "--", *paths, check=False)
        if changed.returncode:
            return None
        files = []
        for entry in filter(None, changed.stdout.split("\0")):
            code, name = entry[:2], entry[3:]
            if code == "??" and not name.endswith((".png", ".json")):
                continue
            if "D" in code or "R" in code:
                continue
            files.append(name)
        if not files:
            return None
        try:
            return self.commit(files, subject)
        except RuntimeError:
            return None

    # ---------- pushes ----------

    def request_push(self):
        self.push_requested = True
        self.wake.set()

    def retry_now(self):
        """Wake the background loop now (POST /api/git/retry)."""
        self.next_retry_at = None
        self.push_requested = self.push_enabled
        self.wake.set()

    def _push(self, *extra):
        return self.git("push", "--quiet", *extra, self.remote, f"HEAD:refs/heads/{self.branch}",
                        check=False)

    def _remote_ref(self, name):
        result = self.git("rev-parse", "--verify", "--quiet", f"refs/remotes/{self.remote}/{name}", check=False)
        return result.stdout.strip() if result.returncode == 0 else None

    def _same_studio_files(self, a, b):
        return self.git("diff", "--quiet", a, b, "--", *STUDIO_PATHS, check=False).returncode == 0

    def _repair_rejection(self):
        """After a rejected push: replace landed remote work, or rebase onto new remote work. Returns (ok, error)."""
        self.git("fetch", "--quiet", self.remote, check=False)
        remote = self._remote_ref(self.branch)
        if remote is None:
            return False, "push rejected and the remote branch could not be fetched"
        if self.git("merge-base", "--is-ancestor", remote, "HEAD", check=False).returncode == 0:
            result = self._push()
            return result.returncode == 0, result.stderr.strip()
        base = self._remote_ref(self.base) if self.base else None
        if base:
            fork = self.git("merge-base", "HEAD", base, check=False).stdout.strip()
            if self._same_studio_files(remote, base) or (fork and self._same_studio_files(remote, fork)):
                result = self._push(f"--force-with-lease=refs/heads/{self.branch}:{remote}")
                return result.returncode == 0, result.stderr.strip()
        return self._rebase_onto(remote)

    def _rebase_onto(self, remote):
        """Caller holds the save lock (the rebase rewrites files the server serves) and self.lock."""
        if self.git("rebase", "--quiet", remote, check=False).returncode:
            self.git("rebase", "--abort", check=False)
            return False, (f"push rejected: {self.remote}/{self.branch} has commits that conflict with this "
                           "studio's commits; resolve the branch by hand")
        result = self._push()
        return result.returncode == 0, result.stderr.strip()

    def _repair_locked(self):
        """Repair a rejected push, taking locks in a save's order: the save lock, then self.lock."""
        if self.write_lock is not None and not self.write_lock.acquire(timeout=30):
            return False, "push rejected; a save is running, will retry"
        try:
            with self.lock:
                return self._repair_rejection()
        finally:
            if self.write_lock is not None:
                self.write_lock.release()

    def _push_once(self):
        with self.lock:
            result = self._push()
        ok, error, kind = result.returncode == 0, (result.stderr + result.stdout).strip(), ""
        if not ok:
            kind = classify(error)
            if kind == "rejected":
                ok, repair_error = self._repair_locked()
                error = repair_error if not ok else ""
        self.last_push = {"ok": ok, "at": int(time.time()), "kind": "" if ok else kind,
                          "error": "" if ok else (error or "push failed")[-300:]}
        if ok:
            self.failures, self.next_retry_at = 0, None
        else:
            self.failures += 1
            delay = min(RETRY_MAX_S, RETRY_MIN_S * 2 ** min(self.failures - 1, 6))
            self.next_retry_at = time.time() + delay

    def _loop(self):
        """Retry queued commits and coalesce pushes; failures back off from 15 s to 10 min."""
        while True:
            timeout = 60 if self.next_retry_at is None else max(1, self.next_retry_at - time.time())
            self.wake.wait(timeout=timeout)
            self.wake.clear()
            if self.pending:
                with self.lock:
                    flushed = self._flush_pending()
                if flushed and self.push_enabled:
                    self.push_requested = True
            if not self.push_enabled:
                continue
            due = self.next_retry_at is not None and time.time() >= self.next_retry_at
            if self.push_requested or due:
                self.push_requested = False
                try:
                    self._push_once()
                except RuntimeError as error:  # git missing or timed out
                    self.last_push = {"ok": False, "at": int(time.time()), "kind": "error", "error": str(error)}
                    self.failures += 1
                    self.next_retry_at = time.time() + RETRY_MAX_S

    def status(self):
        with self.lock:
            head = self.git("rev-parse", "--short", "HEAD", check=False)
            ahead = self.git("rev-list", "--count", f"{self.remote}/{self.branch}..HEAD", check=False)
        unpushed = ahead.stdout.strip() if ahead.returncode == 0 else ""
        return {"enabled": True, "branch": self.branch, "head": head.stdout.strip(), "push": self.push_enabled,
                "unpushed": int(unpushed) if unpushed.isdigit() else None, "last_push": self.last_push,
                "pending_commits": len(self.pending), "last_commit_error": self.last_commit_error,
                "next_retry_at": int(self.next_retry_at) if self.next_retry_at else None}
