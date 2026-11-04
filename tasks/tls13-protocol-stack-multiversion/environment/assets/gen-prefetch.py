#!/usr/bin/env python3
"""Regenerate the per-step prefetch stand-ins and keep the published `rustls`
crate out of the agent image's Cargo registry cache.

WHAT THE STAND-INS ARE
    `environment/assets/prefetch/NN/` is a single-package stand-in whose
    dependency declarations are the registry entries of step NN's frozen
    Cargo.lock, so that `cargo fetch` at image build time populates
    CARGO_HOME/registry with exactly that step's graph. The thirteen
    agent-image stand-ins share one CARGO_HOME, so the agent's cache is their
    union. `steps/NN-*/tests/assets/prefetch/` is the verifier image's copy
    (its build context is only that step's tests/ tree). The two copies agree
    at 10-13 and differ at 06-09 -- see VERIFIER_LEAKFREE below for why.

WHY `rustls` IS STUBBED
    A stand-in that depended on the published `rustls` crate would put the
    reference implementation of a milestone -- at steps 12/13 the *exact*
    target release -- into the agent's cache, one `tar xzf` away.

    For the steps whose documented build lines never compile the published
    crate (every step except 10 and 11), the direct pin is dropped and the
    dependents' `rustls` edges are pointed at a local stub through
    `[patch.crates-io]` -- the same mechanism the release's own tree uses from
    0.23.31 on (`[patch.crates-io] rustls = { path = "rustls" }`). The stub is
    never fetched and never built: `cargo fetch` only needs the manifest to
    resolve the edge. The published .crate does not enter the cache, so it
    cannot be read there.

    The agent-side rule is applied to every step the build lines allow; the
    verifier-side copy follows VERIFIER_LEAKFREE below, which explains why
    06-09 differ between the two sides.

    Steps 10 and 11 keep the pin: their frozen locks make the *release's own*
    `examples` crate depend on `hickory-resolver`, which depends on a published
    `rustls ^0.23`, and the documented build line builds that crate
    (`-p rustls-examples --bins`). Nothing in the release tree patches that edge
    to the workspace member, so the .crate must be in the cache for the
    candidate to build the documented line offline.

USAGE
    gen-prefetch.py [--pkg DIR] [--check]

    --pkg DIR   package root (default: the package this file lives in)
    --check     write nothing; fail if anything on disk differs
"""

import argparse
import os
import re
import shutil
import sys

# Steps whose AGENT-side stand-in drops the published crate. Steps 01-05 have
# no registry rustls in their locks; steps 10-11 are pinned deliberately (see
# the module docstring).
STANDIN_STEPS = ["06", "07", "08", "09", "12", "13"]

# Of those, the steps whose VERIFIER-side copy is leak-free too. The two sides
# are not the same job:
#
#   * the agent image reads every stand-in it ships, shares one CARGO_HOME
#     across all thirteen, and must never hold a readable copy of the
#     implementation under test;
#   * the verifier image is built per step from that step's tests/ tree, is
#     never visible to the agent, and resolves the *release's* lock offline --
#     its cache deliberately mirrors the step's frozen graph, including the
#     registry `rustls` that the release's own lock names at 06-11.
#
# At 12 and 13 the release's own tree carries [patch.crates-io] rustls = path,
# so the release-faithful form IS the stub there and both sides get it. At
# 06-09 the release has no patch and its lock names the registry crate, so the
# verifier's copy keeps it (its `cargo build --locked` resolves the whole
# workspace offline, and a lock entry that is not in the compile graph is still
# resolved) while the agent's does not.
VERIFIER_LEAKFREE = ["12", "13"]

# Steps whose verifier copy must still pin the published crate. Asserted, not
# written -- see generate(): this script never rewrites those copies.
PINNED_STEPS = ["10", "11"]

