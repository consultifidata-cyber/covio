"""
build_identity.py -- pure decision logic for firmware build-identity
resolution (git commit SHA, dirty-tree status, UTC build timestamp).

Deliberately dependency-free of PlatformIO/SCons (no `Import("env")`, no
subprocess calls at module-import time) so it is directly host-testable --
same "pure function extracted for testability" pattern this project
already uses for server.py's _resolve_admin_credentials(). See
test/native/test_build_identity.py.

The actual PlatformIO wiring (Import("env"), env.Append(CPPDEFINES=...))
lives in scripts/generate_build_identity_extra.py, which imports
resolve_build_identity() from here rather than duplicating this logic.
"""
import re
import subprocess
import datetime

_SHA_RE = re.compile(r"^[0-9a-f]{40}$")
_PLACEHOLDER_VALUES = {None, "", "unknown", "dev", "local", "0" * 40}


class BuildIdentityError(Exception):
    """Raised when a release build's identity cannot be safely resolved.
    Carries no side effect itself -- the caller turns this into an actual
    build failure (see generate_build_identity_extra.py)."""


def resolve_build_identity(pio_env_name, git_head_fn, git_dirty_fn, now_fn):
    """Pure decision logic -- no subprocess/env access of its own.

    git_head_fn() -> str or None   : full 40-char commit SHA, or None if
                                      git is unavailable / HEAD unresolved.
    git_dirty_fn() -> bool or None : True if TRACKED source differs from
                                      HEAD, False if clean, None if git
                                      status itself could not be determined.
                                      Untracked files must NOT make this
                                      True (matches this project's own
                                      "tracked source differs from the
                                      recorded commit" wording).
    now_fn() -> datetime.datetime (UTC)

    Returns (commit, dirty, build_time_utc_str).

    Raises BuildIdentityError for a release build (pio_env_name=="release")
    whose identity is not safely resolvable: missing, malformed, a
    placeholder value, dirty, or unknown-dirty-status. Never raises for a
    non-release build -- those always get a best-effort identity instead
    (commit="dev-nogit" / dirty=True when uncertain), matching config.h's
    own existing RELEASE_BUILD-gated fail-closed-only-for-production
    posture.
    """
    is_release = (pio_env_name == "release")
    commit = git_head_fn()
    dirty = git_dirty_fn()
    build_time_utc = now_fn().strftime("%Y-%m-%dT%H:%M:%SZ")

    if is_release:
        if commit is None or commit in _PLACEHOLDER_VALUES or not _SHA_RE.match(commit):
            raise BuildIdentityError(
                "RELEASE_BUILD requires a resolvable, well-formed 40-character "
                "git commit SHA; got %r. Refusing to build an unidentified "
                "release image." % (commit,))
        if dirty is None:
            raise BuildIdentityError(
                "RELEASE_BUILD requires a known clean/dirty git status; "
                "`git diff --quiet HEAD --` could not be evaluated. Refusing "
                "to build a release image whose source identity cannot be "
                "confirmed.")
        if dirty:
            raise BuildIdentityError(
                "RELEASE_BUILD requires a clean working tree (tracked source "
                "must exactly match the recorded commit); uncommitted "
                "changes to tracked files were detected. Refusing to build "
                "a release image from a dirty tree.")
        return commit, False, build_time_utc

    # Non-release (bench/factory) builds: best-effort identity, never fails
    # the build -- these are dev/bench artifacts, never claimed production-
    # certified regardless of what this reports.
    if commit is None or commit in _PLACEHOLDER_VALUES or not _SHA_RE.match(commit):
        commit = "dev-nogit"
    if dirty is None:
        dirty = True   # unknown status must never be reported as clean
    return commit, dirty, build_time_utc


def git_head(cwd=None):
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=cwd, stderr=subprocess.STDOUT
        ).decode().strip()
        return out
    except Exception:
        return None


def git_dirty(cwd=None):
    # `git diff --quiet HEAD --` exits 1 if TRACKED files differ from HEAD,
    # 0 if not -- deliberately ignores untracked files, matching this
    # module's own "tracked source differs" contract (an untracked new
    # Docs/ file must never make a release build refuse to compile).
    try:
        result = subprocess.call(
            ["git", "diff", "--quiet", "HEAD", "--"], cwd=cwd,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return result != 0
    except Exception:
        return None


def now_utc():
    return datetime.datetime.utcnow()
