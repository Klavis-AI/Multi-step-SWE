#!/usr/bin/env python3
"""Regenerate the offline prefetch assets under environment/assets/prefetch/.

The agent phase has no network — the only reachable host is the model's
inference endpoint — so every crate the candidate's `cargo build --bin uv`
can legitimately ask for must already be in the image's CARGO_HOME.

What the candidate can legitimately ask for is fixed by the documents:
`WORKSPACE_HANDOFF.md` ("No dependency outside the lockfiles the checkpoint
prewarms can be added") and `TEST_INTERFACE.md` ("a CARGO_HOME pre-populated
with every crate the lockfiles the checkpoint prewarms name — the releases
this step spans, together with the starter tree the workspace was seeded
from").  Those lockfiles are exactly the ones each step's verifier builds its
registry from, so this script reads them from `steps/NN/tests/skeleton/*/`:
the same set the checkpoint holds the candidate to.

For every step it writes one stand-in crate *per lockfile that step spans*
under `prefetch/NN/<tag>/`:

    prefetch/NN/<tag>/Cargo.toml   one exact-pinned entry per external package
                                   the lockfile names -- registry crates by
                                   `=version`, git crates by `rev` -- and
                                   nothing else (no path members: the
                                   workspace's own crates are the task, and are
                                   not in the prefetch)
    prefetch/NN/<tag>/Cargo.lock   the same packages as a lock, so `cargo fetch`
                                   keeps the locked versions (including yanked
                                   ones) instead of re-resolving
    prefetch/NN/<tag>/src/lib.rs   the stub source of the stand-in package

One stand-in per *lockfile*, not one per step, because a lockfile is a
resolution cargo accepted and a union of several is not: two of the ladder's
lockfiles can name two versions of one crate that cargo's semver rules require
it to unify (`anstream 0.6.11` in the starter tree, `0.6.13` a release later),
and a single stand-in listing both cannot resolve. Within one lockfile no such
pair exists -- cargo would already have unified it -- so exact pins are always
satisfiable per directory, and the union still accumulates in the shared
CARGO_HOME because cargo fetches into a cache, not a graph.

`cargo fetch` in each directory downloads into the shared CARGO_HOME; the
union accumulates.  The Dockerfile deletes the tree afterwards, so what ships
in the image is the registry cache and nothing else.

Usage:  python3 environment/assets/prefetch/generate.py [--check]
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

# Cargo.lock entries are parsed by hand: a lock is line-oriented enough that a
# regex over `[[package]]` blocks is stable, and it keeps the script dependency
# free (it runs when the prefetch assets are generated, not at image-build time).
ENTRY_RE = re.compile(r"^\[\[package\]\]\s*$", re.M)
NAME_RE = re.compile(r'^name = "([^"]+)"$', re.M)
VERSION_RE = re.compile(r'^version = "([^"]+)"$', re.M)
SOURCE_RE = re.compile(r'^source = "([^"]+)"$', re.M)
CHECKSUM_RE = re.compile(r'^checksum = "([^"]+)"$', re.M)
DEPS_RE = re.compile(r"^dependencies = \[(.*?)^\]", re.M | re.S)
DEP_ITEM_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


class Package:
    """One external package, identified by (name, version, source)."""

    __slots__ = ("name", "version", "source", "checksum", "deps")

    def __init__(self, name, version, source, checksum=None, deps=()):
        self.name = name
        self.version = version
        self.source = source
        self.checksum = checksum
        self.deps = list(deps)

    @property
    def key(self):
        return (self.name, self.version, self.source)

    def dep_strings(self):
        """`dependencies = [...]` entries, normalised.

        Cargo writes a bare name when only one version of it is in the graph
        and `name version` otherwise.  The union below makes many names
        ambiguous, so the version is always written; cargo accepts the
        explicit form.
        """
        return [f"{n} {v}" for n, v in self.deps]


def parse_lock(path):
    """Return one Cargo.lock as (external packages, path members).

    External packages are the registry and git entries: what the registry has
    to hold.  Path members (no `source` line) are the workspace's own crates —
    the thing the task asks the candidate to write — returned separately
    because the *names* still matter here: where a workspace crate is also a
    registry crate's dependency, the workspace patches the registry copy away,
    and a stand-in has to reproduce that or it fetches the very crate the task
    is about.
    """
    text = open(path, encoding="utf-8").read()
    blocks = ENTRY_RE.split(text)[1:]
    packages = []
    path_members = {}
    for block in blocks:
        name = NAME_RE.search(block).group(1)
        version = VERSION_RE.search(block).group(1)
        source = SOURCE_RE.search(block)
        if source is None:
            path_members[name] = version
            continue
        source = source.group(1)
        if not (source.startswith("registry+") or source.startswith("git+")):
            continue
        checksum = CHECKSUM_RE.search(block)
        deps = []
        dep_block = DEPS_RE.search(block)
        if dep_block:
            for item in DEP_ITEM_RE.findall(dep_block.group(1)):
                parts = item.split()
                deps.append((parts[0], parts[1] if len(parts) > 1 else None))
        packages.append(
            Package(
                name,
                version,
                source,
                checksum.group(1) if checksum else None,
                deps,
            )
        )
    return packages, path_members


def patched_crates(packages, path_members):
    """Path members the stand-in must patch into crates.io, and at what version.

    A crate qualifies when some package in the lock depends on it by name, and
    the lock resolves it to a path member rather than a registry entry — the
    shape `[patch.crates-io]` produces in the workspace's own manifest.  The
    stub takes the path member's version so the substitution satisfies exactly
    the requirement the workspace's patch satisfies.  Where a lock holds both a
    registry and a path copy of one name (the starter tree's `pep440_rs 0.3.12`
    beside `pep440_rs 0.4.0`), nothing is patched: the registry copy is what the
    build really links against.
    """
    registry_names = {pkg.name for pkg in packages if pkg.source.startswith("registry+")}
    required = {name for pkg in packages for name, _version in pkg.deps}
    return {
        name: path_members[name]
        for name in sorted(required & set(path_members))
        if name not in registry_names
    }


def step_lockfiles(step_dir):
    skeleton = os.path.join(step_dir, "tests", "skeleton")
    if not os.path.isdir(skeleton):
        return []
    locks = []
    for tag in sorted(os.listdir(skeleton)):
        lock = os.path.join(skeleton, tag, "Cargo.lock")
        if os.path.exists(lock):
            locks.append((tag, lock))
    return locks


def git_spec(source):
    """`git+URL?rev=X#sha` -> (URL, {"rev": X}) for a Cargo.toml dependency."""
    body = source[len("git+") :]
    url, _, _fragment = body.partition("#")
    url, _, query = url.partition("?")
    spec = {}
    for part in query.split("&"):
        if not part:
            continue
        key, _, value = part.partition("=")
        if key in ("rev", "branch", "tag"):
            spec[key] = value
    if "rev" not in spec and "branch" not in spec and "tag" not in spec:
        spec["rev"] = body.partition("#")[2]
    return url, spec


def merge(packages):
    """Union packages by (name, version, source), unioning their dep edges."""
    merged = OrderedDict()
    for pkg in packages:
        existing = merged.get(pkg.key)
        if existing is None:
            merged[pkg.key] = Package(
                pkg.name, pkg.version, pkg.source, pkg.checksum, list(pkg.deps)
            )
            continue
        seen = set(existing.deps)
        for dep in pkg.deps:
            if dep not in seen:
                existing.deps.append(dep)
                seen.add(dep)
        if existing.checksum is None:
            existing.checksum = pkg.checksum
    return list(merged.values())


def dep_name_map(packages):
    """Resolve `(name, version|None)` dep edges to the package they name."""
    by_name = {}
    for pkg in packages:
        by_name.setdefault(pkg.name, []).append(pkg)
    resolved = {}
    for pkg in packages:
        edges = []
        for name, version in pkg.deps:
            candidates = by_name.get(name, [])
            if version is not None:
                match = [p for p in candidates if p.version == version]
            else:
                match = candidates
            if len(match) == 1:
                edges.append(match[0].key)
            elif len(match) > 1:
                # Ambiguous and the source lock did not disambiguate: bind to
                # every candidate.  A superset of edges only widens the fetch.
                edges.extend(sorted(p.key for p in match))
        resolved[pkg.key] = sorted(set(edges))
    return resolved


def cargo_idents(packages):
    """Assign every package a unique Cargo.toml key.

    The key is the package's own name where that is unambiguous, and
    `name_version` where several versions are in the set.  The same version can
    still arrive from two different git URLs (`pubgrub 0.2.1` is in the union
    twice, from `zanieb/pubgrub` and `astral-sh/pubgrub`), so a collision
    counter is the last resort; Cargo.toml keys are just labels, and each entry
    still names its real package through `package = "..."`.
    """
    by_name = {}
    for pkg in packages:
        by_name[pkg.name] = by_name.get(pkg.name, 0) + 1

    idents = {}
    used = {}
    for pkg in sorted(packages, key=lambda p: (p.name, p.version, p.source)):
        base = re.sub(r"[^A-Za-z0-9_]", "_", pkg.name)
        if by_name[pkg.name] > 1:
            base = f"{base}_{re.sub(r'[^A-Za-z0-9_]', '_', pkg.version)}"
        ident, counter = base, 1
        while ident in used:
            counter += 1
            ident = f"{base}_{counter}"
        used[ident] = pkg.key
        idents[pkg.key] = ident
    return idents


def stand_in_name(step, tag):
    """The stand-in package's name: `prefetch-NN-tag`, cargo-name-safe.

    The directory keeps the tag verbatim (`01/0.1.0/`), but a package name may
    not contain `.`, so the name spells the tag with underscores.
    """
    return "prefetch-{}-{}".format(step, re.sub(r"[^A-Za-z0-9_-]", "_", tag))


def render(step, tag, packages, patched):
    idents = cargo_idents(packages)

    lines = [
        "# Generated by environment/assets/prefetch/generate.py -- do not edit.",
        "#",
        f"# Stand-in crate for step {step}, lockfile {tag}: every external",
        "# package that lockfile names (see the module docstring of",
        "# generate.py).  Dependencies only -- no source of the workspace's own",
        "# crates, and the only path members are the empty stubs under stubs/",
        "# that stand in for the workspace's [patch.crates-io] entries. `cargo",
        "# fetch` here downloads the set into the image's CARGO_HOME; the",
        "# directory itself is deleted afterwards.",
        "#",
        "# Every package is an exact-pinned root, so version selection is forced",
        "# rather than re-derived, and `default-features = false` keeps the",
        "# resolver from walking optional dependencies no lockfile names -- the",
        "# fetched set is then the lockfile's package set, which is also what",
        "# the checkpoint's own registry holds.",
        "",
        "[package]",
        f'name = "{stand_in_name(step, tag)}"',
        'version = "0.0.0"',
        'edition = "2021"',
        "publish = false",
        "",
        "[lib]",
        'path = "src/lib.rs"',
        "",
        "[dependencies]",
    ]
    for pkg in sorted(packages, key=lambda p: (p.name, p.version, p.source)):
        ident = idents[pkg.key]
        if pkg.source.startswith("git+"):
            url, spec = git_spec(pkg.source)
            options = ", ".join(f'{k} = "{v}"' for k, v in sorted(spec.items()))
            lines.append(
                f'{ident} = {{ package = "{pkg.name}", git = "{url}", '
                f"{options}, default-features = false }}"
            )
        else:
            lines.append(
                f'{ident} = {{ package = "{pkg.name}", version = "={pkg.version}", '
                "default-features = false }"
            )
    if patched:
        lines.append("")
        lines.append("# Mirrors the workspace's own [patch.crates-io]: the crates the")
        lines.append("# candidate writes are what the rest of the graph links against,")
        lines.append("# so the registry copies are replaced, never fetched.")
        lines.append("[patch.crates-io]")
        for name, version in sorted(patched.items()):
            lines.append(f'{name} = {{ path = "stubs/{name}" }}')
    lines.append("")
    return "\n".join(lines)


def render_stub(name, version):
    """An empty package standing in for one of the workspace's own crates.

    The feature list is the one the workspace's own crate offers (`serde`,
    `pyo3`, `rkyv`, `pubgrub`, `tracing` are the optional dependencies of
    `crates/pep440-rs` and `crates/pep508-rs`), because a dependent asks for
    them by name -- `pyproject-toml` requires `pep440_rs` with `features =
    ["serde"]`, and a stub without the feature fails to resolve, loudly, at
    image-build time.  The features carry nothing; the package is never built.
    """
    return "\n".join(
        [
            "# Generated by environment/assets/prefetch/generate.py -- do not edit.",
            "#",
            "# Empty stand-in for a crate the workspace patches into crates.io:",
            '# `[patch.crates-io] X = { path = "crates/..." }` in the',
            "# workspace's own manifest.  It exists so that the crate a lockfile",
            "# resolves to the workspace's copy is not downloaded from the",
            "# registry -- the task is to write it, not to fetch it.  No source:",
            "# the package is never built.",
            "",
            "[package]",
            f'name = "{name}"',
            f'version = "{version}"',
            'edition = "2021"',
            "publish = false",
            "",
            "[lib]",
            'path = "src/lib.rs"',
            "",
            "[features]",
            "default = []",
            "serde = []",
            "tracing = []",
            "pyo3 = []",
            "rkyv = []",
            "pubgrub = []",
            "",
        ]
    )


def render_lock(step, tag, packages, edges):
    out = [
        "# This file is @generated by environment/assets/prefetch/generate.py",
        "# It is not intended for manual editing.",
        "version = 3",
        "",
    ]

    def emit(name, version, source, checksum, deps):
        out.append("[[package]]")
        out.append(f'name = "{name}"')
        out.append(f'version = "{version}"')
        if source is not None:
            out.append(f'source = "{source}"')
        if checksum is not None:
            out.append(f'checksum = "{checksum}"')
        if deps:
            out.append("dependencies = [")
            for dep_name, dep_version in deps:
                out.append(f' "{dep_name} {dep_version}",')
            out.append("]")
        out.append("")

    stand_in_deps = sorted((pkg.name, pkg.version) for pkg in packages)
    emit(stand_in_name(step, tag), "0.0.0", None, None, stand_in_deps)

    for pkg in sorted(packages, key=lambda p: (p.name, p.version, p.source)):
        deps = []
        for key in edges[pkg.key]:
            dep = next(p for p in packages if p.key == key)
            deps.append((dep.name, dep.version))
        emit(pkg.name, pkg.version, pkg.source, pkg.checksum, deps)
    return "\n".join(out)


def generate(check_only=False):
    steps_root = os.path.join(PACKAGE, "steps")
    steps = sorted(
        d for d in os.listdir(steps_root) if os.path.isdir(os.path.join(steps_root, d))
    )
    written, problems, wanted = 0, [], set()
    for step in steps:
        step_dir = os.path.join(steps_root, step)
        locks = step_lockfiles(step_dir)
        if not locks:
            problems.append(f"{step}: no tests/skeleton/*/Cargo.lock")
            continue
        count = 0
        for tag, lock in locks:
            parsed, path_members = parse_lock(lock)
            packages = merge(parsed)
            patched = patched_crates(packages, path_members)
            edges = dep_name_map(packages)
            target = os.path.join(HERE, step[:2], tag)
            wanted.add(os.path.relpath(target, HERE))
            toolchain = open(
                os.path.join(os.path.dirname(lock), "rust-toolchain.toml"),
                encoding="utf-8",
            ).read()
            files = {
                "Cargo.toml": render(step[:2], tag, packages, patched),
                "Cargo.lock": render_lock(step[:2], tag, packages, edges),
                "rust-toolchain.toml": toolchain,
                "src/lib.rs": "// Stand-in package: no code, just the dependency set.\n",
            }
            for name, version in patched.items():
                stub = os.path.join(target, "stubs", name)
                wanted.add(os.path.relpath(stub, HERE))
                files[os.path.join("stubs", name, "Cargo.toml")] = render_stub(
                    name, version
                )
                files[os.path.join("stubs", name, "src", "lib.rs")] = (
                    "// Empty stand-in for a patched workspace crate: never built.\n"
                )
            for rel, content in files.items():
                path = os.path.join(target, rel)
                if check_only:
                    current = (
                        open(path, encoding="utf-8").read()
                        if os.path.exists(path)
                        else None
                    )
                    if current != content:
                        problems.append(f"{path}: stale")
                    continue
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write(content)
                written += 1
            count += len(packages)
        print(f"{step[:2]}: {len(locks)} stand-ins, {count} package entries")
    # Anything under the tree that no step asked for is a leftover from an
    # earlier shape of this script and would be fetched by the Dockerfile loop.
    stale = []
    for root, _dirs, files in os.walk(HERE):
        if "Cargo.toml" not in files:
            continue
        rel = os.path.relpath(root, HERE)
        if rel not in wanted:
            stale.append(rel)
    for rel in stale:
        problems.append(f"{rel}: not claimed by any step")
    if check_only:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1 if problems else 0
    print(f"wrote {written} files under {HERE}")
    for problem in problems:
        print(problem, file=sys.stderr)
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="verify the assets are up to date"
    )
    sys.exit(generate(check_only=parser.parse_args().check))
