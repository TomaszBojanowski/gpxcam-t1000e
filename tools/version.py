# PlatformIO pre-build script: puts the git revision into the firmware as FW_GIT.
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO/SCons)


def git_revision():
    try:
        return subprocess.run(
            ["git", "describe", "--always", "--dirty", "--abbrev=7", "--exclude=*"],  # a hash, also on a tag
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "none"


env.Append(CPPDEFINES=[("FW_GIT", env.StringifyMacro(git_revision()))])  # noqa: F821
