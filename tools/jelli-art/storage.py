"""Crash-safe, confined file writes for Jelli Art.

Every studio write goes through here:

- Paths are resolved against a fixed root (assets/slice or content/). A path that
  is absolute, climbs out with "..", or whose directory resolves outside the root
  through a symlink is refused; the file itself is replaced by rename, so a
  symlink at the target is replaced rather than followed.
- Files are written to a temporary sibling, flushed and fsynced, then renamed
  over the target, so the game watcher, the page and a crash never see half a
  file. A save that touches several files stages all of them first and renames
  them only when every temporary file is complete.
- WriteLock serialises saves within the process (a thread lock) and across
  studio processes serving the same root (an advisory flock where available).
- clean_orphans() removes temporary files a crash left behind.
"""
import hashlib
import io
import json
import os
import tempfile
import threading
import time
from pathlib import Path

try:
    import fcntl
except ImportError:  # Windows: saves are still serialised within the process
    fcntl = None

TEMP_MARK = ".jelli-art-tmp-"
LEGACY_TEMPS = ("*.json.tmp",)  # left by studio versions before 0.3 (write_json used <name>.json.tmp)


class UnsafePath(ValueError):
    """A write outside the studio's roots, or through a symlink."""


class Busy(RuntimeError):
    """Another save held the write lock for too long."""


def confined(root, relative):
    """root/relative, refusing anything that would land outside root."""
    root = Path(root).resolve()
    rel = Path(relative)
    if rel.is_absolute() or ".." in rel.parts or not rel.parts:
        raise UnsafePath(f"Refusing to write outside {root.name}: {relative}")
    target = root / rel
    parent = target.parent.resolve()
    if parent != root and root not in parent.parents:
        raise UnsafePath(f"Refusing to write through a symlink: {relative}")
    if target.is_symlink() or (target.exists() and not target.is_file()):
        raise UnsafePath(f"Refusing to replace a link or directory: {relative}")
    return parent / target.name


def json_bytes(value):
    return (json.dumps(value, indent=2) + "\n").encode()


def png_bytes(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    return buffer.getvalue()


def digest_bytes(data):
    return hashlib.sha1(data).hexdigest()[:16]


def digest(path):
    """Short content hash of a file, or None when it does not exist."""
    try:
        return digest_bytes(Path(path).read_bytes())
    except FileNotFoundError:
        return None


def _stage(path, data):
    """Write data to a fsynced temporary sibling of path; returns the temporary path."""
    fd, temp = tempfile.mkstemp(prefix=f".{path.name}{TEMP_MARK}", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        if path.exists():
            os.chmod(temp, path.stat().st_mode & 0o777)
        else:
            os.chmod(temp, 0o644)
    except BaseException:
        Path(temp).unlink(missing_ok=True)
        raise
    return Path(temp)


def _sync_dir(directory):
    try:
        fd = os.open(directory, os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(fd)
    except OSError:
        pass
    finally:
        os.close(fd)


def write_files(files):
    """Atomically replace each path with its bytes: stage every file, then rename them all.

    files is a list of (path, bytes). Paths must already be confined(). If any
    temporary file cannot be written, nothing is replaced.
    """
    staged = []
    try:
        for path, data in files:
            staged.append((_stage(Path(path), data), Path(path)))
    except BaseException:
        for temp, _ in staged:
            temp.unlink(missing_ok=True)
        raise
    for temp, path in staged:
        os.replace(temp, path)
    for directory in {path.parent for _, path in staged}:
        _sync_dir(directory)


def write_file(path, data):
    write_files([(path, data)])


def clean_orphans(*roots):
    """Delete temporary files a crashed save left behind; returns their paths."""
    removed = []
    for root in roots:
        root = Path(root)
        if not root.is_dir():
            continue
        patterns = [f"**/.*{TEMP_MARK}*", *(f"**/{p}" for p in LEGACY_TEMPS)]
        for pattern in patterns:
            for path in root.glob(pattern):
                if path.is_file() and not path.is_symlink():
                    try:
                        path.unlink(missing_ok=True)
                    except OSError:  # a read-only review mount: leave it, saves are refused there anyway
                        continue
                    removed.append(path)
    return removed


def check_json(*paths):
    """Problems with JSON files the studio reads: [(path, message)] for each one that does not parse."""
    problems = []
    for path in paths:
        path = Path(path)
        if not path.exists():
            continue
        try:
            json.loads(path.read_text())
        except (OSError, ValueError) as error:
            problems.append((path, str(error)))
    return problems


class WriteLock:
    """Serialise saves: a thread lock, plus flock on a per-root lock file shared by other studio processes."""

    def __init__(self, timeout=60.0):
        self.timeout = timeout
        self.thread_lock = threading.Lock()
        self.path = None
        self.handle = None

    def bind(self, root):
        """Use a lock file keyed by the served root, so two studios on one checkout queue their saves."""
        key = hashlib.sha1(str(Path(root).resolve()).encode()).hexdigest()[:12]
        self.path = Path(tempfile.gettempdir()) / f"jelli-art-{key}.lock"

    def _flock(self, deadline):
        if self.path is None or fcntl is None:
            return
        handle = open(self.path, "a+")  # noqa: SIM115 (closed in release)
        while True:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                self.handle = handle
                return
            except BlockingIOError:
                if time.monotonic() >= deadline:
                    handle.close()
                    raise Busy("Another Jelli Art process is saving to these files; try again") from None
                time.sleep(0.05)

    def _enter(self, timeout):
        deadline = time.monotonic() + timeout
        if not self.thread_lock.acquire(timeout=timeout):
            raise Busy("Another save is still running; try again in a moment")
        try:
            self._flock(deadline)
        except BaseException:
            self.thread_lock.release()
            raise
        return self

    def __enter__(self):
        return self._enter(self.timeout)

    def __exit__(self, *exc):
        if self.handle is not None:
            fcntl.flock(self.handle, fcntl.LOCK_UN)
            self.handle.close()
            self.handle = None
        self.thread_lock.release()
        return False

    def acquire(self, timeout=None):
        """For callers outside a with-block (git_sync's rebase); returns False on timeout."""
        try:
            self._enter(self.timeout if timeout is None else timeout)
            return True
        except Busy:
            return False

    def release(self):
        self.__exit__(None, None, None)
