#!/usr/bin/env python3
"""Fetch the pinned Notepad4 upstream tree and apply the Markdown preview patch.

The repository does not vendor the whole upstream source tree (it is ~200 MB with
build artefacts). Instead ``upstream.lock`` pins a commit and this script
reproduces the exact tree the patch was generated against:

    python scripts/fetch_upstream.py            # -> ./notepad4
    python scripts/fetch_upstream.py --dest build/upstream-test --force

Both the local build scripts and the GitHub Actions workflow use this script, so
a fresh clone is buildable without manual steps.

Requirements: Git 2.25+ and network access to the upstream repository.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCK_FILE = ROOT / "upstream.lock"
PATCH_FILE = ROOT / "patch" / "0001-notepad4-markdown-preview.patch"
DEFAULT_DEST = ROOT / "notepad4"


def fail(message: str) -> "None":
    raise SystemExit(f"[fetch_upstream] {message}")


def read_lock() -> dict[str, str]:
    if not LOCK_FILE.is_file():
        fail(f"missing {LOCK_FILE}")
    values: dict[str, str] = {}
    for raw in LOCK_FILE.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    for key in ("repo", "commit"):
        if not values.get(key):
            fail(f"upstream.lock has no '{key}' entry")
    return values


def run(args: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def git(args: list[str], cwd: Path, what: str) -> None:
    result = run(["git", "-c", "core.autocrlf=false", *args], cwd=cwd)
    if result.returncode != 0:
        sys.stdout.write(result.stdout or "")
        fail(f"{what} failed (exit {result.returncode})")


def already_prepared(dest: Path) -> bool:
    return (dest / "src" / "Notepad4.cpp").is_file() and (dest / "src" / "MarkdownPreview.cpp").is_file()


def patch_applied(dest: Path) -> bool:
    return (dest / "src" / "MarkdownPreview.cpp").is_file()


def clone_pinned(dest: Path, repo: str, commit: str) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    print(f"[fetch_upstream] cloning {repo} @ {commit[:12]} -> {dest}")
    git(["init", "-q", "."], dest, "git init")
    git(["remote", "add", "origin", repo], dest, "git remote add")
    # Shallow fetch of a single commit: keeps CI fast and avoids full history.
    git(["fetch", "-q", "--depth", "1", "origin", commit], dest, "git fetch")
    git(["checkout", "-q", "FETCH_HEAD"], dest, "git checkout")


def apply_patch(dest: Path) -> None:
    if not PATCH_FILE.is_file():
        fail(f"missing patch file: {PATCH_FILE}")
    print(f"[fetch_upstream] applying {PATCH_FILE.name}")
    # core.autocrlf=false is essential on Windows runners: upstream stores CRLF
    # in the repository, and a checkout-time conversion would make the patch
    # context lines stop matching.
    git(["apply", "--whitespace=nowarn", str(PATCH_FILE)], dest, "git apply")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dest", default=str(DEFAULT_DEST), help="target directory (default: <repo>/notepad4)")
    parser.add_argument("--force", action="store_true", help="delete the target directory before cloning")
    args = parser.parse_args()

    if shutil.which("git") is None:
        fail("git not found in PATH")

    dest = Path(args.dest).resolve()
    lock = read_lock()

    if dest.exists():
        if args.force:
            print(f"[fetch_upstream] removing {dest}")
            shutil.rmtree(dest)
        elif already_prepared(dest):
            print(f"[fetch_upstream] {dest} already prepared, nothing to do")
            return 0
        elif (dest / "src" / "Notepad4.cpp").is_file():
            # Upstream is there but the patch is not: apply it in place.
            apply_patch(dest)
            print("[fetch_upstream] patch applied to existing upstream tree")
            return 0
        else:
            fail(f"{dest} exists but is not a Notepad4 tree; pass --force to replace it")

    clone_pinned(dest, lock["repo"], lock["commit"])
    apply_patch(dest)

    if not already_prepared(dest):
        fail("patch applied but src/MarkdownPreview.cpp is missing")

    head = run(["git", "-C", str(dest), "rev-parse", "HEAD"])
    print(f"[fetch_upstream] ready: {dest}")
    if head.returncode == 0:
        print(f"[fetch_upstream] HEAD {head.stdout.strip()}")
    print("[fetch_upstream] next: scripts\\build.cmd")
    return 0


if __name__ == "__main__":
    sys.exit(main())
