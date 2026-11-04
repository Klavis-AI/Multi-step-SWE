#!/usr/bin/env python3
"""Generate environment/assets/prefetch/NN from each step's frozen Cargo.lock.

Why this exists
---------------
The agent phase of this task runs with no network: the only reachable host is
the model's inference endpoint (`task.toml` -> `[agent] network_mode =
"allowlist"`).  A cargo build that has to resolve a dependency therefore cannot
reach crates.io, and every step's first build would fail on a crate the step
really compiles.  The fix is to populate the registry cache in the image being
built, exactly as the verifier images already do for their own offline build.

What is written, per step
-------------------------
Two locks describe a step's dependency set, and they are the same file for
twelve of the fifteen steps:

  * `steps/NN/workdir/Cargo.lock` -- the lock shipped to the agent in the
    workdir.  This is the set the step's own `TEST_INTERFACE.md` promises is
    reachable offline ("the registry cache holds every crate in the Cargo.lock
    that ships in this workdir").
  * `steps/NN/tests/assets/prewarm/candidate/Cargo.lock` -- the graph the
    milestone's own manifests resolve to, i.e. what the verifier prewarms and
    what the finished workspace needs.

For every step this writes a stand-in crate -- a manifest listing that step's
whole dependency set with exact (=) version pins, a stub `src/`, and a
`Cargo.lock` that is the step's frozen lock with its workspace members replaced
by the stand-in package.  `cargo fetch --locked` in that directory downloads
exactly the crates the step's own build will need, and because the lock is
shipped, nothing is re-resolved and yanked releases the step really compiles
stay reachable.

The lock this script writes is a seed, not the final file.  Naming every crate
as a direct dependency with default features makes cargo's feature unification
reach optional dependencies the original graph did not (clap's `ansi_term` and
`atty`, plotters' image backends), so `cargo fetch --locked` refuses the seed
until cargo has resolved it once.  `environment/bin/freeze-locks.sh` does that
with network at authoring time and rewrites the locks in place; the checked-in
locks are its output, and `environment/Dockerfile` then runs `cargo fetch
--locked` against them, so a lock that drifts is a loud failure rather than a
silent re-resolution.  Re-running this script reverts the locks to seeds, so
run `freeze-locks.sh` again afterwards.

Where the two locks differ (steps 07, 09 and 10 ship the previous release's
lock over the target release's tree), the target graph is written to a second
directory, `NN-target/`.  Both directories share one CARGO_HOME in the image,
so what lands in the cache is their union -- 570 distinct crate versions plus
one rev-pinned git checkout.

What one milestone may resolve
------------------------------
The union in `CARGO_HOME` is what the *image* can resolve; what any one
*milestone* may resolve is a subset of it, and that subset is what the
verifier's own image carries.  Two more graphs complete it, and both are
written here so that the agent image can assemble the same vendor directory
the verifier does (`environment/bin/make-vendor.py`):

  * `NN-prev/` -- the graph the PREVIOUS release's own manifests resolve to.
    A candidate who has not done the work yet presents those manifests against
    this milestone's lock, so the verifier prewarms them (`tests/prewarm-prev`)
    and the agent gets the same set here.  Steps 01 and 08 have no previous
    release and no such directory.
  * `NN-harness/` -- the verifier harness crate's own dependency graph.

Both are COPIES of the verifier's own prewarm directories, not re-rendered
stand-ins: the verifier fetches those trees itself, so the input is the same
file on both sides, and a stand-in would not be -- it names every crate in the
lock as a direct dependency with default features.

The prev locks are frozen.  A fresh `cargo fetch` in `tests/prewarm-prev`
resolves to whatever crates.io serves that day, and for two milestones it no
longer resolves at all -- every `boring ^2.0.0` release is yanked -- so the
resolution is taken once, checked in, and fetched `--locked` in both images.
That also pins the agent's cache and the verifier's to the same versions.

Git dependencies are part of that set, not an exception to it.  `qlog-dancer`,
a crate this task asks the agent to write from step 14 on, depends on
`wirefilter-engine` pinned to a git revision; with no network at agent time the
checkout has to be in `$CARGO_HOME/git` already or the step cannot build.

What it deliberately does NOT write
-----------------------------------
Only registry and git packages are listed.  The workspace members -- `quiche`,
`qlog`, `octets`, `h3i`, `tokio-quiche` and the rest of the crates the task
asks the agent to write -- are path packages with no source, so they are
dropped rather than fetched: baking them would be baking the answer, and the
instruction declares depending on them out of bounds.  A name that is both a
workspace member and a registry dependency is refused outright.  No QUIC or
HTTP/3 implementation is in any lock, so none can be baked by accident.

Usage
-----
    environment/bin/gen-prefetch.py [TASK_DIR]

TASK_DIR defaults to this file's package (the directory containing steps/).
The script is offline: it only reads the frozen locks and writes assets.
"""
import os
import re
import shutil
import sys

