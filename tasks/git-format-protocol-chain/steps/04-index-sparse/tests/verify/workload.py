#!/usr/bin/env python3
"""Run a fixed list of git scenarios against one git binary and record the
result of every probe.  Used twice: at authoring time against a pinned real
git (to record expectations) and at scoring time against the candidate.

Every scenario is a POSIX shell snippet run in a fresh directory with a frozen
identity and clock, followed by probe commands whose stdout/stderr/exit status
are captured.  Nothing here is git-version specific: the probes only use
commands and options that git's own manual pages document.
"""
# Every command a grader starts runs under an address-space ceiling and a
# wall clock, so one runaway process cannot take the node down.
LIMITS = "ulimit -v 4194304 2>/dev/null; ulimit -t 600 2>/dev/null; "

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ENV_BASE = {
    "GIT_AUTHOR_NAME": "A U Thor",
    "GIT_AUTHOR_EMAIL": "author@example.com",
    "GIT_COMMITTER_NAME": "C O Mitter",
    "GIT_COMMITTER_EMAIL": "committer@example.com",
    "GIT_AUTHOR_DATE": "@1112911993 +0000",
    "GIT_COMMITTER_DATE": "@1112911993 +0000",
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_ATTR_NOSYSTEM": "1",
    "TZ": "UTC",
    "LC_ALL": "C",
    "LANG": "C",
    "PAGER": "cat",
    "GIT_PAGER": "cat",
    "GIT_TERMINAL_PROMPT": "0",
}


def norm(text, workdir, gitbin):
    """Remove the only environment-dependent strings from captured output."""
    text = text.replace(workdir, "<WORK>")
    text = text.replace(os.path.realpath(workdir), "<WORK>")
    text = text.replace(gitbin, "<GIT>")
    text = re.sub(r"/opt/gitpeer/v[0-9.]+", "<PEER>", text)
    text = text.replace(os.path.dirname(gitbin), "<GITDIR>")
    # times that leak into reflog/status output are frozen, but the temp dir is not
    text = re.sub(r"/tmp/[A-Za-z0-9._-]+", "<TMP>", text)
    return text



# Commands that run the candidate drop to an unprivileged user when the scorer
# asks for it: the grader's own files stay root-only, so nothing the candidate
# runs can read the expectations or rewrite the scorer.
AS_USER = None


def wrap(cmd):
    if AS_USER is None:
        return cmd
    return ("setpriv --reuid=%d --regid=%d --clear-groups --inh-caps=-all "
            "/bin/bash -c %s" % (AS_USER, AS_USER, "'" + cmd.replace("'", "'\\''") + "'"))


def own(path):
    if AS_USER is None:
        return
    # Root walks a tree the candidate uid writes between calls: never follow a
    # planted symlink out of it (chown of the link itself, never its target).
    # A top directory the candidate replaced with a symlink is not walked: the
    # walk would list, and chown, whatever the link points at.
    try:
        if os.path.islink(path):
            os.chown(path, AS_USER, AS_USER, follow_symlinks=False)
            return
        for root, dirs, files in os.walk(path):
            os.chown(root, AS_USER, AS_USER, follow_symlinks=False)
            for n in dirs + files:
                try:
                    os.chown(os.path.join(root, n), AS_USER, AS_USER,
                             follow_symlinks=False)
                except OSError:
                    pass
        os.chown(path, AS_USER, AS_USER, follow_symlinks=False)
    except OSError:
        pass


def write_wrapper(link, gitbin):
    """Root writes the `git` exec wrapper into a directory the candidate uid may
    own: remove whatever is there, then create the file exclusively without
    following a symlink, so a planted (or dangling) link can never redirect a
    root write elsewhere."""
    try:
        os.unlink(link)
    except FileNotFoundError:
        pass
    fd = os.open(link, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o755)
    try:
        os.write(fd, ("#!/bin/sh\nexec %s \"$@\"\n" % gitbin).encode())
        os.fchmod(fd, 0o755)
    finally:
        os.close(fd)


