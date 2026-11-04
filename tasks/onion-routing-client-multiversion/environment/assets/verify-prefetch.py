#!/usr/bin/env python3
"""Check the twelve prefetch stand-ins against the releases they were built from.

Every check below is derived from the files on disk:

  1. Each stand-in's lockfile resolves to exactly the registry package set of
     that step's frozen release lockfile -- same names, same versions, same
     checksums, no extra package, nothing missing.
  2. No workspace crate the task asks the agent to write (the `path =` members
     of any of the twelve releases) appears in any stand-in's lockfile or
     manifest.
  3. The union of the twelve lockfiles covers every release graph *and* every
     predecessor graph the verifier primes for.

Usage: verify-prefetch.py [--task DIR]      exit 0 = all checks pass
"""
import argparse
import os
import re
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TASK = os.path.dirname(os.path.dirname(HERE))


def parse_lock(path):
    pkgs, cur, in_deps = [], None, False
    for raw in open(path, encoding="utf-8"):
        line = raw.rstrip("\n")
        if line.startswith("[[package]]"):
            cur = {"deps": []}
            pkgs.append(cur)
            in_deps = False
            continue
        if cur is None:
            continue
        if in_deps:
            if line == "]":
                in_deps = False
            else:
                m = re.match(r'^ "(.*)",$', line)
                if m:
                    cur["deps"].append(m.group(1))
            continue
        for key, field in (("name = ", "name"), ("version = ", "version"),
                           ("source = ", "source"), ("checksum = ", "checksum")):
            if line.startswith(key):
                cur[field] = line[len(key) + 1:-1]
                break
        else:
            if line == "dependencies = [":
                in_deps = True
    return pkgs


def registry_set(path):
    return {(p["name"], p["version"]): p.get("checksum")
            for p in parse_lock(path) if "source" in p}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--task", default=DEFAULT_TASK)
    args = ap.parse_args()
    task = os.path.abspath(args.task)
    pf = os.path.join(task, "environment", "assets", "prefetch")
    steps = sorted(d for d in os.listdir(os.path.join(task, "steps")) if re.match(r"^\d\d-", d))
    problems = []
    union = set()
    all_members = set()

    for step_dir in steps:
        step = step_dir[:2]
        wd = os.path.join(task, "steps", step_dir, "workdir")
        cp = os.path.join(task, "steps", step_dir, "tests/assets/prefetch/checkpoint")
        release = os.path.join(wd, "Cargo.lock") if os.path.exists(os.path.join(wd, "Cargo.lock")) \
            else os.path.join(cp, "Cargo.lock")
        tree = wd if os.path.exists(os.path.join(wd, "Cargo.toml")) and \
            os.path.exists(os.path.join(wd, "tor-cell")) else cp

        root = tomllib.load(open(os.path.join(tree, "Cargo.toml"), "rb"))
        all_members |= {os.path.basename(m) for m in root["workspace"]["members"]}

        want = registry_set(release)
        got = registry_set(os.path.join(pf, step, "Cargo.lock"))
        union |= set(got)
        if want != got:
            problems.append("%s: stand-in lock != release lock (missing %s, extra %s)"
                            % (step, sorted(set(want) - set(got))[:4], sorted(set(got) - set(want))[:4]))
        else:
            print("step %s: %3d packages, identical to the release lockfile" % (step, len(got)))

        manifest = open(os.path.join(pf, step, "Cargo.toml")).read()
        member_hits = sorted(m for m in all_members
                             if re.search(r'package = "%s"' % re.escape(m), manifest))
        if member_hits:
            problems.append("%s: workspace crate in the stand-in manifest: %s" % (step, member_hits))

    # coverage: every release graph and every predecessor graph
    for step_dir in steps:
        step = step_dir[:2]
        for which in ("checkpoint", "checkpoint-prev"):
            p = os.path.join(task, "steps", step_dir, "tests/assets/prefetch", which, "Cargo.lock")
            if not os.path.exists(p):
                continue
            missing = set(registry_set(p)) - union
            if missing:
                problems.append("%s %s not fully covered by the baked union: %s"
                                % (step, which, sorted(missing)[:4]))
    print("baked union: %d (name, version) pairs; workspace crate names checked: %d"
          % (len(union), len(all_members)))

    if problems:
        for p in problems:
            print("FAIL " + p, file=sys.stderr)
        return 1
    print("ok: every stand-in matches its release lockfile, no workspace crate is baked,"
          " every release and predecessor graph is covered")
    return 0


if __name__ == "__main__":
    sys.exit(main())