try:
    import tomllib
except ImportError:  # python3 < 3.11
    import tomli as tomllib  # type: ignore


def read_lock(path):
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def is_registry(pkg):
    return pkg.get("source", "").startswith("registry+")


def is_git(pkg):
    return pkg.get("source", "").startswith("git+")


def is_bakeable(pkg):
    return is_registry(pkg) or is_git(pkg)


def bakeable_packages(lock):
    """[(name, version)] for every non-local package, in lock order."""
    return [(p["name"], p["version"]) for p in lock["package"] if is_bakeable(p)]


def local_packages(lock):
    """Workspace members: path packages with no source at all."""
    return sorted({p["name"] for p in lock["package"] if not p.get("source", "")})


def parse_git_source(source):
    """(url, 'rev'|'tag'|'branch', value) from a lock's git source spelling.

    Locks write `git+<url>?<query>#<sha>`, where the query names the revision
    the release pinned.  This task's locks use `rev`; the other two forms are
    accepted so a future lock does not silently fall through to a different
    checkout than the one it pins.
    """
    body = source[len("git+"):]
    url, _, fragment = body.partition("#")
    url, _, query = url.partition("?")
    for key in ("rev", "tag", "branch"):
        for part in query.split("&"):
            name, _, value = part.partition("=")
            if name == key and value:
                return url, key, value
    if fragment:
        return url, "rev", fragment
    raise SystemExit(f"git source with no revision to pin: {source}")


def alias(name, version):
    """A crate-name-safe alias, unique per (name, version)."""
    return re.sub(r"[^0-9A-Za-z_]", "_", f"{name}_{version}")


def render_toml(step, source, lock):
    registry = [p for p in lock["package"] if is_registry(p)]
    git = [p for p in lock["package"] if is_git(p)]
    lines = [
        f"# Generated by environment/bin/gen-prefetch.py from step {step}'s",
        f"# {source}.",
        "# Not the task's code: a dependency-graph stand-in whose only purpose",
        "# is to make `cargo fetch` populate the registry cache -- and the git",
        "# checkout cache -- in the image being built, so a build there needs",
        "# no mirror. Do not edit.",
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
        "[dependencies]",
    ]
    for pkg in sorted(registry, key=lambda p: (p["name"], p["version"])):
        name, version = pkg["name"], pkg["version"]
        lines.append(
            f'{alias(name, version)} = {{ package = "{name}", version = "={version}" }}'
        )
    for pkg in sorted(git, key=lambda p: (p["name"], p["version"])):
        name, version = pkg["name"], pkg["version"]
        url, key, value = parse_git_source(pkg["source"])
        lines.append(
            f'{alias(name, version)} = {{ package = "{name}", '
            f'git = "{url}", {key} = "{value}" }}'
        )
    lines.append("")
    return "\n".join(lines)


