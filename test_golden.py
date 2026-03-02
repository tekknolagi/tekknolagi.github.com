#!/usr/bin/env python3
"""Golden test: rebuild the site and compare against saved checksums.

Usage:
    python3 test_golden.py              # verify build output matches golden checksums
    python3 test_golden.py --update     # rebuild and update the golden checksums file
"""

import hashlib
import json
import os
import subprocess
import sys

GOLDEN_FILE = os.path.join(os.path.dirname(__file__), "golden_checksums.json")
SITE_DIR = os.path.join(os.path.dirname(__file__), "_site")
RENDERED_EXTS = {".html", ".xml", ".css", ".xsl"}


def checksum(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def collect_checksums(site_dir):
    """Walk the _site directory and return {relative_path: sha256} for rendered files."""
    result = {}
    for root, _dirs, files in os.walk(site_dir):
        for name in files:
            if os.path.splitext(name)[1] not in RENDERED_EXTS:
                continue
            full = os.path.join(root, name)
            rel = os.path.relpath(full, site_dir)
            result[rel] = checksum(full)
    return dict(sorted(result.items()))


def build():
    """Run build.py and return True on success."""
    script = os.path.join(os.path.dirname(__file__), "build.py")
    result = subprocess.run([sys.executable, script], capture_output=True, text=True)
    if result.returncode != 0:
        print("Build failed:")
        print(result.stderr)
        return False
    return True


def update():
    """Rebuild the site and save fresh golden checksums."""
    if not build():
        sys.exit(1)
    checksums = collect_checksums(SITE_DIR)
    with open(GOLDEN_FILE, "w") as f:
        json.dump(checksums, f, indent=2)
        f.write("\n")
    print(f"Updated {GOLDEN_FILE} ({len(checksums)} files)")


def verify():
    """Rebuild the site and compare against the golden checksums."""
    if not os.path.exists(GOLDEN_FILE):
        print(f"No golden file found at {GOLDEN_FILE}. Run with --update first.")
        sys.exit(1)

    with open(GOLDEN_FILE) as f:
        expected = json.load(f)

    if not build():
        sys.exit(1)

    actual = collect_checksums(SITE_DIR)
    ok = True

    missing = set(expected) - set(actual)
    extra = set(actual) - set(expected)
    changed = {
        k for k in set(expected) & set(actual) if expected[k] != actual[k]
    }

    if missing:
        ok = False
        for f in sorted(missing):
            print(f"MISSING: {f}")
    if extra:
        ok = False
        for f in sorted(extra):
            print(f"EXTRA:   {f}")
    if changed:
        ok = False
        for f in sorted(changed):
            print(f"CHANGED: {f}")

    if ok:
        print(f"OK: all {len(expected)} files match")
    else:
        print(f"\nFAILED: {len(missing)} missing, {len(extra)} extra, {len(changed)} changed")
        sys.exit(1)


if __name__ == "__main__":
    if "--update" in sys.argv:
        update()
    else:
        verify()
