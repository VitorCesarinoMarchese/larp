#!/usr/bin/env python3
"""Compare CUDA profile parsing in a private mount namespace; never edit the driver."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, help="Installed NVIDIA application profile file")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="Sanitized probe and arguments")
    args = parser.parse_args()
    if not args.command or not shutil.which("bwrap"):
        parser.error("A probe command and bubblewrap (bwrap) are required")
    profile = args.profile.resolve(strict=True)
    # NVIDIA's shipped JSON permits whole-line comments.
    original = json.loads("\n".join(
        line for line in profile.read_text().splitlines()
        if not line.lstrip().startswith("#")
    ))
    name = "CudaNoStablePerfLimit"
    matching = [entry for entry in original["profiles"] if entry["name"] == name]
    if len(matching) != 1:
        parser.error("Expected exactly one CudaNoStablePerfLimit profile")
    filtered = dict(original, profiles=[
        entry for entry in original["profiles"] if entry["name"] != name
    ])
    minimal = {"profiles": matching, "rules": []}
    renamed = {"profiles": [dict(matching[0], name="LarpLeakProbe")], "rules": []}
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:protect_shadow_gap=0")
    failed = False
    with tempfile.TemporaryDirectory(prefix="larp-profiles-") as directory:
        for label, data in [("original", None), ("filtered", filtered),
                            ("minimal-duplicate", minimal), ("renamed", renamed)]:
            command = ["bwrap", "--ro-bind", "/", "/", "--dev-bind", "/dev", "/dev",
                       "--proc", "/proc"]
            if data is not None:
                fixture = Path(directory) / f"{label}.json"
                fixture.write_text(json.dumps(data))
                command += ["--ro-bind", str(fixture), str(profile)]
            print(f"PROFILE CASE: {label}", flush=True)
            result = subprocess.run(command + args.command, env=env, check=False)
            print(f"PROFILE RESULT: {label} exit={result.returncode}", flush=True)
            failed |= result.returncode != 0
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
