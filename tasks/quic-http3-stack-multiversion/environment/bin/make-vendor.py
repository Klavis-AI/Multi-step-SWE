#!/usr/bin/env python3
"""Assemble one milestone's cargo *directory source* out of locked crates.

Why this exists
---------------
The agent container's `CARGO_HOME` is the union of all fifteen milestones'
dependency sets -- one image serves all fifteen steps. Left at that, a
candidate's own `cargo build --offline` resolves crates the grader's cache does
not carry, so a dependency outside the milestone's set looks fine locally and
fails at grading time, silently. The environment has to hold the candidate to
the same set the verifier will, and the only lever that is per-step inside one
shared container is a file the step itself ships: `workdir/.cargo/config.toml`.

That file points `crates-io` at `/opt/vendor/NN` through `[source]`
replacement. A directory source is cargo's own format for a vendored
dependency set (`cargo vendor` writes one), so this is a stock mechanism, not a
special case: with it in place, resolution consults only the crates named in
the locks this script was given, and an unlisted dependency fails with

    no matching package named `X` found
    location searched: directory source `/opt/vendor/NN`

-- the same failure, in the same words, that the verifier image produces.

Both images build the same directory from the same lock files, so the agent's
cache and the grader's are the same set by construction.

What it links
-------------
Every `registry+` package any of the named locks carries, hard-linked out of
`$CARGO_HOME/registry/src/<registry>/<name>-<version>` into
`<outdir>/<name>-<version>`, plus the `.cargo-checksum.json` a directory source
requires. Hard links, so the vendor tree costs no disk beside the cache it is
built from. `git+` packages are deliberately not linked: they are already in
`$CARGO_HOME/git` in both images, and a git dependency that is not already
there is one a candidate could not have fetched offline anyway.

Usage
-----
    make-vendor.py <outdir> <cargo-home> <lock> [<lock> ...]
"""
import json
import os
import shutil
import sys

try:
    import tomllib
except ImportError:  # python3 < 3.11
    import tomli as tomllib  # type: ignore


def registry_packages(path):
    """{(name, version): checksum} for every registry package in one lock."""
    with open(path, "rb") as fh:
        lock = tomllib.load(fh)
    out = {}
    for pkg in lock.get("package", []):
        if pkg.get("source", "").startswith("registry+"):
            out[(pkg["name"], pkg["version"])] = pkg.get("checksum")
    return out


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    outdir, cargo_home, locks = sys.argv[1], sys.argv[2], sys.argv[3:]

    want = {}
    for lock in locks:
        for key, csum in registry_packages(lock).items():
            if key in want and want[key] != csum:
                sys.exit(f"{key}: two locks disagree on the checksum")
            want[key] = csum

    srcroot = os.path.join(cargo_home, "registry", "src")
    index = {}
    for reg in os.listdir(srcroot):
        for d in os.listdir(os.path.join(srcroot, reg)):
            index.setdefault(d, os.path.join(srcroot, reg, d))

    os.makedirs(outdir, exist_ok=True)
    linked, copied, missing = 0, 0, []
    for (name, version), csum in sorted(want.items()):
        d = f"{name}-{version}"
        src = index.get(d)
        if src is None:
            missing.append(d)
            continue
        dst = os.path.join(outdir, d)
        if not os.path.exists(dst):
            try:
                shutil.copytree(src, dst, copy_function=os.link)
                linked += 1
            except OSError:
                # a hard link cannot cross a filesystem; fall back to copying
                # (the half-built tree has to go first, copytree refuses to
                # merge into a directory it did not create)
                shutil.rmtree(dst, ignore_errors=True)
                shutil.copytree(src, dst)
                copied += 1
        with open(os.path.join(dst, ".cargo-checksum.json"), "w") as fh:
            # `files` empty: the crates came out of this image's own cache, so
            # there is no per-file hash to check them against, and cargo skips
            # file-level verification when the map is empty. `package` is the
            # .crate checksum the lock records, which is what cargo compares
            # against Cargo.lock.
            json.dump({"files": {}, "package": csum}, fh)
    print(f"{outdir}: {len(want)} locked crates, {linked} hard-linked, "
          f"{copied} copied, {len(missing)} missing from the cache")
    if missing:
        print("MISSING: " + ", ".join(missing[:20]), file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