# A pinned real git that reads a candidate-built repository runs on that
# repository's own .git/config, which the candidate wrote.  It must not pick up
# anything that runs a program or changes what it prints: hooks, an fsmonitor
# command, an external diff or textconv driver, a pager, or a global or XDG
# config planted in a HOME the candidate could write.  So the peer is reached
# only through this root-written wrapper, in a root-owned directory that is the
# first entry of PATH (a candidate-writable directory there could shadow `sort`,
# `grep` or `md5sum` in a probe), with a root-owned empty HOME and the
# command-line overrides below, which outrank every config file.
PEER_WRAPPER = """#!/bin/sh
HOME=@HOME@; XDG_CONFIG_HOME=@HOME@/.config
GIT_CONFIG_NOSYSTEM=1; GIT_CONFIG_GLOBAL=/dev/null; GIT_ATTR_NOSYSTEM=1
export HOME XDG_CONFIG_HOME GIT_CONFIG_NOSYSTEM GIT_CONFIG_GLOBAL GIT_ATTR_NOSYSTEM
n=$#; seen=0; want=0
for a in "$@"; do
  if [ $want = 1 ]; then set -- "$@" "$a"; want=0; continue; fi
  if [ $seen = 0 ]; then
    case "$a" in
      -C|-c) set -- "$@" "$a"; want=1; continue;;
      -*) set -- "$@" "$a"; continue;;
      diff|log|show) set -- "$@" "$a" --no-ext-diff --no-textconv; seen=1; continue;;
      *) set -- "$@" "$a"; seen=1; continue;;
    esac
  fi
  set -- "$@" "$a"
done
shift $n
exec @PEER@ -c core.hooksPath=/dev/null -c core.fsmonitor= -c core.pager=cat -c diff.external= "$@"
"""
# Scenarios hand a client `--upload-pack="$(dirname $PEER)/git-upload-pack"`, so
# the peer's server side sits beside the wrapper, under the same overrides.
PEER_UPLOAD_PACK = """#!/bin/sh
HOME=@HOME@; XDG_CONFIG_HOME=@HOME@/.config
GIT_CONFIG_NOSYSTEM=1; GIT_CONFIG_GLOBAL=/dev/null; GIT_ATTR_NOSYSTEM=1
export HOME XDG_CONFIG_HOME GIT_CONFIG_NOSYSTEM GIT_CONFIG_GLOBAL GIT_ATTR_NOSYSTEM
exec @PEER@ -c core.hooksPath=/dev/null -c core.fsmonitor= -c core.pager=cat -c diff.external= upload-pack "$@"
"""


def make_peer_env(base, peerbin):
    """Root-owned bin/ (holding only the peer wrapper) and empty home/ under
    `base`, which the caller places where the candidate cannot write."""
    if os.path.lexists(base):
        if os.path.islink(base) or not os.path.isdir(base):
            os.unlink(base)
        else:
            shutil.rmtree(base)
    bindir = os.path.join(base, "bin")
    home = os.path.join(base, "home")
    os.makedirs(bindir)
    os.makedirs(home)
    for d in (base, bindir, home):
        os.chown(d, 0, 0, follow_symlinks=False)
        os.chmod(d, 0o755)
    for name, text in (("git", PEER_WRAPPER), ("git-upload-pack", PEER_UPLOAD_PACK)):
        fd = os.open(os.path.join(bindir, name),
                     os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o755)
        try:
            os.write(fd, text.replace("@HOME@", home).replace("@PEER@", peerbin).encode())
            os.fchmod(fd, 0o755)
        finally:
            os.close(fd)
    return {"bin": bindir, "home": home, "git": os.path.join(bindir, "git")}