# Per step: the stub's version and the feature names it must declare.
#
# Version: for 06-09, the version the step's lock itself resolved the registry
# crate to (0.21.9 / 0.21.10). For 12-13, the step's OWN release version, which
# is what the release's tree resolves every dependent to through its own
# [patch.crates-io], and the only value all dependents accept (hickory-proto
# 0.25.2 needs ^0.23.23, hickory-net 0.26.1 and hickory-resolver 0.26.1
# ^0.23.23, rustls-graviola 0.2.1 ^0.23.18, tokio-rustls 0.26.2 ^0.23.22 and
# 0.26.4 ^0.23.27).
#
# Features: the union of what each dependent of that step can request -- its
# `[dependencies.rustls]` features plus every `rustls/<f>` entry in its
# `[features]` table, read out of the pinned .crate manifests. Supersets cost
# nothing; a missing name makes `cargo fetch` fail, which fails the image build
# loudly, not silently.
#   06-09: tokio-rustls 0.24.1 (default = logging, tls12; also
#          dangerous_configuration, secret_extraction), hickory-proto 0.24.0
#          and hickory-resolver 0.24.0 (quic).
#   12:    hickory-proto 0.25.2, hickory-resolver 0.25.2 (logging, std, tls12),
#          rustls-graviola 0.2.1 (std, tls12), tokio-rustls 0.26.2
#          (aws_lc_rs, fips, logging, ring, std, tls12).
#   13:    hickory-net 0.26.1, hickory-resolver 0.26.1 (logging, std, tls12),
#          rustls-graviola 0.2.1 (std, tls12), tokio-rustls 0.26.4
#          (aws_lc_rs, brotli, fips, logging, ring, std, tls12, zlib).
STUBS = {
    "06": ("0.21.9", ["dangerous_configuration", "logging", "quic",
                      "secret_extraction", "tls12"]),
    "07": ("0.21.9", ["dangerous_configuration", "logging", "quic",
                      "secret_extraction", "tls12"]),
    "08": ("0.21.10", ["dangerous_configuration", "logging", "quic",
                       "secret_extraction", "tls12"]),
    "09": ("0.21.10", ["dangerous_configuration", "logging", "quic",
                       "secret_extraction", "tls12"]),
    "12": ("0.23.31", ["aws_lc_rs", "fips", "logging", "ring", "std", "tls12"]),
    "13": ("0.23.45", ["aws_lc_rs", "brotli", "fips", "logging", "ring", "std",
                       "tls12", "zlib"]),
}

PATCH_COMMENT = """
# The published `rustls` is deliberately NOT in this stand-in's download set.
# The workspace member of that name is the implementation under test, and the
# agent image shares one CARGO_HOME, so a .crate for it here is a readable copy
# of a milestone on this ladder. Every registry dependency of this step that
# names `rustls` resolves to the local stub instead -- the mechanism the
# release's own tree uses from 0.23.31 on ([patch.crates-io] rustls = path).
# Regenerate with environment/assets/gen-prefetch.py.
[patch.crates-io]
rustls = { path = "rustls-stub" }
"""

STUB_LIB = """// Stub for `cargo fetch` resolution only: it satisfies the `rustls` edges of
// this step's registry dependencies without downloading the published crate.
// Never fetched, never built, never shipped anywhere but this directory.
"""


def stub_manifest(version, features):
    feats = "\n".join("%s = []" % f for f in sorted(features))
    return """[package]
name = "rustls"
version = "%s"
edition = "2021"
publish = false

# Only the names matter: they exist so the resolution of this step's
# dependents succeeds. Nothing is ever fetched or built from this crate.
[features]
default = []
%s
""" % (version, feats)


def read(path):
    with open(path) as f:
        return f.read()


def blocks(lock):
    """Split a Cargo.lock into (header, [(name, text), ...])."""
    parts = re.split(r'(?=^\[\[package\]\]$)', lock, flags=re.M)
    header, rest = parts[0], parts[1:]
    out = []
    for b in rest:
        m = re.search(r'^name = "([^"]+)"$', b, flags=re.M)
        out.append((m.group(1) if m else "", b))
    return header, out


def join(header, blks):
    return header + "".join(text for _, text in blks)


def transform_manifest(text, step):
    """Drop the direct pin, add the patch section. Idempotent."""
    out = re.sub(r'^rustls_[0-9_]+ = \{ package = "rustls", version = "=[^"]*" \}\n',
                 "", text, flags=re.M)
    if "[patch.crates-io]" not in out:
        out = out.rstrip("\n") + "\n" + PATCH_COMMENT
    return out


def path_entry(version):
    return '[[package]]\nname = "rustls"\nversion = "%s"\n' % version


def transform_lock(text, step):
    """Point the lock's single `rustls` package at the local stub. Idempotent."""
    version, _ = STUBS[step]
    header, blks = blocks(text)
    for i, (name, b) in enumerate(blks):
        if name == "prefetch-stand-in":
            # the stand-in's own dependency list must not name rustls
            b = re.sub(r'^ "rustls(?: [^"]*)?",\n', "", b, flags=re.M)
            blks[i] = (name, b)
        elif name == "rustls":
            # a registry entry: rewrite it as the path package the release's
            # own lock carries at 0.23.31 and later
            blks[i] = (name, path_entry(version))
    if not any(n == "rustls" for n, _ in blks):
        # steps 12-13: no entry at all; insert one in name order
        at = next((i for i, (n, _) in enumerate(blks) if n > "rustls"), len(blks))
        blks.insert(at, ("rustls", path_entry(version)))
    return join(header, blks)


