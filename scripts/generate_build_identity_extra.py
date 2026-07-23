"""
generate_build_identity_extra.py -- PlatformIO extra_script (pre-build).

Wired into platformio.ini's shared [env] section
(`extra_scripts = pre:scripts/generate_build_identity_extra.py`), so it
runs for every environment (esp32dev, release, factory) before
compilation. Computes BUILD_COMMIT/BUILD_DIRTY/BUILD_TIME_UTC fresh from
git at build time -- never hand-maintained, no generated file checked
into source control, only compiler defines for this one build invocation.

All decision logic lives in scripts/build_identity.py (plain, dependency-
free, host-testable); this file is only the thin PlatformIO/SCons wiring
around it, kept deliberately free of any logic worth unit-testing on its
own.
"""
import sys
import os

Import("env")  # noqa: F821  -- SCons-injected global, only valid under `pio run`

# NOTE: `__file__` is NOT defined inside an SConscript's exec() context (a
# known SCons quirk, confirmed by this script's own first real `pio run` --
# see Docs/FIRMWARE_BUILD.md), so this script's own directory is derived
# from env["PROJECT_DIR"] instead of the usual __file__ trick.
project_dir = env["PROJECT_DIR"]
sys.path.insert(0, os.path.join(project_dir, "scripts"))
from build_identity import (  # noqa: E402
    resolve_build_identity, git_head, git_dirty, now_utc, BuildIdentityError,
)

try:
    commit, dirty, build_time_utc = resolve_build_identity(
        env["PIOENV"],
        lambda: git_head(cwd=project_dir),
        lambda: git_dirty(cwd=project_dir),
        now_utc,
    )
except BuildIdentityError as e:
    sys.stderr.write("[build_identity] FAIL-CLOSED (%s): %s\n" % (env["PIOENV"], e))
    env.Exit(1)
    raise SystemExit(1)  # unreachable in a real PlatformIO run; keeps static analysis happy

env.Append(CPPDEFINES=[
    ("BUILD_COMMIT", env.StringifyMacro(commit)),
    ("BUILD_DIRTY", 1 if dirty else 0),
    ("BUILD_TIME_UTC", env.StringifyMacro(build_time_utc)),
])

print("[build_identity] env=%s commit=%s dirty=%s build_time_utc=%s"
      % (env["PIOENV"], commit, dirty, build_time_utc))
