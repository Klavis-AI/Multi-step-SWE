#!/usr/bin/env python3
"""Generate environment/assets/prefetch/NN/ -- the dependency-graph stand-ins
that the agent image primes its Cargo registry cache from, and then vendors.

One stand-in per step.  Each is a single package whose dependency declarations
are the union of that release's workspace members' declarations: registry
dependencies only, at the versions the step's own Cargo.lock resolves to, with
the dependency features the members' `[features]` tables enable.  The task's own
crates (the workspace members, `path = ...`) are not in it, and neither is any
implementation of anything -- `cargo fetch` only needs the manifests, the
lockfile and an empty `src/`.

The release's own Cargo.lock is seeded into each directory before the fetch, so
cargo keeps its locked versions (including releases that have since been yanked
from crates.io) instead of re-resolving to whatever the registry serves today.
Each generated lockfile is cargo's own output for that manifest, and every one
of them resolves to exactly the release lockfile's registry package set, as
checked by verify-prefetch.py.

Usage:
    gen-prefetch.py [--task DIR] [--out DIR] [--fetch]

    --task DIR   task package root (default: the package this file lives in)
    --out DIR    where to write the NN/ trees (default: <task>/environment/assets/prefetch)
    --fetch      also run `cargo fetch` in each directory, which canonicalizes
                 the lockfile the stand-in ships with (needs network, or a
                 populated cache; the image build does this instead, on the
                 pinned toolchain).  Without it each directory keeps the
                 release's lockfile as seeded, and the next `cargo fetch
                 --locked` rewrites it.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tomllib

DEP_SECTIONS = ("dependencies", "dev-dependencies", "build-dependencies")


def release_tree(task, step_dir):
    """The release's own manifests and lockfile for one step."""
    wd = os.path.join(task, "steps", step_dir, "workdir")
    cp = os.path.join(task, "steps", step_dir, "tests/assets/prefetch/checkpoint")
    if os.path.exists(os.path.join(wd, "Cargo.lock")):
        # Step 01 ships the workspace the agent is handed: use it directly.
        return wd, "the workspace the agent is handed (steps/%s/workdir)" % step_dir
    return cp, "the release checkpoint tree (steps/%s/tests/assets/prefetch/checkpoint)" % step_dir


def merge_spec(dst, spec, where):
    """Merge one member's dependency declaration into a union entry."""
    if isinstance(spec, str):
        spec = {"version": spec}
    if "path" in spec:
        return None                     # a workspace member: not a registry dependency
    if "version" not in spec:
        raise SystemExit("%s: dependency with neither version nor path: %r" % (where, spec))
    if dst is None:
        dst = {"version": set(), "features": set(), "default_features": False}
    dst["version"].add(spec["version"])
    dst["features"] |= set(spec.get("features", []))
    dst["default_features"] = dst["default_features"] or spec.get("default-features", True)
    return dst


