#!/usr/bin/env python3
"""Apply the UWP (Xbox Dev Mode) changes to the lib/ submodules.

The submodules point at the upstream jessetbh forks, so the UWP changes to them live here as patches instead of
commits. Run this after every `git submodule update` (it is idempotent: a patch that is already applied is skipped).

Usage: python uwp/apply_patches.py [--check]
  --check   only report whether each patch is applied, applicable or conflicting; change nothing
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATCHES = ROOT / "uwp" / "patches"

# patch file -> the repository it applies to (order matters only for readability; they are independent).
TARGETS = {
    "plume.patch": ROOT / "lib" / "rt64" / "src" / "contrib" / "plume",
    "rt64.patch": ROOT / "lib" / "rt64",
    "RecompFrontend.patch": ROOT / "lib" / "RecompFrontend",
}


def git_apply(repo, patch, *args):
    return subprocess.run(["git", "-C", str(repo), "apply", "--whitespace=nowarn", *args, str(patch)],
                          capture_output=True, text=True)


def main():
    check_only = "--check" in sys.argv[1:]
    failed = False
    for name, repo in TARGETS.items():
        patch = PATCHES / name
        if not (repo / ".git").exists():
            print(f"{name}: {repo.relative_to(ROOT)} is not checked out (git submodule update --init --recursive)")
            failed = True
            continue
        if git_apply(repo, patch, "--reverse", "--check").returncode == 0:
            print(f"{name}: already applied")
            continue
        forward = git_apply(repo, patch, "--check")
        if forward.returncode != 0:
            print(f"{name}: does NOT apply to {repo.relative_to(ROOT)} (submodule moved? regenerate the patch)\n{forward.stderr}")
            failed = True
            continue
        if check_only:
            print(f"{name}: applicable")
            continue
        result = git_apply(repo, patch)
        if result.returncode != 0:
            print(f"{name}: failed\n{result.stderr}")
            failed = True
        else:
            print(f"{name}: applied")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