def render_lock(step, source, lock):
    """The frozen lock, its workspace members replaced by the stand-in root.

    Every dependency edge is resolved against the kept (non-local) packages by
    name and, when the lock spells it, version, so nothing dangles after the
    members come out.  Sources and checksums are carried over verbatim:
    `cargo fetch --locked` then fetches the exact bytes the release was built
    against, yanked releases included.
    """
    kept = bakeable_packages(lock)
    kept_set = set(kept)
    versions = {}
    for name, version in kept:
        versions.setdefault(name, set()).add(version)

    def keep_edge(edge):
        parts = edge.split(" ")
        if len(parts) == 2:
            return (parts[0], parts[1]) in kept_set
        return len(versions.get(parts[0], ())) == 1 and (parts[0], next(
            iter(versions[parts[0]]))) in kept_set

    out = [
        "# Generated by environment/bin/gen-prefetch.py. Do not edit.",
        f"# Step {step}'s frozen {source}, with its workspace members replaced",
        "# by the stand-in package, so `cargo fetch --locked` resolves nothing",
        "# and yanked releases the step really compiles stay reachable.",
        "",
        f"version = {lock.get('version', 4)}",
        "",
        "[[package]]",
        'name = "prefetch-stand-in"',
        'version = "0.0.0"',
        "dependencies = [",
    ]
    for name, version in sorted(kept):
        out.append(f' "{name} {version}",')
    out.append("]")
    for pkg in lock["package"]:
        if not is_bakeable(pkg):
            continue
        out += [
            "",
            "[[package]]",
            f'name = "{pkg["name"]}"',
            f'version = "{pkg["version"]}"',
            f'source = "{pkg["source"]}"',
        ]
        if "checksum" in pkg:
            out.append(f'checksum = "{pkg["checksum"]}"')
        deps = sorted(e for e in pkg.get("dependencies", []) if keep_edge(e))
        if deps:
            out.append("dependencies = [")
            for edge in deps:
                out.append(f' "{edge}",')
            out.append("]")
    out.append("")
    return "\n".join(out)