def assert_standin(manifest, lock, step):
    if len(re.findall(r'^name = "rustls"$', lock, flags=re.M)) != 1:
        raise SystemExit("step %s: lock does not have exactly one rustls" % step)
    entry = re.search(r'\[\[package\]\]\nname = "rustls"\nversion = "[^"]*"\n'
                      r'(source = [^\n]*\n)?', lock)
    if entry and entry.group(1):
        raise SystemExit("step %s: lock's rustls entry still names a source" % step)
    if 'package = "rustls",' in manifest:
        raise SystemExit("step %s: manifest still pins the published crate" % step)
    if "[patch.crates-io]" not in manifest:
        raise SystemExit("step %s: manifest has no [patch.crates-io]" % step)


def assert_pinned(manifest, step):
    if not re.search(r'^rustls_[0-9_]+ = \{ package = "rustls", version = "=',
                     manifest, flags=re.M):
        raise SystemExit("step %s: published-rustls pin is missing" % step)
    if "[patch.crates-io]" in manifest:
        raise SystemExit("step %s: patch section present but must not be" % step)


def step_dir(pkg, step):
    ds = [d for d in os.listdir(os.path.join(pkg, "steps"))
          if d.startswith(step + "-")]
    if len(ds) != 1:
        raise SystemExit("step %s: %d step directories" % (step, len(ds)))
    return os.path.join(pkg, "steps", ds[0])


def release_faithful_copy(pkg, step):
    """The verifier-side copy for a step whose release lock names the registry
    crate: it must still pin it, and must not carry the stub."""
    dst = os.path.join(step_dir(pkg, step), "tests/assets/prefetch")
    assert_pinned(read(os.path.join(dst, "Cargo.toml")), step)
    if os.path.exists(os.path.join(dst, "rustls-stub")):
        raise SystemExit("step %s: verifier copy carries the stub but must not" % step)
    if 'rustls-stub' in read(os.path.join(dst, "Cargo.toml")):
        raise SystemExit("step %s: verifier copy has a patch section but must not" % step)


def wanted(pkg):
    """path -> content, for every file the generator owns."""
    out = {}
    for step in STANDIN_STEPS:
        env = os.path.join(pkg, "environment/assets/prefetch", step)
        manifest = transform_manifest(read(os.path.join(env, "Cargo.toml")), step)
        lock = transform_lock(read(os.path.join(env, "Cargo.lock")), step)
        assert_standin(manifest, lock, step)
        version, features = STUBS[step]
        out[os.path.join(env, "Cargo.toml")] = manifest
        out[os.path.join(env, "Cargo.lock")] = lock
        out[os.path.join(env, "rustls-stub/Cargo.toml")] = stub_manifest(version, features)
        out[os.path.join(env, "rustls-stub/src/lib.rs")] = STUB_LIB
        if step in VERIFIER_LEAKFREE:
            dst = os.path.join(step_dir(pkg, step), "tests/assets/prefetch")
            for name, text in list(out.items()):
                if name.startswith(env + os.sep):
                    out[os.path.join(dst, os.path.relpath(name, env))] = text
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pkg", default=os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    pkg = args.pkg
    if not os.path.isdir(os.path.join(pkg, "environment/assets/prefetch")):
        raise SystemExit("not a package root: %s" % pkg)

    for step in PINNED_STEPS:
        env = os.path.join(pkg, "environment/assets/prefetch", step)
        assert_pinned(read(os.path.join(env, "Cargo.toml")), step)
        if os.path.exists(os.path.join(env, "rustls-stub")):
            raise SystemExit("step %s: stub present but step must stay pinned" % step)
        release_faithful_copy(pkg, step)

    # The verifier's copy at the remaining stand-in steps is the release's own
    # graph, not this script's output: it must still carry the registry crate.
    for step in STANDIN_STEPS:
        if step not in VERIFIER_LEAKFREE:
            release_faithful_copy(pkg, step)

    files = wanted(pkg)
    drift = []
    for path, text in sorted(files.items()):
        if not os.path.exists(path) or read(path) != text:
            drift.append(path)
            if not args.check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as f:
                    f.write(text)
    if args.check and drift:
        for p in drift:
            print("drift: %s" % p)
        raise SystemExit(1)
    agent_only = [s for s in STANDIN_STEPS if s not in VERIFIER_LEAKFREE]
    print("prefetch stand-ins: agent copies leak-free at %s; verifier copies leak-free"
          " at %s and release-faithful at %s (pinned at %s); %d agent files%s"
          % (" ".join(STANDIN_STEPS), " ".join(VERIFIER_LEAKFREE),
             " ".join(agent_only), " ".join(PINNED_STEPS), len(files),
             " (%d repaired)" % len(drift) if drift and not args.check else ""))


if __name__ == "__main__":
    sys.exit(main())