def union_manifest(task, step_dir, source):
    tree, _ = release_tree(task, step_dir)
    root = tomllib.load(open(os.path.join(tree, "Cargo.toml"), "rb"))
    members = root["workspace"]["members"]
    member_names = {os.path.basename(m) for m in members}
    sections = {}                       # section name -> {package name: merged entry}
    enables = {}                        # package name -> extra features the members enable

    for m in members:
        mt = tomllib.load(open(os.path.join(tree, m, "Cargo.toml"), "rb"))
        keys = {}                       # this member's dependency key -> package name
        for kind in DEP_SECTIONS:
            for key, spec in (mt.get(kind) or {}).items():
                keys[key] = spec.get("package", key) if isinstance(spec, dict) else key
        for tab in (mt.get("target") or {}).values():
            for kind in DEP_SECTIONS:
                for key, spec in (tab.get(kind) or {}).items():
                    keys[key] = spec.get("package", key) if isinstance(spec, dict) else key
        # A member's [features] table enables *dependency* features, e.g.
        # tor-dirclient's `zstd = ["async-compression/zstd"]`.  One stand-in
        # package cannot reproduce per-member feature activation, so it enables
        # the union of every feature any member names for a registry
        # dependency: a superset of what any single member asks for, never a
        # subset.  Enables pointing at a member are that member's own table.
        for items in (mt.get("features") or {}).values():
            for it in items:
                if "/" not in it:
                    continue
                depkey, feat = it.split("/", 1)
                pkg = keys.get(depkey, depkey)
                if pkg not in member_names:
                    enables.setdefault(pkg, set()).add(feat)
        for kind in DEP_SECTIONS:
            for key, spec in (mt.get(kind) or {}).items():
                real = spec.get("package", key) if isinstance(spec, dict) else key
                if real in member_names:
                    continue
                merged = merge_spec(sections.setdefault(kind, {}).get(real),
                                    spec, "%s:%s:%s" % (m, kind, key))
                if merged is not None:
                    sections[kind][real] = merged
        for tgt, tab in (mt.get("target") or {}).items():
            for kind in DEP_SECTIONS:
                for key, spec in (tab.get(kind) or {}).items():
                    real = spec.get("package", key) if isinstance(spec, dict) else key
                    if real in member_names:
                        continue
                    name = "target.%s.%s" % (tgt, kind)
                    merged = merge_spec(sections.setdefault(name, {}).get(real),
                                        spec, "%s:%s:%s" % (m, name, key))
                    if merged is not None:
                        sections[name][real] = merged

    for table in sections.values():
        for pkg, entry in table.items():
            entry["features"] |= enables.get(pkg, set())

    out = [
        "# Generated by environment/assets/gen-prefetch.py from this release's own",
        "# manifests -- %s." % source,
        "#",
        "# Not the task's code: a dependency-graph stand-in whose only purpose is to",
        "# make `cargo fetch` populate the registry cache in the image being built, so",
        "# a build there needs no mirror.  It carries no implementation of anything,",
        "# and the task's own crates are not in it.  Do not edit.",
        "",
        "[package]",
        'name = "prefetch-stand-in"',
        'version = "0.0.0"',
        'edition = "2021"',
        "publish = false",
        "",
        "# Standalone: must not be absorbed into any surrounding workspace.",
        "[workspace]",
        "",
    ]
    for name in sorted(sections):
        if not sections[name]:
            continue
        if name.startswith("target."):
            tgt, kind = name[len("target."):].rsplit(".", 1)
            out.append("[target.'%s'.%s]" % (tgt, kind))
        else:
            out.append("[%s]" % name)
        seen = set()
        for pkg in sorted(sections[name]):
            e = sections[name][pkg]
            req = max(sorted(e["version"]), key=len)
            key = re.sub(r"[^A-Za-z0-9_]", "_", pkg)
            while key in seen:
                key += "_"
            seen.add(key)
            parts = ['package = "%s"' % pkg, 'version = "%s"' % req]
            if e["features"]:
                parts.append("features = [%s]" % ", ".join('"%s"' % f for f in sorted(e["features"])))
            if not e["default_features"]:
                parts.append("default-features = false")
            out.append("%s = { %s }" % (key, ", ".join(parts)))
        out.append("")
    return "\n".join(out), tree


def main():
    here = os.path.dirname(os.path.abspath(__file__))               # environment/assets
    task_default = os.path.dirname(os.path.dirname(here))           # package root
    ap = argparse.ArgumentParser()
    ap.add_argument("--task", default=task_default)
    ap.add_argument("--out", default=None)
    ap.add_argument("--fetch", action="store_true")
    args = ap.parse_args()
    task = os.path.abspath(args.task)
    out_root = args.out or os.path.join(task, "environment", "assets", "prefetch")

    steps = sorted(d for d in os.listdir(os.path.join(task, "steps")) if re.match(r"^\d\d-", d))
    for step_dir in steps:
        step = step_dir[:2]
        tree, source = release_tree(task, step_dir)
        manifest, _ = union_manifest(task, step_dir, source)
        d = os.path.join(out_root, step)
        os.makedirs(os.path.join(d, "src"), exist_ok=True)
        with open(os.path.join(d, "Cargo.toml"), "w") as f:
            f.write(manifest)
        with open(os.path.join(d, "src", "lib.rs"), "w") as f:
            f.write("// Intentionally empty.  See Cargo.toml.\n")
        # Seed the release's own lockfile so cargo keeps its locked versions.
        shutil.copyfile(os.path.join(tree, "Cargo.lock"), os.path.join(d, "Cargo.lock"))
        if args.fetch:
            r = subprocess.run(["cargo", "fetch"], cwd=d, capture_output=True, text=True)
            tail = (r.stdout + r.stderr).strip().splitlines()[-2:]
            print("%s: cargo fetch rc=%d %s" % (step, r.returncode, " | ".join(tail)))
        else:
            print("%s: wrote %s" % (step, d))


if __name__ == "__main__":
    sys.exit(main())