def write_stand_in(outroot, dirname, step, source, lock):
    # No baked package may be a crate the task asks the agent to write: that
    # would put the answer in the cache rather than its dependencies.
    clash = sorted({n for n, _ in bakeable_packages(lock)} & set(local_packages(lock)))
    if clash:
        sys.exit(f"{step}: {clash} is both a workspace member and a fetched "
                 f"dependency -- refusing to generate")

    stepdir = os.path.join(outroot, dirname)
    os.makedirs(os.path.join(stepdir, "src"), exist_ok=True)
    with open(os.path.join(stepdir, "Cargo.toml"), "w") as fh:
        fh.write(render_toml(step, source, lock))
    with open(os.path.join(stepdir, "src", "lib.rs"), "w") as fh:
        fh.write("// Intentionally empty. See Cargo.toml.\n")
    with open(os.path.join(stepdir, "Cargo.lock"), "w") as fh:
        fh.write(render_lock(step, source, lock))
    return bakeable_packages(lock), local_packages(lock)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default = os.path.dirname(os.path.dirname(here))
    task = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else default)
    steps = sorted(
        d for d in os.listdir(os.path.join(task, "steps"))
        if os.path.isdir(os.path.join(task, "steps", d))
    )
    if not steps:
        sys.exit(f"no steps under {task}")

    outroot = os.path.join(task, "environment", "assets", "prefetch")
    union, total, dirs = set(), 0, 0
    for step in steps:
        number = step.split("-", 1)[0]
        workdir_lock = os.path.join(task, "steps", step, "workdir", "Cargo.lock")
        target_lock = os.path.join(
            task, "steps", step, "tests", "assets", "prewarm", "candidate",
            "Cargo.lock")
        if not os.path.exists(workdir_lock) or not os.path.exists(target_lock):
            sys.exit(f"{step}: missing workdir/Cargo.lock or "
                     f"tests/assets/prewarm/candidate/Cargo.lock")

        prev_dir = os.path.join(task, "steps", step, "tests", "prewarm-prev")
        harness_lock = os.path.join(
            task, "steps", step, "tests", "assets", "prewarm", "harness",
            "Cargo.lock")
        ship = read_lock(workdir_lock)
        target = read_lock(target_lock)
        ship_pkgs, members = write_stand_in(
            outroot, number, number, "workdir/Cargo.lock", ship)
        dirs += 1
        union |= set(ship_pkgs)
        total += len(ship_pkgs)
        line = (f"  {number}: {len(ship_pkgs):3d} fetched crates from the "
                f"shipped lock ({len(members)} members dropped)")

        # The target graph is a second, different lock for three steps: they
        # seed the target release's lock over the previous release's tree, so
        # the shipped lock is the *previous* release's and the crates the
        # finished workspace compiles are not all in it.
        if set(bakeable_packages(target)) != set(ship_pkgs):
            extra, _ = write_stand_in(
                outroot, f"{number}-target", number,
                "tests/assets/prewarm/candidate/Cargo.lock", target)
            dirs += 1
            union |= set(extra)
            total += len(extra)
            line += (f"; + {len(extra):3d} from the target lock "
                     f"({len(set(extra) - set(ship_pkgs))} not in the "
                     f"shipped one)")

        # The previous release's graph: what the verifier prewarms separately,
        # because a candidate that has not done the work yet presents it. Copied
        # rather than re-rendered as a stand-in -- the verifier fetches that
        # directory itself, and a stand-in that pinned every crate in the lock
        # as a direct dependency would pull optional features the real
        # manifests never ask for and, for two milestones, conflict with a
        # dependency's own version bound. One fetch, one input, both images.
        if os.path.exists(os.path.join(prev_dir, "Cargo.lock")):
            dst = os.path.join(outroot, f"{number}-prev")
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(prev_dir, dst)
            extra = bakeable_packages(read_lock(
                os.path.join(prev_dir, "Cargo.lock")))
            dirs += 1
            union |= set(extra)
            total += len(extra)
            line += f"; + {len(extra):3d} from the previous release"

        # The harness crate's graph, so the per-milestone vendor directory can
        # be the verifier's cache exactly rather than a subset of it. Copied for
        # the same reason as the previous release's.
        if os.path.exists(harness_lock):
            dst = os.path.join(outroot, f"{number}-harness")
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(os.path.dirname(harness_lock), dst)
            extra = bakeable_packages(read_lock(harness_lock))
            dirs += 1
            union |= set(extra)
            total += len(extra)
            line += f"; + {len(extra):3d} from the harness"

        # The four *real* locks, copied beside the stand-ins, because the
        # stand-in locks are not the same set: naming every crate as a direct
        # dependency with default features makes cargo's feature unification
        # reach optional dependencies the real graph never enables (that is
        # why freeze-locks.sh exists), so a stand-in lock is a superset. The
        # per-milestone vendor directory must be built from the real ones --
        # it has to be the verifier's cache exactly, and the verifier fetches
        # these four files. The stand-in locks stay what the image *fetches*,
        # so the cache is a superset of them; nothing is missing, and nothing
        # extra is resolvable.
        locks_dir = os.path.join(outroot, f"{number}-locks")
        os.makedirs(locks_dir, exist_ok=True)
        for name, rel in (("workdir", workdir_lock),
                          ("target", target_lock),
                          ("prev", os.path.join(prev_dir, "Cargo.lock")),
                          ("harness", harness_lock)):
            if os.path.exists(rel):
                shutil.copyfile(rel, os.path.join(locks_dir, f"{name}.lock"))

        print(line)

    print(f"==> wrote {dirs} stand-ins, {total} pins, under {outroot}")
    print(f"    union: {len(union)} distinct name-version pairs")

    # Every lock in the package must be covered by what was just written, or
    # the image will fail the build it exists to make work.
    written = set()
    for d in sorted(os.listdir(outroot)):
        lock = os.path.join(outroot, d, "Cargo.lock")
        if not os.path.exists(lock):
            continue  # an NN-locks/ directory: the real locks, not a stand-in
        written |= set(bakeable_packages(read_lock(lock)))
    for step in steps:
        for rel in ("workdir/Cargo.lock",
                    "tests/assets/prewarm/candidate/Cargo.lock",
                    "tests/assets/prewarm/harness/Cargo.lock",
                    "tests/prewarm-prev/Cargo.lock",
                    "solution/Cargo.lock"):
            path = os.path.join(task, "steps", step, rel)
            if not os.path.exists(path):
                continue
            missing = set(bakeable_packages(read_lock(path))) - written
            if missing:
                sys.exit(f"{step}/{rel}: not covered by the prefetch: "
                         f"{sorted(missing)}")
    print("    coverage: every workdir, target, harness, prev and solution "
          "lock is covered")


if __name__ == "__main__":
    main()
