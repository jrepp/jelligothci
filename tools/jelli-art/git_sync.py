"""Commit studio saves to a working branch and push it in the background.

Used by the hosted studio: every save becomes one commit on the configured
branch (for example art/studio), and a background push keeps the branch on
GitHub so changes arrive as a reviewable pull request. Local runs leave git alone.
"""
import subprocess
import threading
import time


class GitSync:
    def __init__(self, repo, branch, push=False, remote="origin"):
        self.repo, self.branch, self.push_enabled, self.remote = repo, branch, push, remote
        self.lock = threading.Lock()
        self.push_wanted = threading.Event()
        self.last_push = {"ok": None, "at": None, "error": ""}
        if push:
            threading.Thread(target=self._push_loop, daemon=True, name="git-push").start()

    def git(self, *args, check=True):
        result = subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True, text=True, check=False)
        if check and result.returncode:
            raise RuntimeError(f"git {args[0]} failed: {result.stderr.strip() or result.stdout.strip()}")
        return result.stdout.strip()

    def commit(self, paths, subject, artist=""):
        """Stage exactly these paths and commit them; returns the new short SHA or None."""
        with self.lock:
            current = self.git("rev-parse", "--abbrev-ref", "HEAD")
            if current != self.branch:
                raise RuntimeError(f"Studio checkout is on {current}, expected {self.branch}")
            self.git("add", "--", *[str(p) for p in paths])
            if not self.git("diff", "--cached", "--name-only"):
                return None
            body = f"\n\nPainted-by: {artist.strip()[:80]}" if artist.strip() else ""
            self.git("commit", "--quiet", "-m", subject + body)
            sha = self.git("rev-parse", "--short", "HEAD")
        if self.push_enabled:
            self.push_wanted.set()
        return sha

    def _push_loop(self):
        """Coalesce pushes; a failure is retried on the next save or after a minute."""
        while True:
            self.push_wanted.wait(timeout=60)
            if not self.push_wanted.is_set() and self.last_push["ok"] is not False:
                continue
            self.push_wanted.clear()
            with self.lock:
                result = subprocess.run(["git", "-C", str(self.repo), "push", "--quiet", self.remote, f"HEAD:refs/heads/{self.branch}"],
                                        capture_output=True, text=True, check=False)
            self.last_push = {"ok": result.returncode == 0, "at": int(time.time()),
                              "error": "" if result.returncode == 0 else (result.stderr.strip() or "push failed")[-300:]}

    def status(self):
        with self.lock:
            head = self.git("rev-parse", "--short", "HEAD", check=False)
            ahead = self.git("rev-list", "--count", f"{self.remote}/{self.branch}..HEAD", check=False)
        return {"enabled": True, "branch": self.branch, "head": head, "push": self.push_enabled,
                "unpushed": int(ahead) if ahead.isdigit() else None, "last_push": self.last_push}