def run_scenario(sc, gitbin, root, keep=False, peers=None):
    work = tempfile.mkdtemp(prefix="wl-", dir=root)
    home = os.path.join(work, "home")
    os.makedirs(home)
    env = dict(os.environ)
    env.update(ENV_BASE)
    env["HOME"] = home
    env["XDG_CONFIG_HOME"] = os.path.join(home, ".config")
    env["GIT_CEILING_DIRECTORIES"] = root
    bindir = os.path.join(work, "bin")
    os.makedirs(bindir)
    # the scenario scripts call plain `git`
    # an exec wrapper, not a symlink -- a candidate entry point that
    # resolves $0 without dereferencing must still find its own libexec.
    link = os.path.join(bindir, "git")
    write_wrapper(link, gitbin)
    env["PATH"] = bindir + ":/usr/bin:/bin:/usr/sbin:/sbin"
    # interop scenarios also get a pinned real git as $PEER
    # The peer is reached through a root-written wrapper in a root-owned
    # directory OUTSIDE `root` (own(root) hands everything under it to the
    # candidate), with its own empty HOME: see make_peer_env.
    penv = None
    if peers and sc.get("peer"):
        penv = make_peer_env(tempfile.mkdtemp(prefix="wlpeer-"),
                             os.path.join(peers, sc["peer"], "bin", "git"))
        env["PEER"] = penv["git"]
        env["PEER_VERSION"] = sc["peer"]
    repo = os.path.join(work, "r")
    os.makedirs(repo)
    out = {"name": sc["name"], "probes": []}
    own(root)
    try:
        setup = subprocess.run(["/bin/sh", "-c", wrap(LIMITS + sc["setup"])], cwd=repo, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
        srt, sout = setup.returncode, setup.stdout
    except (subprocess.TimeoutExpired, OSError) as exc:
        srt, sout = 125, ("workload: setup did not run: %s" % exc).encode()
    out["setup_status"] = srt
    out["setup_output"] = norm(sout.decode("utf-8", "replace"), work, gitbin)
    for probe in sc["probes"]:
        cwd = repo
        if isinstance(probe, dict):
            cmd = probe["cmd"]
            cwd = os.path.join(repo, probe.get("cwd", "."))
        else:
            cmd = probe
        try:
            p = subprocess.run(["/bin/sh", "-c", wrap(LIMITS + cmd)], cwd=cwd, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
            rc, so, se = p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            rc, so, se = 124, b"", b"timeout"
        except OSError as exc:
            rc, so, se = 125, b"", ("workload: probe could not start: %s" % exc).encode()
        out["probes"].append({
            "cmd": cmd,
            "status": rc,
            "stdout": norm(so.decode("utf-8", "replace"), work, gitbin),
            "stderr": norm(se.decode("utf-8", "replace"), work, gitbin),
        })
    if not keep:
        shutil.rmtree(work, ignore_errors=True)
    if penv:
        shutil.rmtree(os.path.dirname(penv["bin"]), ignore_errors=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--git", required=True)
    ap.add_argument("--scenarios", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default=None)
    ap.add_argument("--peers", default=None,
                    help="directory of pinned real gits, exposed to scenarios as $PEER")
    ap.add_argument("--as-user", type=int, default=None,
                    help="run every candidate command as this uid")
    args = ap.parse_args()
    global AS_USER
    AS_USER = args.as_user
    with open(args.scenarios) as fh:
        scenarios = json.load(fh)
    if args.only:
        want = set(args.only.split(","))
        scenarios = [s for s in scenarios if s["name"] in want]
    root = tempfile.mkdtemp(prefix="wlroot-")
    results = []
    for sc in scenarios:
        try:
            results.append(run_scenario(sc, os.path.abspath(args.git), root, peers=args.peers))
        except Exception as exc:  # noqa: BLE001
            sys.stderr.write("workload: scenario %s failed: %r\n" % (sc.get("name"), exc))
    shutil.rmtree(root, ignore_errors=True)
    with open(args.out, "w") as fh:
        json.dump(results, fh, indent=1, sort_keys=True)
    print("scenarios: %d" % len(results))


if __name__ == "__main__":
    main()
